// GloMemPool.cpp
#include "GloMemPool.h"
#include "Logger.h"

#include <sstream>
#include <unordered_map>

// 静态成员定义
ZRMemPool* GloMemPool::s_pool = nullptr;
GloMemPool::Stats GloMemPool::s_stats;

#ifdef _MEMORY_USE_TRACK_
std::unordered_map<void*, size_t> GloMemPool::s_alloc_map;
#endif

bool GloMemPool::initialize() {
    ZRInitialGlobalMemPool();
    s_pool = nullptr;  // 使用全局默认池

    Logger::getInstance().logAndPrint("[Memory] 全局内存池初始化成功（使用默认池）");
    return true;
}

void GloMemPool::finalize() {
    ZRFinalizeGlobalMemPool();
    s_pool = nullptr;

#ifdef _MEMORY_USE_TRACK_
    if (!s_alloc_map.empty()) {
        Logger::getInstance().logAndPrint(
            "[Memory] 警告：finalize 时仍有 " + std::to_string(s_alloc_map.size()) + " 个未释放的内存块"
        );
    }
#endif
}

void* GloMemPool::allocate(size_t size, const char* file, int line) {
    void* ptr = nullptr;

#ifdef _MEMORY_USE_TRACK_
    ptr = ZRMallocWCallInfo(s_pool, static_cast<DDS_ULong>(size), file, __FUNCTION__, line);
#else
    ptr = ZRMalloc(s_pool, static_cast<DDS_ULong>(size));
#endif

    if (ptr) {
        s_stats.total_allocated += size;
        s_stats.alloc_count++;
        if (s_stats.total_allocated > s_stats.peak_usage) {
            s_stats.peak_usage = s_stats.total_allocated;
        }

#ifdef _MEMORY_USE_TRACK_
        s_alloc_map[ptr] = size;
#endif
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

#ifdef _MEMORY_USE_TRACK_
    auto it = s_alloc_map.find(ptr);
    if (it != s_alloc_map.end()) {
        s_stats.total_allocated -= it->second;
        s_alloc_map.erase(it);
    }
#else
    // 无法知道 size，只能减少计数
    s_stats.dealloc_count++;
    // 注意：total_allocated 无法减，可能略高估
#endif

    s_stats.dealloc_count++;
}

GloMemPool::Stats GloMemPool::getStats() {
    return s_stats;
}

void GloMemPool::logStats() {
    auto stats = getStats();
    std::ostringstream oss;
    oss << "[Memory Stats] "
        << "Peak: " << (stats.peak_usage / 1024.0) << " KB, "
        << "Current: " << (stats.total_allocated / 1024.0) << " KB, "
        << "Allocs: " << stats.alloc_count << ", "
        << "Frees: " << stats.dealloc_count;
    Logger::getInstance().logAndPrint(oss.str());
}

// =======================
// C++ 安全构造接口实现
// =======================

template<typename T, typename... Args>
T* GloMemPool::new_object(Args&&... args) {
    void* mem = allocate(sizeof(T), __FILE__, __LINE__);
    if (!mem) return nullptr;
    return new (mem) T(std::forward<Args>(args)...);
}

template<typename T>
void GloMemPool::delete_object(T* ptr) {
    if (ptr) {
        ptr->~T();
        deallocate(ptr);
    }
}

// 显式实例化常用类型（避免头文件包含模板实现）
// 在 .cpp 中显式实例化，或直接放头文件中（看你项目风格）
// 示例：如果你常用 new_object<MyData>
// template class MyData* GloMemPool::new_object<MyData>();
// template void GloMemPool::delete_object<MyData>(MyData*);

// =======================
// 全局 new/delete 重载
// =======================

#ifdef ENABLE_GLOBAL_NEW_DELETE

void* operator new(size_t size) {
    void* ptr = GloMemPool::allocate(size, __FILE__, __LINE__);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr) noexcept {
    GloMemPool::deallocate(ptr);
}

void* operator new[](size_t size) {
    void* ptr = GloMemPool::allocate(size, __FILE__, __LINE__);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete[](void* ptr) noexcept {
    GloMemPool::deallocate(ptr);
}

#endif // ENABLE_GLOBAL_NEW_DELETE