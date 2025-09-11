// SysMetrics.h
#pragma once
#include <cstdint>

struct SysMetrics {
    double cpu_usage_percent = 0.0;           // ��ǰ����CPUʹ���� (%)
    uint64_t memory_peak_kb = 0;              // ��ֵ�ڴ� (KB)
    uint64_t memory_current_kb = 0;           // ��ǰ�ڴ� (KB)
    uint64_t memory_alloc_count = 0;          // �ܷ������
    uint64_t memory_dealloc_count = 0;        // ���ͷŴ���
    uint64_t memory_current_blocks = 0;       // ��ǰ��Ծ����

    // ���캯������ѡ��
    SysMetrics() = default;
};