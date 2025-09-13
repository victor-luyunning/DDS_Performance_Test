﻿// Throughput_Bytes.cpp
#include "Throughput_Bytes.h"

#include "Logger.h"
#include "ResourceUtilization.h"
#include "TestRoundResult.h"
#include "SysMetrics.h"

#include "ZRDDSDataWriter.h"
#include "ZRDDSDataReader.h"
#include "ZRBuiltinTypes.h"

#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace DDS;

// ========================
// 内部类：WriterListener
// ========================

class Throughput_Bytes::WriterListener : public virtual DDS::DataWriterListener {
public:
    WriterListener(std::atomic<bool>& flag, std::mutex& mtx, std::condition_variable& cv)
        : reconnected_flag_(flag), mutex_(mtx), cond_var_(cv), last_current_count_(0) {
    }

    void on_liveliness_lost(DataWriter*, const LivelinessLostStatus&) override {}
    void on_offered_deadline_missed(DataWriter*, const OfferedDeadlineMissedStatus&) override {}
    void on_offered_incompatible_qos(DataWriter*, const OfferedIncompatibleQosStatus&) override {}
    void on_publication_matched(DataWriter*, const PublicationMatchedStatus&) override {}

    /*void on_subscription_matched(DataWriter*, const SubscriptionMatchedStatus& status) override {
        int32_t current = status.current_count;
        int32_t previous = last_current_count_.load();

        last_current_count_.store(current);

        if (current > 0 && previous == 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            reconnected_flag_.store(true);
            cond_var_.notify_all();
            Logger::getInstance().logAndPrint("Throughput_Bytes: 检测到订阅者重新上线");
        }
    }*/

private:
    std::atomic<bool>& reconnected_flag_;
    std::mutex& mutex_;
    std::condition_variable& cond_var_;
    std::atomic<int32_t> last_current_count_;
};

// ========================
// 构造函数 & 析构
// ========================

Throughput_Bytes::Throughput_Bytes(DDSManager_Bytes& ddsManager, ResultCallback callback)
    : ddsManager_(ddsManager)
    , result_callback_(std::move(callback))
    , subscriber_reconnected_(false)
{
    writer_listener_ = std::make_unique<WriterListener>(
        subscriber_reconnected_,
        reconnect_mtx_,
        reconnect_cv_
    );

    DataWriter* writer = ddsManager_.get_data_writer();
    if (writer) {
        ReturnCode_t ret = writer->set_listener(writer_listener_.get(), DDS::SUBSCRIPTION_MATCHED_STATUS);
        if (ret != DDS::RETCODE_OK) {
            Logger::getInstance().logAndPrint("警告：无法为 DataWriter 设置监听器");
        }
    }
}

Throughput_Bytes::~Throughput_Bytes() = default;

// ========================
// 同步函数
// ========================

bool Throughput_Bytes::waitForSubscriberReconnect(const std::chrono::seconds& timeout) {
    std::unique_lock<std::mutex> lock(reconnect_mtx_);
    subscriber_reconnected_ = false;
    return reconnect_cv_.wait_for(lock, timeout, [this] { return subscriber_reconnected_.load(); });
}

void Throughput_Bytes::waitForRoundEnd() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return roundFinished_.load(); });
}

