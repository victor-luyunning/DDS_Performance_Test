// TestRoundResult.h 
#pragma once
#include "SysMetrics.h"

#include <vector>

struct TestRoundResult {
    int round_index;              // 第几轮
    SysMetrics start_metrics;     // 开始时的资源状态
    SysMetrics end_metrics;       // 结束时的资源状态

    // 可选：中间采样点（用于绘制趋势图）
    std::vector<SysMetrics> samples;

    TestRoundResult(int idx, const SysMetrics& start, const SysMetrics& end)
        : round_index(idx), start_metrics(start), end_metrics(end) {
    }
};