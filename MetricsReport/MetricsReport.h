// MetricsReport.h
#pragma once
#include "TestRoundResult.h"
#include <vector>
#include <mutex>

class MetricsReport {
public:
    void addResult(const TestRoundResult& result);

    // ��ѡ�����ɻ��ܱ���
    void generateSummary() const;

private:
    std::vector<TestRoundResult> results_;
    mutable std::mutex mtx_;
};