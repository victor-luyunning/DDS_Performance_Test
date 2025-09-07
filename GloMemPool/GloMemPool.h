// GloMemPool.h
#pragma once
#include <cstddef>
#include <string>

#include "ZRMemPool.h"  

class GloMemPool {
public:
    // 初始化/清理
    static bool initialize();
    static void finalize();

    // 分配与释放（自动使用全局池）
    static void* allocate(size_t size, const char* file = nullptr, int line = 0);
    static void deallocate(void* ptr);

    // 统计功能（关键！用于性能测试）
    struct Stats {
        size_t total_allocated = 0;   // 当前已分配总量
        size_t peak_usage = 0;        // 峰值内存使用
        size_t alloc_count = 0;       // 分配次数
        size_t dealloc_count = 0;     // 释放次数
    };
    static Stats getStats();
    static void logStats();  // 打印日志

private:
    static ZRMemPool* s_pool;     // 全局池指针（由 ZR 初始化）
    static Stats s_stats;         // 统计数据

    // 禁止实例化
    GloMemPool() = delete;
}; 
