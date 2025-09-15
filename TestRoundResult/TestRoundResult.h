// TestRoundResult.h
#pragma once
#include "SysMetrics.h"

#include <vector>
#include <string>

// ========================
// 性能指标枚举（可选，用于区分测试类型）
// ========================
enum class TestType {
    THROUGHPUT,
    LATENCY,
    UNKNOWN
};

// ========================
// 核心结果结构体
// ========================
struct TestRoundResult {
    int round_index;              // 第几轮测试
    TestType test_type;           // 测试类型（吞吐 or 时延）

    // --- 资源使用情况 ---
    SysMetrics start_metrics;     // 开始时系统状态
    SysMetrics end_metrics;       // 结束时系统状态
    std::vector<SysMetrics> samples;          // 中间采样点（可选）
    std::vector<float> cpu_usage_history;     // CPU 使用率历史 (%)

    // --- 通用性能指标 ---
    double total_duration_s = 0.0;            // 测试持续时间（秒）
    uint64_t sent_count = 0;                  // 发送包数
    uint64_t received_count = 0;              // 接收包数
    double loss_rate_percent = 0.0;           // 丢包率 (%)

    // --- 特定于 LATENCY 的字段 ---
    double avg_rtt_us = 0.0;                  // 平均 RTT（微秒）
    double min_rtt_us = 0.0;
    double max_rtt_us = 0.0;
    std::vector<double> rtt_samples_us;       // 所有 RTT 样本（可用于后续分析）

    // --- 特定于 THROUGHPUT 的字段 ---
    double throughput_mbps = 0.0;             // 吞吐带宽 (Mbps)
    double throughput_pps = 0.0;              // 包速率 (pps)

    // --- 数据大小信息 ---
    int avg_packet_size_bytes = 0;            // 平均包大小

    // --- 构造函数 ---
    TestRoundResult()
        : round_index(0)
        , test_type(TestType::UNKNOWN)
        , cpu_usage_history({})
        , rtt_samples_us({}) {
    }

    TestRoundResult(int idx, const SysMetrics& start, const SysMetrics& end, TestType type = TestType::UNKNOWN)
        : round_index(idx)
        , test_type(type)
        , start_metrics(start)
        , end_metrics(end)
        , cpu_usage_history({})
        , rtt_samples_us({}) {
    }
}; 
