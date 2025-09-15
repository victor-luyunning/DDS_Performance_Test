// LatencyTest_Bytes.cpp
#include "LatencyTest_Bytes.h"

#include "Logger.h"
#include "ResourceUtilization.h"
#include "SysMetrics.h"

#include "ZRDDSDataWriter.h"
#include "ZRDDSDataReader.h"
#include "ZRBuiltinTypes.h"

#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>

using namespace DDS;
using namespace std;

// ========================
// 构造函数 & 析构
// ========================

LatencyTest_Bytes::LatencyTest_Bytes(DDSManager_Bytes& dds_manager, ResultCallback callback)
    : dds_manager_(dds_manager)
    , result_callback_(std::move(callback)) {
}

LatencyTest_Bytes::~LatencyTest_Bytes() = default;

// ========================
// 回调函数：由 DataReader 触发（Responder 模式下处理 Ping）
// ========================

void LatencyTest_Bytes::onDataReceived(const DDS::Bytes& sample, const DDS::SampleInfo& info) {
    if (!info.valid_data) return;

    const uint8_t* buffer = sample.value.get_contiguous_buffer();
    if (!buffer || sample.value.length() < sizeof(PacketHeader)) return;

    const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(buffer);
    if (hdr->packet_type != DATA_PACKET) return;

    // === ✅ 回复 Pong ===
    Bytes pong_sample;
    dds_manager_.cleanupBytesData(pong_sample);

    size_t reply_size = sample.value._length;
    if (dds_manager_.prepareBytesData(pong_sample,
        static_cast<int>(reply_size), static_cast<int>(reply_size),
        hdr->sequence, hdr->timestamp_us)) {

        PacketHeader* out_hdr = reinterpret_cast<PacketHeader*>(
            pong_sample.value.get_contiguous_buffer());
        out_hdr->packet_type = DATA_PACKET; // 确保标记为数据包

        ZRDDSDataWriter<DDS::Bytes>* pong_writer =
            dynamic_cast<ZRDDSDataWriter<DDS::Bytes>*>(dds_manager_.get_Pong_data_writer());
        if (pong_writer) {
            ReturnCode_t ret = pong_writer->write(pong_sample, DDS_HANDLE_NIL_NATIVE);
            if (ret != RETCODE_OK) {
                Logger::getInstance().error("Pong write failed: " + to_string(ret));
            }
        }
    }

    dds_manager_.cleanupBytesData(pong_sample);

    // 可选日志
    static int count = 0;
    if (++count % 10000 == 0) {
        Logger::getInstance().logAndPrint("已回复 " + to_string(count) + " 个 Pong");
    }
}

void LatencyTest_Bytes::onEndOfRound() {
    // 本模块不使用结束包触发行为
}

// ========================
// 报告结果
// ========================

void LatencyTest_Bytes::report_results(int round_index, int expected_count, int avg_packet_size) {
    int received = static_cast<int>(received_sequences_.size());
    int lost = expected_count - received;
    double loss_rate = expected_count > 0 ? (double)lost / expected_count * 100.0 : 0.0;

    if (rtt_times_us_.empty()) {
        Logger::getInstance().logAndPrint("警告：未收到任何 Pong 回包");
        return;
    }

    double min_rtt = *min_element(rtt_times_us_.begin(), rtt_times_us_.end());
    double max_rtt = *max_element(rtt_times_us_.begin(), rtt_times_us_.end());
    double avg_rtt = accumulate(rtt_times_us_.begin(), rtt_times_us_.end(), 0.0) / rtt_times_us_.size();

    ostringstream oss;
    oss << fixed << setprecision(2)
        << "时延测试结果 | 第 " << (round_index + 1) << " 轮 | "
        << "发送: " << expected_count << " | "
        << "收到: " << received << " | "
        << "丢包: " << lost << " (" << loss_rate << "%) | "
        << "Avg RTT: " << avg_rtt << " μs | "
        << "Min RTT: " << min_rtt << " μs | "
        << "Max RTT: " << max_rtt << " μs";

    Logger::getInstance().logAndPrint(oss.str());
}

