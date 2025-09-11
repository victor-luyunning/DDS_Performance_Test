// MetricsReport.h
#pragma once
#include "TestRoundResult.h"
#include <vector>
#include <mutex>

class MetricsReport {
public:
    void addResult(const TestRoundResult& result);

    // 可选：生成汇总报告
    void generateSummary() const;

private:
    std::vector<TestRoundResult> results_;
    mutable std::mutex mtx_;
};