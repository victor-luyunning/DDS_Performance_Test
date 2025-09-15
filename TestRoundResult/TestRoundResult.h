// TestRoundResult.h
#pragma once
#include "SysMetrics.h"

#include <vector>
#include <string>

// ========================
// ����ָ��ö�٣���ѡ���������ֲ������ͣ�
// ========================
enum class TestType {
    THROUGHPUT,
    LATENCY,
    UNKNOWN
};

// ========================
// ���Ľ���ṹ��
// ========================
struct TestRoundResult {
    int round_index;              // �ڼ��ֲ���
    TestType test_type;           // �������ͣ����� or ʱ�ӣ�

    // --- ��Դʹ����� ---
    SysMetrics start_metrics;     // ��ʼʱϵͳ״̬
    SysMetrics end_metrics;       // ����ʱϵͳ״̬
    std::vector<SysMetrics> samples;          // �м�����㣨��ѡ��
    std::vector<float> cpu_usage_history;     // CPU ʹ������ʷ (%)

    // --- ͨ������ָ�� ---
    double total_duration_s = 0.0;            // ���Գ���ʱ�䣨�룩
    uint64_t sent_count = 0;                  // ���Ͱ���
    uint64_t received_count = 0;              // ���հ���
    double loss_rate_percent = 0.0;           // ������ (%)

    // --- �ض��� LATENCY ���ֶ� ---
    double avg_rtt_us = 0.0;                  // ƽ�� RTT��΢�룩
    double min_rtt_us = 0.0;
    double max_rtt_us = 0.0;
    std::vector<double> rtt_samples_us;       // ���� RTT �����������ں���������

    // --- �ض��� THROUGHPUT ���ֶ� ---
    double throughput_mbps = 0.0;             // ���´��� (Mbps)
    double throughput_pps = 0.0;              // ������ (pps)

    // --- ���ݴ�С��Ϣ ---
    int avg_packet_size_bytes = 0;            // ƽ������С

    // --- ���캯�� ---
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
