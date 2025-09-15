// LatencyTest_Bytes.h
#pragma once

#include "ConfigData.h"
#include "DDSManager_Bytes.h"
#include "TestRoundResult.h"

#include <functional>
#include <vector>
#include <set>
#include <chrono>

struct PacketHeader {
    uint32_t sequence;
    uint64_t timestamp_us;
    uint8_t  packet_type;
};

#define DATA_PACKET     0
#define END_PACKET      1

class LatencyTest_Bytes {
public:
    using ResultCallback = std::function<void(const TestRoundResult&)>;

    LatencyTest_Bytes(DDSManager_Bytes& dds_manager, ResultCallback callback);
    ~LatencyTest_Bytes();

    int runPublisher(const ConfigData& config);
    int runSubscriber(const ConfigData& config);

    void onDataReceived(const DDS::Bytes& sample, const DDS::SampleInfo& info);
    void onEndOfRound();

private:
    void handlePongReceived(const DDS::Bytes& sample, const DDS::SampleInfo& info);
    void report_results(int round_index, int expected_count, int avg_packet_size);

    DDSManager_Bytes& dds_manager_;
    ResultCallback result_callback_;

    std::vector<double> rtt_times_us_;
    std::set<uint32_t> received_sequences_;
    std::chrono::steady_clock::time_point start_time_, end_time_;
};