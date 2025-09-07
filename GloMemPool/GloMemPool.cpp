// GloMemPool.cpp
#include "GloMemPool.h"
#include "ZRMemPool.h"
#include "Logger.h"  

#include <sstream>

// 静态成员定义
ZRMemPool* GloMemPool::s_pool = nullptr;
GloMemPool::Stats GloMemPool::s_stats;

bool GloMemPool::initialize() {
    ZRInitialGlobalMemPool(); 

    s_pool = nullptr; // 允许传 nullptr 表示“全局默认池”

    if (!s_pool) {
        Logger::getInstance().logAndPrint("[Memory] 全局内存池初始化成功（使用默认池）");
    }
    else {
        Logger::getInstance().logAndPrint("[Memory] 全局内存池初始化成功");
    }

    return true;
}

void GloMemPool::finalize() {
    ZRFinalizeGlobalMemPool();
    s_pool = nullptr;
}

void* GloMemPool::allocate(size_t size, const char* file, int line) {
#ifdef _MEMORY_USE_TRACK_
    void* ptr = ZRMallocWCallInfo(s_pool, static_cast<DDS_ULong>(size), file, __FUNCTION__, line);
#else
    void* ptr = ZRMalloc(s_pool, static_cast<DDS_ULong>(size));
#endif

    if (ptr) {
        s_stats.total_allocated += size;
        s_stats.alloc_count++;
        if (s_stats.total_allocated > s_stats.peak_usage) {
            s_stats.peak_usage = s_stats.total_allocated;
        }
    }
    else {
        Logger::getInstance().logAndPrint(
            "[Memory] 分配失败: " + std::to_string(size) + " 字节"
        );
    }

    return ptr;
}

void GloMemPool::deallocate(void* ptr) {
    if (!ptr) return;
    ZRDealloc(s_pool, ptr);
    // 注意：这里无法知道释放了多少 bytes（除非 ZR 支持 tracking）
    s_stats.dealloc_count++;
    // s_stats.total_allocated -= ???  ← 无法精确减！
}

GloMemPool::Stats GloMemPool::getStats() {
    return s_stats;
}

void GloMemPool::logStats() {
    auto stats = getStats();
    std::ostringstream oss;
    oss << "[Memory Stats] "
        << "Peak: " << (stats.peak_usage / 1024.0) << " KB, "
        << "Allocs: " << stats.alloc_count << ", "
        << "Frees: " << stats.dealloc_count;
    Logger::getInstance().logAndPrint(oss.str());
}
