// Throughput.cpp
#include "Throughput.h"

// 第三方头文件
#include "Logger.h"
#include "TestDataDataWriter.h"
#include "TestDataDataReader.h"
#include "ResourceUtilization.h"
#include "TestRoundResult.h"
#include "SysMetrics.h"

#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace DDS;

// ========================
// 内部类：WriterListener (ZRDDS 兼容版)
// ========================

class Throughput::WriterListener : public virtual DDS::DataWriterListener {
public:
    WriterListener(std::atomic<bool>& flag, std::mutex& mtx, std::condition_variable& cv)
        : reconnected_flag_(flag), mutex_(mtx), cond_var_(cv), last_current_count_(0) {
    } // ✅ 添加 last_current_count_ 初始化

// ✅ 继承自 DDS::DataWriterListener，解决类型不兼容问题

    void on_liveliness_lost(DDS::DataWriter* /*writer*/, const DDS::LivelinessLostStatus& /*status*/) {
        // 留空
    }

    void on_offered_deadline_missed(DDS::DataWriter* /*writer*/, const DDS::OfferedDeadlineMissedStatus& /*status*/) {
        // 留空
    }

    void on_offered_incompatible_qos(DDS::DataWriter* /*writer*/, const DDS::OfferedIncompatibleQosStatus& /*status*/) {
        // 留空
    }

    void on_publication_matched(DDS::DataWriter* /*writer*/, const DDS::PublicationMatchedStatus& /*status*/) {
        // 这个回调在这里不是必需的
    }

    // ✅ 关键回调：当订阅者匹配状态变化时触发
    void on_subscription_matched(DDS::DataWriter* /*writer*/, const DDS::SubscriptionMatchedStatus& status) {
        // ✅ 使用手动维护的 last_current_count_ 来判断是否为“从无到有”的连接事件
        int32_t current = status.current_count;
        int32_t previous = last_current_count_.load();

        // 更新 last_current_count_
        last_current_count_.store(current);

        if (current > 0 && previous == 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            reconnected_flag_.store(true);
            cond_var_.notify_all();
            Logger::getInstance().logAndPrint("Throughput: 检测到订阅者重新上线");
        }
    }

private:
    std::atomic<bool>& reconnected_flag_;
    std::mutex& mutex_;
    std::condition_variable& cond_var_;
    std::atomic<int32_t> last_current_count_; 
};


// ========================
// 构造函数 & 成员函数实现
// ========================

Throughput::Throughput(DDSManager& ddsManager, ResultCallback callback)
    : ddsManager_(ddsManager)
    , result_callback_(std::move(callback))
    , subscriber_reconnected_(false)
{
    // 创建监听器
    writer_listener_ = std::make_unique<WriterListener>(
        subscriber_reconnected_,
        reconnect_mtx_,
        reconnect_cv_
    );

    // 获取 DataWriter 并设置监听器
    DataWriter* writer = ddsManager_.get_data_writer();
    if (writer) {
        ReturnCode_t ret = writer->set_listener(writer_listener_.get(), DDS::SUBSCRIPTION_MATCHED_STATUS);
        if (ret != DDS::RETCODE_OK) {
            Logger::getInstance().logAndPrint("警告：无法为 DataWriter 设置监听器");
        }
    }
}

Throughput::~Throughput() = default;

bool Throughput::waitForSubscriberReconnect(const std::chrono::seconds& timeout) {
    std::unique_lock<std::mutex> lock(reconnect_mtx_);
    subscriber_reconnected_ = false; // 重置标志
    return reconnect_cv_.wait_for(lock, timeout, [this] {
        return subscriber_reconnected_.load();
        });
}

void Throughput::waitForRoundEnd() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return roundFinished_.load(); });
}