// ========================
// runPublisher - Initiator: 发送 Ping 并接收 Pong
// ========================

int LatencyTest_Bytes::runPublisher(const ConfigData& config) {
    using WriterType = ZRDDSDataWriter<DDS::Bytes>;
    using ReaderType = ZRDDSDataReader<DDS::Bytes, DDS::BytesSeq>;

    WriterType* ping_writer = dynamic_cast<WriterType*>(dds_manager_.get_Ping_data_writer());
    ReaderType* pong_reader = dynamic_cast<ReaderType*>(dds_manager_.get_Pong_data_reader());

    if (!ping_writer) {
        Logger::getInstance().error("LatencyTest_Bytes: Ping DataWriter 为空");
        return -1;
    }

    const int round_index = config.m_activeLoop;
    const int min_size = config.m_minSize[round_index];
    const int max_size = config.m_maxSize[round_index];
    const int send_count = config.m_sendCount[round_index];
    const int print_gap = config.m_sendPrintGap[round_index];

    // ✅ 等待匹配（确保 Responder 已上线）
    if (!ping_writer->wait_for_acknowledgments({ 10, 0 })) {
        Logger::getInstance().logAndPrint("LatencyTest_Bytes: 等待 Subscriber ACK 超时");
        return -1;
    }

    ostringstream oss;
    oss << "第 " << (round_index + 1) << " 轮时延测试（Initiator）| 发送: " << send_count
        << " 次 | 数据大小: [" << min_size << ", " << max_size << "]";
    Logger::getInstance().logAndPrint(oss.str());

    auto& resUtil = ResourceUtilization::instance();
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();
    start_time_ = chrono::steady_clock::now();

    Bytes ping_sample;
    rtt_times_us_.clear();
    received_sequences_.clear();

    // 🔁 初始化时延模式：收 Pong 包
    bool init_ok = dds_manager_.initialize_latency(
        nullptr,                              // 我不关心谁发了 Ping
        [this](const DDS::Bytes& s, const DDS::SampleInfo& i) {
            this->handlePongReceived(s, i);   // 处理回包
        },
        nullptr                               // 不处理结束包
    );

    if (!init_ok) {
        Logger::getInstance().error("LatencyTest_Bytes: 初始化作为 Initiator 失败");
        return -1;
    }

    int sent = 0;
    for (int i = 0; i < send_count; ++i) {
        uint64_t send_timestamp_us = chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now().time_since_epoch()
        ).count();

        if (!dds_manager_.prepareBytesData(ping_sample, min_size, max_size, i, send_timestamp_us)) {
            Logger::getInstance().error("准备第 " + to_string(i) + " 个 Ping 包失败");
            continue;
        }

        PacketHeader* hdr = reinterpret_cast<PacketHeader*>(ping_sample.value.get_contiguous_buffer());
        hdr->packet_type = DATA_PACKET;

        ReturnCode_t ret = ping_writer->write(ping_sample, DDS_HANDLE_NIL_NATIVE);
        if (ret == RETCODE_OK) {
            ++sent;
            if (sent % print_gap == 0) {
                Logger::getInstance().logAndPrint("已发送 " + to_string(sent) + " 个 Ping");
            }
        }
        else {
            Logger::getInstance().error("Ping write failed: " + to_string(ret));
        }

        dds_manager_.cleanupBytesData(ping_sample);

        this_thread::sleep_for(chrono::microseconds(0)); // 使用配置间隔
    }

    // ✅ 等待所有回复
    this_thread::sleep_for(chrono::seconds(5));

    end_time_ = chrono::steady_clock::now();
    SysMetrics end_metrics = resUtil.collectCurrentMetrics();

    // 📊 计算并打印时延统计（仅日志输出，暂不上报）
    int received = static_cast<int>(received_sequences_.size());
    int lost = send_count - received;
    double loss_rate = send_count > 0 ? (double)lost / send_count * 100.0 : 0.0;

    double duration_seconds = chrono::duration<double>(end_time_ - start_time_).count();

    double min_rtt = rtt_times_us_.empty() ? 0.0 : *min_element(rtt_times_us_.begin(), rtt_times_us_.end());
    double max_rtt = rtt_times_us_.empty() ? 0.0 : *max_element(rtt_times_us_.begin(), rtt_times_us_.end());
    double avg_rtt = rtt_times_us_.empty() ? 0.0 : accumulate(rtt_times_us_.begin(), rtt_times_us_.end(), 0.0) / rtt_times_us_.size();

    oss << fixed << setprecision(2);
    oss << "=== 第 " << (round_index + 1) << " 轮时延测试完成 ===\n"
        << "  发送 Ping 数: " << send_count << "\n"
        << "  收到 Pong 数: " << received << "\n"
        << "     丢包数量: " << lost << "\n"
        << "     丢包率:   " << loss_rate << "%\n"
        << "  测试持续时间: " << duration_seconds << " 秒\n"
        << "  数据包大小:   [" << min_size << ", " << max_size << "] 字节\n"
        << "  RTT 统计:\n"
        << "     平均 RTT:  " << avg_rtt << " μs\n"
        << "     最小 RTT:  " << min_rtt << " μs\n"
        << "     最大 RTT:  " << max_rtt << " μs";

    Logger::getInstance().logAndPrint(oss.str());

    return 0;
}

