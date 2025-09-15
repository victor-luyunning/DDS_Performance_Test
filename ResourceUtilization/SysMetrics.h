#pragma once
#include <cstdint>

// ϵͳ��Դָ��ṹ��
struct SysMetrics {
    // CPU ʹ���ʷ�ֵ (�ٷֱ�)
    // -1.0 ��ʾ��ʼ��ʧ�ܻ�δ��ȡ������
    // >= 0.0 ��ʾ��Ч�ķ�ֵ�ٷֱ�
    double cpu_usage_percent_peak = -1.0;

    // �ڴ����ָ�� (��λ: KB)
    unsigned long long memory_peak_kb = 0;
    unsigned long long memory_current_kb = 0;

    // �ڴ����/�ͷ�ͳ��
    unsigned long long memory_alloc_count = 0;
    unsigned long long memory_dealloc_count = 0;

    // ��ǰδ�ͷŵ��ڴ����
    long long memory_current_blocks = 0;
};