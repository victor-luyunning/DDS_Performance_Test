// ResourceUtilization.cpp
#include "ResourceUtilization.h"
#include "GloMemPool.h" // 仅用于获取内存 stats

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#pragma comment(lib, "pdh.lib")
#endif

// -----------------------------
// Implementation (Pimpl)
// -----------------------------

class ResourceUtilization::Impl {
public:
    Impl() : is_initialized_(false) {
#ifdef _WIN32
        query_ = nullptr;
        counter_ = nullptr;
#endif
    }

    ~Impl() {
        if (is_initialized_) {
            shutdown_internal();
        }
    }

    bool initialize_internal() {
        if (is_initialized_) return true;

#ifdef _WIN32
        PDH_STATUS status;

        status = PdhOpenQuery(nullptr, 0, &query_);
        if (status != ERROR_SUCCESS) return false;

        DWORD pid = GetCurrentProcessId();
        std::wstring path = L"\\Process(" + std::to_wstring(pid) + L")\\% Processor Time";

        status = PdhAddCounter(query_, path.c_str(), 0, &counter_);
        if (status != ERROR_SUCCESS) {
            PdhCloseQuery(query_);
            query_ = nullptr;
            return false;
        }

        PdhCollectQueryData(query_);
        Sleep(100); // 推荐等待
#endif

        is_initialized_ = true;
        return true;
    }

    void shutdown_internal() {
        if (!is_initialized_) return;

#ifdef _WIN32
        if (counter_) {
            PdhRemoveCounter(counter_);
            counter_ = nullptr;
        }
        if (query_) {
            PdhCloseQuery(query_);
            query_ = nullptr;
        }
#endif
        is_initialized_ = false;
    }

    double get_cpu_usage() const {
        if (!is_initialized_) return -1.0;

#ifdef _WIN32
        if (!counter_) return -1.0;

        PDH_FMT_COUNTERVALUE value;
        PdhCollectQueryData(query_);
        PDH_STATUS status = PdhGetFormattedCounterValue(counter_, PDH_FMT_DOUBLE, nullptr, &value);
        if (status != ERROR_SUCCESS) return -1.0;

        return value.doubleValue < 0 ? 0 : value.doubleValue;
#else
        return -1.0; // Not supported
#endif
    }

private:
    bool is_initialized_;
#ifdef _WIN32
    PDH_HQUERY query_;
    PDH_HCOUNTER counter_;
#endif
};

// -----------------------------
// Public Interface
// -----------------------------

ResourceUtilization& ResourceUtilization::instance() {
    static ResourceUtilization inst;
    return inst;
}

ResourceUtilization::ResourceUtilization()
    : pimpl_(std::make_unique<Impl>()) {
}

ResourceUtilization::~ResourceUtilization() {
    shutdown();
}

bool ResourceUtilization::initialize() {
    if (is_initialized_) return true;
    if (!pimpl_) return false;

    bool ok = pimpl_->initialize_internal();
    if (ok) is_initialized_ = true;
    return ok;
}

void ResourceUtilization::shutdown() {
    if (is_initialized_ && pimpl_) {
        pimpl_->shutdown_internal();
        is_initialized_ = false;
    }
}

SysMetrics ResourceUtilization::collectCurrentMetrics() const {
    SysMetrics metrics{};

    // 1. CPU 使用率
    double cpu = pimpl_->get_cpu_usage();
    metrics.cpu_usage_percent = (cpu >= 0.0) ? cpu : 0.0;

    // 2. 内存统计来自 GloMemPool
    auto mem_stats = GloMemPool::getStats();
    metrics.memory_peak_kb = mem_stats.peak_usage / 1024;
    metrics.memory_current_kb = mem_stats.total_allocated / 1024;
    metrics.memory_alloc_count = mem_stats.alloc_count;
    metrics.memory_dealloc_count = mem_stats.dealloc_count;
    metrics.memory_current_blocks = mem_stats.current_blocks;

    return metrics;
}