bool Throughput_Bytes::waitForWriterMatch() {
    auto writer = ddsManager_.get_data_writer();
    if (!writer) return false;

    while (true) {
        PublicationMatchedStatus status{};
        ReturnCode_t ret = writer->get_publication_matched_status(status);
        if (ret == RETCODE_OK) {
            Logger::getInstance().logAndPrint(
                "Writer wait match(" + std::to_string(status.current_count) + "/1)"
            );
            if (status.current_count > 0) return true;
        }
        else {
            Logger::getInstance().logAndPrint("Error: Failed to get publication matched status.");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool Throughput_Bytes::waitForReaderMatch() {
    auto reader = ddsManager_.get_data_reader();
    if (!reader) return false;

    while (true) {
        SubscriptionMatchedStatus status{};
        ReturnCode_t ret = reader->get_subscription_matched_status(status);
        if (ret == RETCODE_OK) {
            Logger::getInstance().logAndPrint(
                "Reader wait match(" + std::to_string(status.current_count) + "/1)"
            );
            if (status.current_count > 0) return true;
        }
        else {
            Logger::getInstance().logAndPrint("Error: Failed to get subscription matched status.");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ========================
// runPublisher - 发送逻辑
// ========================

int Throughput_Bytes::runPublisher(const ConfigData& config) {
    using WriterType = DDS::ZRDDSDataWriter<DDS::Bytes>;
    WriterType* writer = dynamic_cast<WriterType*>(ddsManager_.get_data_writer());
    if (!writer) {
        Logger::getInstance().logAndPrint("Throughput_Bytes: DataWriter 为空，无法发送");
        return -1;
    }

    const int round_index = config.m_activeLoop;
    const int minSize = config.m_minSize[round_index];
    const int maxSize = config.m_maxSize[round_index];
    const int sendCount = config.m_sendCount[round_index];

    if (!waitForWriterMatch()) {
        Logger::getInstance().logAndPrint("Throughput_Bytes: 等待 Subscriber 匹配超时");
        return -1;
    }

    std::ostringstream oss;
    oss << "第 " << (round_index + 1) << " 轮吞吐测试 | 发送: " << sendCount
        << " 条 | 数据大小: [" << minSize << ", " << maxSize << "]";
    Logger::getInstance().logAndPrint(oss.str());

    auto& resUtil = ResourceUtilization::instance();
    resUtil.initialize();
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();

    DDS::Bytes sample;
    if (!ddsManager_.prepareBytesData(sample, minSize, maxSize)) {
        Logger::getInstance().logAndPrint("Throughput_Bytes: 准备测试数据失败");
        return -1;
    }

    for (int j = 0; j < sendCount; ++j) {
        if (minSize != maxSize) {
            ddsManager_.cleanupBytesData(sample);  // 🟡 必须清理再重准备
            if (!ddsManager_.prepareBytesData(sample, minSize, maxSize)) {
                break;
            }
        }
        writer->write(sample, DDS_HANDLE_NIL_NATIVE);
    }

    writer->wait_for_acknowledgments({ 10, 0 });

    // === 发送结束包 ===
    ddsManager_.cleanupBytesData(sample);
    if (ddsManager_.prepareBytesData(sample, minSize, maxSize)) {
        if (sample.value.length() > 0) {
            sample.value[0] = 255;
        }
        for (int k = 0; k < 3; ++k) {
            writer->write(sample, DDS_HANDLE_NIL_NATIVE);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ddsManager_.cleanupBytesData(sample);

    SysMetrics end_metrics = resUtil.collectCurrentMetrics();
    if (result_callback_) {
        result_callback_(TestRoundResult{ round_index + 1, start_metrics, end_metrics });
    }

    Logger::getInstance().logAndPrint("第 " + std::to_string(round_index + 1) + " 轮发送完成");
    return 0;
}

// ========================
// runSubscriber - 接收逻辑
// ========================

int Throughput_Bytes::runSubscriber(const ConfigData& config) {
    using ReaderType = DDS::ZRDDSDataReader<DDS::Bytes, DDS::BytesSeq>;
    ReaderType* reader = dynamic_cast<ReaderType*>(ddsManager_.get_data_reader());
    if (!reader) {
        Logger::getInstance().logAndPrint("Throughput_Bytes: DataReader 为空，无法接收");
        return -1;
    }

    const int round_index = config.m_activeLoop;
    const int expected = config.m_sendCount[round_index];

    if (!waitForReaderMatch()) {
        Logger::getInstance().logAndPrint("Throughput_Bytes: 等待 Publisher 匹配超时");
        return -1;
    }

    Logger::getInstance().logAndPrint("第 " + std::to_string(round_index + 1) + " 轮接收开始");

    auto& resUtil = ResourceUtilization::instance();
    resUtil.initialize();
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();

    receivedCount_.store(0);
    roundFinished_.store(false);

    waitForRoundEnd();

    SysMetrics end_metrics = resUtil.collectCurrentMetrics();
    if (result_callback_) {
        result_callback_(TestRoundResult{ round_index + 1, start_metrics, end_metrics });
    }

    int received = receivedCount_.load();
    int lost = expected - received;
    double lossRate = expected > 0 ? (double)lost / expected * 100.0 : 0.0;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6)
        << "吞吐测试 (Bytes) | 第 " << (round_index + 1) << " 轮 | "
        << "接收: " << received << " | "
        << "丢包: " << lost << " | "
        << "丢包率: " << lossRate << "%";
    Logger::getInstance().logAndPrint(oss.str());

    return 0;
}

// ========================
// 回调函数
// ========================

void Throughput_Bytes::onDataReceived(const DDS::Bytes& /*sample*/, const DDS::SampleInfo& info) {
    if (!info.valid_data) return;
    ++receivedCount_;
}

void Throughput_Bytes::onEndOfRound() {
    roundFinished_.store(true);
    cv_.notify_one();
}