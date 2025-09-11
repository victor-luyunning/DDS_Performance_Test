// MetricsReport.cpp
#include "MetricsReport.h"
#include "Logger.h"
#include <numeric>
#include <sstream>
#include <iomanip>

void MetricsReport::addResult(const TestRoundResult& result) {
    std::lock_guard<std::mutex> lock(mtx_);
    results_.push_back(result);
}

void MetricsReport::generateSummary() const {
    std::lock_guard<std::mutex> lock(mtx_);

    if (results_.empty()) {
        Logger::getInstance().logAndPrint("[Metrics] 无资源监控数据可汇总");
        return;
    }

    Logger::getInstance().logAndPrint("\n=== 系统资源使用汇总报告 ===");

    for (const auto& r : results_) {
        const auto& start = r.start_metrics;
        const auto& end = r.end_metrics;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2)
            << "第 " << r.round_index << " 轮资源变化 | "
            << "CPU峰值: " << end.cpu_usage_percent << "% | "
            << "内存增量: " << (end.memory_current_kb - start.memory_current_kb) << " KB | "
            << "峰值内存: " << end.memory_peak_kb << " KB | "
            << "净分配块数: " << (end.memory_alloc_count - end.memory_dealloc_count);
        Logger::getInstance().logAndPrint(oss.str());
    }
}