// ========================
// handlePongReceived - 处理从 Pong Reader 收到的数据
// ========================

void LatencyTest_Bytes::handlePongReceived(const DDS::Bytes& sample, const DDS::SampleInfo& info) {
    if (!info.valid_data) return;

    const uint8_t* buffer = sample.value.get_contiguous_buffer();
    if (!buffer || sample.value.length() < sizeof(PacketHeader)) return;

    const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(buffer);
    if (hdr->packet_type != DATA_PACKET) return;

    uint64_t recv_time_us = chrono::duration_cast<chrono::microseconds>(
        chrono::steady_clock::now().time_since_epoch()
    ).count();

    int64_t rtt = static_cast<int64_t>(recv_time_us - hdr->timestamp_us);
    if (rtt > 0) {
        rtt_times_us_.push_back(static_cast<double>(rtt));
    }

    received_sequences_.insert(hdr->sequence);
}

// ========================
// runSubscriber - Responder: 接收 Ping 并自动回 Pong
// ========================

int LatencyTest_Bytes::runSubscriber(const ConfigData& config) {
    using ReaderType = ZRDDSDataReader<DDS::Bytes, DDS::BytesSeq>;
    ReaderType* ping_reader = dynamic_cast<ReaderType*>(dds_manager_.get_Ping_data_reader());
    if (!ping_reader) {
        Logger::getInstance().error("LatencyTest_Bytes: Ping DataReader 为空");
        return -1;
    }

    const int round_index = config.m_activeLoop;

    // 等待 Publisher 匹配
    while (true) {
        SubscriptionMatchedStatus status{};
        if (ping_reader->get_subscription_matched_status(status) == RETCODE_OK) {
            if (status.current_count > 0) break;
        }
        this_thread::sleep_for(chrono::seconds(1));
    }

    Logger::getInstance().logAndPrint("第 " + to_string(round_index + 1) + " 轮时延测试开始（Responder 模式）");

    auto& resUtil = ResourceUtilization::instance();
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();

    // 🔁 初始化：只监听 Ping，自动回 Pong
    bool init_ok = dds_manager_.initialize_latency(
        [this](const DDS::Bytes& s, const DDS::SampleInfo& i) {
            this->onDataReceived(s, i);   // 自动回复 Pong
        },
        nullptr,                          // 不接收 Pong（我是 Responder）
        nullptr                           // 不处理结束包
    );

    if (!init_ok) {
        Logger::getInstance().error("LatencyTest_Bytes: Responder 初始化失败");
        return -1;
    }

    // 持续运行
    while (true) {
        this_thread::sleep_for(chrono::seconds(1));
    }
}