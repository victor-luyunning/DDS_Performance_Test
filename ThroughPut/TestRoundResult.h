// TestRoundResult.h 
#pragma once
#include "SysMetrics.h"

#include <vector>

struct TestRoundResult {
    int round_index;              // �ڼ���
    SysMetrics start_metrics;     // ��ʼʱ����Դ״̬
    SysMetrics end_metrics;       // ����ʱ����Դ״̬

    // ��ѡ���м�����㣨���ڻ�������ͼ��
    std::vector<SysMetrics> samples;

    TestRoundResult(int idx, const SysMetrics& start, const SysMetrics& end)
        : round_index(idx), start_metrics(start), end_metrics(end) {
    }
};