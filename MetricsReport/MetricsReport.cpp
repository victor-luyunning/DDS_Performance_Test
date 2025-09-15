// MetricsReport.cpp
#include "MetricsReport.h"
#include "Logger.h"
#include <numeric>
#include <sstream>
#include <iomanip>
// --- 新增包含：为了使用 std::max_element 计算峰值 ---
#include <algorithm> 
// --- 新增包含结束 ---

void MetricsReport::addResult(const TestRoundResult& result) {
    std::lock_guard<std::mutex> lock(mtx_);

    // --- 修改：处理 CPU 历史记录以获得一致的峰值 ---
    TestRoundResult processed_result = result; // 复制一份用于处理

    // 从历史记录中计算最终的 CPU 峰值
    if (!result.cpu_usage_history.empty()) {
        // 使用 std::max_element 找到历史记录中的最大值
        auto max_iter = std::max_element(result.cpu_usage_history.begin(), result.cpu_usage_history.end());
        if (max_iter != result.cpu_usage_history.end()) {
            float final_peak = *max_iter;
            // 将计算出的最终峰值存入 processed_result 的 end_metrics 中
            processed_result.end_metrics.cpu_usage_percent_peak = static_cast<double>(final_peak);
            Logger::getInstance().logAndPrint(
                "[MetricsReport::addResult] Calculated final consistent CPU peak for round " +
                std::to_string(result.round_index) + ": " + std::to_string(final_peak) + "%"
            );
        }
        else {
            // 理论上不会发生，因为 vector 不空
            Logger::getInstance().logAndPrint(
                "[MetricsReport::addResult] Warning: cpu_usage_history was not empty but max_element failed for round " +
                std::to_string(result.round_index)
            );
        }
    }
    else {
        Logger::getInstance().logAndPrint(
            "[MetricsReport::addResult] Note: No CPU usage history recorded for round " +
            std::to_string(result.round_index)
        );
        // processed_result.end_metrics.cpu_usage_percent_peak 保持原值 (-1.0 或其他)
    }
    // --- 修改结束 ---

    // 将处理后的结果（包含最终峰值）存入 results_ vector
    results_.push_back(std::move(processed_result));

    // --- 修改：在添加结果时立即输出本轮的最终 CPU 峰值 ---
    // 注意：这里使用的是处理后的 processed_result
    const auto& end_metrics = processed_result.end_metrics; // 使用处理后的指标

    std::ostringstream cpu_oss;
    cpu_oss << std::fixed << std::setprecision(2)
        << "[实时 CPU] 第 " << processed_result.round_index << " 轮 | "
        // 使用处理后计算出的最终一致的 CPU 峰值
        << "CPU峰值: " << end_metrics.cpu_usage_percent_peak << "%";

    // 将实时 CPU 信息打印到日志和控制台
    Logger::getInstance().logAndPrint(cpu_oss.str());
    // --- 修改结束 ---
}

void MetricsReport::generateSummary() const {
    std::lock_guard<std::mutex> lock(mtx_);

    if (results_.empty()) {
        Logger::getInstance().logAndPrint("[Metrics] 无资源监控数据可汇总");
        return;
    }

    Logger::getInstance().logAndPrint("\n=== 系统资源使用汇总报告 ===");

    // 遍历处理后的 results_
    for (const auto& r : results_) {
        const auto& start = r.start_metrics;
        const auto& end = r.end_metrics; // end_metrics 现在包含最终一致的峰值

        // --- 修正内存分配/释放块数计算 ---
        // 更准确地计算净分配块数变化：
        long long net_current_blocks_delta = static_cast<long long>(end.memory_current_blocks) - static_cast<long long>(start.memory_current_blocks);
        // --- 修正结束 ---


        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2)
            << "第 " << r.round_index << " 轮资源变化 | "
            // 汇总报告中显示处理后计算出的最终一致的 CPU 峰值
            << "CPU峰值: " << end.cpu_usage_percent_peak << "% | "
            << "内存增量: " << (end.memory_current_kb - start.memory_current_kb) << " KB | "
            << "峰值内存: " << end.memory_peak_kb << " KB | "
            // 显示基于 current_blocks 的净变化
            << "净分配块数 (当前块): " << net_current_blocks_delta;
        Logger::getInstance().logAndPrint(oss.str());

    }
}