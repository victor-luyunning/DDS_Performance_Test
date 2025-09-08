// GloMemPool.h
#pragma once
#include <cstddef>
#include <string>

#include "ZRMemPool.h"

// 可选：定义这个宏来开启 new/delete 重载（建议在性能测试构建中开启）
// #define ENABLE_GLOBAL_NEW_DELETE

class GloMemPool {
public:
    // 初始化/清理
    static bool initialize();
    static void finalize();

    // 原始分配接口（供底层使用）
    static void* allocate(size_t size, const char* file = nullptr, int line = 0);
    static void deallocate(void* ptr);

    // C++ 安全构造接口（推荐上层使用）
    template<typename T, typename... Args>
    static T* new_object(Args&&... args);

    template<typename T>
    static void delete_object(T* ptr);

    // 统计功能
    struct Stats {
        size_t total_allocated = 0;   // 当前已分配总量
        size_t peak_usage = 0;        // 峰值内存使用
        size_t alloc_count = 0;       // 分配次数
        size_t dealloc_count = 0;     // 释放次数
    };
    static Stats getStats();
    static void logStats();

private:
    static ZRMemPool* s_pool;
    static Stats s_stats;

#ifdef _MEMORY_USE_TRACK_
    // 调试模式下维护分配大小映射（用于准确统计释放）
    static std::unordered_map<void*, size_t> s_alloc_map;
#endif

    // 禁止实例化
    GloMemPool() = delete;
};

// 全局 new/delete 重载（可选启用）
#ifdef ENABLE_GLOBAL_NEW_DELETE
void* operator new(size_t size);
void operator delete(void* ptr) noexcept;
void* operator new[](size_t size);
void operator delete[](void* ptr) noexcept;
#endif