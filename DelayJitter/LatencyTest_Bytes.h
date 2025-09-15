// LatencyTest_Bytes.h
#pragma once

#include "DDSManager_Bytes.h"
#include "ConfigData.h"
#include "TestRoundResult.h"

#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <set>

using namespace DDS;

// ========================
// 自定义数据包头结构（去除了 round_id）
// ========================
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t sequence;       // 包序号
    uint64_t timestamp_us;   // 发送时间戳（微秒）
    uint8_t  packet_type;    // 包类型

    PacketHeader(uint32_t seq = 0, uint64_t ts = 0, uint8_t type = 0)
        : sequence(seq), timestamp_us(ts), packet_type(type) {
    }
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 13, "PacketHeader must be 13 bytes");

enum CommandType : uint8_t {
    DATA_PACKET = 0,
};

class LatencyTest_Bytes {
public:
    using ResultCallback = std::function<void(const TestRoundResult&)>;

    LatencyTest_Bytes(DDSManager_Bytes& dds_manager, ResultCallback callback);
    ~LatencyTest_Bytes();

    int runPublisher(const ConfigData& config);
    int runSubscriber(const ConfigData& config);

    void onDataReceived(const DDS::Bytes& sample, const DDS::SampleInfo& info);

private:
    void onEndOfRound();

    void report_results(int round_index, int expected_count, int avg_packet_size); // ✅ 声明函数！

    // === 成员变量 ===
    DDSManager_Bytes& dds_manager_;
    ResultCallback result_callback_;

    // 接收状态
    std::atomic<bool> roundFinished_{ false };
    std::mutex mtx_;
    std::condition_variable cv_;

    // 统计数据
    std::vector<double> rtt_times_us_;     // 存储每轮 RTT 值
    std::chrono::steady_clock::time_point start_time_; // 第一个 Pong 到达时间
    std::chrono::steady_clock::time_point end_time_;   // 最后一个 Pong 到达时间

    // 同步 & 分析
    std::mutex received_seqs_mutex_;
    std::set<uint32_t> received_sequences_; // 收到的 Pong 序号集合
};