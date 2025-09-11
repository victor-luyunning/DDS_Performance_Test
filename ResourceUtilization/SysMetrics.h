// SysMetrics.h
#pragma once
#include <cstdint>

struct SysMetrics {
    double cpu_usage_percent = 0.0;           // 当前进程CPU使用率 (%)
    uint64_t memory_peak_kb = 0;              // 峰值内存 (KB)
    uint64_t memory_current_kb = 0;           // 当前内存 (KB)
    uint64_t memory_alloc_count = 0;          // 总分配次数
    uint64_t memory_dealloc_count = 0;        // 总释放次数
    uint64_t memory_current_blocks = 0;       // 当前活跃块数

    // 构造函数（可选）
    SysMetrics() = default;
};