bool Throughput::waitForWriterMatch() {
    auto writer = ddsManager_.get_data_writer();
    if (!writer) return false;

    while (true) {
        PublicationMatchedStatus status{};
        ReturnCode_t ret = writer->get_publication_matched_status(status);
        if (ret == RETCODE_OK) {
            // 打印当前匹配状态
            Logger::getInstance().logAndPrint(
                "Writer wait match(" + std::to_string(status.current_count) + "/1)"
            );

            if (status.current_count > 0) {
                return true; // 匹配成功
            }
        }
        else {
            Logger::getInstance().logAndPrint("Error: Failed to get publication matched status.");
            return false;
        }

        // 每隔一秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool Throughput::waitForReaderMatch() {
    auto reader = ddsManager_.get_data_reader();
    if (!reader) return false;

    while (true) {
        SubscriptionMatchedStatus status{};
        ReturnCode_t ret = reader->get_subscription_matched_status(status);
        if (ret == RETCODE_OK) {
            // 打印当前匹配状态
            Logger::getInstance().logAndPrint(
                "Reader wait match(" + std::to_string(status.current_count) + "/1)"
            );

            if (status.current_count > 0) {
                return true; // 匹配成功
            }
        }
        else {
            Logger::getInstance().logAndPrint("Error: Failed to get subscription matched status.");
            return false;
        }

        // 每隔一秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ========================
// runPublisher - 单轮发送
// ========================

int Throughput::runPublisher(const ConfigData& config) {
    TestDataDataWriter* writer = dynamic_cast<TestDataDataWriter*>(ddsManager_.get_data_writer());
    if (!writer) {
        Logger::getInstance().logAndPrint("Throughput: DataWriter 为空，无法发送");
        return -1;
    }

    const int round_index = config.m_activeLoop;
    const int minSize = config.m_minSize[round_index];
    const int maxSize = config.m_maxSize[round_index];
    const int sendCount = config.m_sendCount[round_index];

    // 等待匹配
    if (!waitForWriterMatch()) {
        Logger::getInstance().logAndPrint("Throughput: 等待 Subscriber 匹配超时");
        return -1;
    }

    std::ostringstream oss;
    oss << "第 " << (round_index + 1) << " 轮吞吐测试 | 发送: " << sendCount
        << " 条 | 数据大小: [" << minSize << ", " << maxSize << "]";
    Logger::getInstance().logAndPrint(oss.str());  

    auto& resUtil = ResourceUtilization::instance();
    resUtil.initialize(); // 初始化系统资源采集
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();

    TestData sample;
    if (!ddsManager_.prepareTestData(sample, minSize, maxSize)) {
        Logger::getInstance().logAndPrint("Throughput: 准备测试数据失败");
        return -1;
    }

    // === 发送数据 ===
    for (int j = 0; j < sendCount; ++j) {
        // 如果是变长数据，每条都重新准备
        if (minSize != maxSize) {
            ddsManager_.cleanupTestData(sample);
            if (!ddsManager_.prepareTestData(sample, minSize, maxSize)) {
                break;
            }
        }
        writer->write(sample, DDS_HANDLE_NIL_NATIVE);
    }

    // 等待所有数据被确认
    writer->wait_for_acknowledgments({ 10, 0 });

    // === 发送结束包（标记本轮结束）===
    ddsManager_.cleanupTestData(sample);
    if (ddsManager_.prepareTestData(sample, minSize, maxSize)) {
        if (sample.value.length() > 0) {
            sample.value[0] = 255; // 结束标志
        }
        // 发送3次以确保送达
        for (int k = 0; k < 3; ++k) {
            writer->write(sample, DDS_HANDLE_NIL_NATIVE);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ddsManager_.cleanupTestData(sample);

    // === 收集结束时资源数据 ===
    SysMetrics end_metrics = resUtil.collectCurrentMetrics();
    if (result_callback_) {
        result_callback_(TestRoundResult{ round_index + 1, start_metrics, end_metrics });
    }

    Logger::getInstance().logAndPrint("第 " + std::to_string(round_index + 1) + " 轮发送完成");
    return 0;
}

// ========================
// runSubscriber - 单轮接收
// ========================

int Throughput::runSubscriber(const ConfigData& config) {
    TestDataDataReader* reader = dynamic_cast<TestDataDataReader*>(ddsManager_.get_data_reader());
    if (!reader) {
        Logger::getInstance().logAndPrint("Throughput: DataReader 为空，无法接收");
        return -1;
    }

    const int round_index = config.m_activeLoop;
    const int expected = config.m_sendCount[round_index];

    if (!waitForReaderMatch()) {
        Logger::getInstance().logAndPrint("Throughput: 等待 Publisher 匹配超时");
        return -1;
    }

    Logger::getInstance().logAndPrint("第 " + std::to_string(round_index + 1) + " 轮接收开始");

    auto& resUtil = ResourceUtilization::instance();
    resUtil.initialize();
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();

    receivedCount_.store(0);
    roundFinished_.store(false);

    waitForRoundEnd(); // 阻塞等待结束包

    SysMetrics end_metrics = resUtil.collectCurrentMetrics();
    if (result_callback_) {
        result_callback_(TestRoundResult{ round_index + 1, start_metrics, end_metrics });
    }

    int received = receivedCount_.load();
    int lost = expected - received;
    double lossRate = expected > 0 ? (double)lost / expected * 100.0 : 0.0;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6)
        << "吞吐测试 | 第 " << (round_index + 1) << " 轮 | "
        << "接收: " << received << " | "
        << "丢包: " << lost << " | "
        << "丢包率: " << lossRate << "%";
    Logger::getInstance().logAndPrint(oss.str());

    return 0;
}

void Throughput::onDataReceived(const TestData& sample, const DDS::SampleInfo& info) {
    ++receivedCount_;
    // 可选：加日志或性能采样
}

void Throughput::onEndOfRound() {
    roundFinished_.store(true);
    cv_.notify_one();
}