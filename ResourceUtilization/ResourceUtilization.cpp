// ResourceUtilization.cpp
#include "ResourceUtilization.h"
#include "GloMemPool.h" // 用于获取内存 stats
#include "Logger.h"     // 用于输出调试日志

// Windows 平台特定头文件
#ifdef _WIN32
#include <windows.h>
#include <sstream>      // 用于格式化错误信息
#include <chrono>       // 用于时间间隔控制
#include <thread>       // 用于后台采样线程
#include <atomic>       // 用于线程安全的峰值存储
#include <algorithm>    // 用于 std::max (虽然本次未直接使用)
#endif

// -----------------------------
// Implementation (Pimpl)
// -----------------------------

// 内部实现类，封装了具体的监控逻辑
class ResourceUtilization::Impl {
public:
    // 构造函数：初始化成员变量
    Impl() : is_initialized_(false), pid_(0), sampling_thread_(nullptr),
        stop_sampling_(false), current_cpu_peak_(-1.0) {
#ifdef _WIN32
        process_handle_ = NULL;
#endif
    }

    // 析构函数：确保资源被清理
    ~Impl() {
        shutdown_internal();
    }

    // 内部初始化函数
    bool initialize_internal() {
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::initialize_internal] Starting initialization (Peak GetProcessTimes method)...");

        // 如果已经初始化，直接返回成功
        if (is_initialized_) {
            Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::initialize_internal] Already initialized.");
            return true;
        }

#ifdef _WIN32
        // 获取当前进程 ID
        pid_ = GetCurrentProcessId();
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::initialize_internal] Current Process ID: " + std::to_string(pid_));

        // 获取当前进程句柄 (伪句柄，无需 CloseHandle)
        process_handle_ = GetCurrentProcess();
        if (process_handle_ == NULL) {
            Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::initialize_internal] Error: Failed to get current process handle.");
            return false;
        }
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::initialize_internal] Got process handle.");

        // 初始化时获取一次时间，检查 API 可用性
        FILETIME dummy_ft1, dummy_ft2, dummy_ft3, dummy_ft4;
        if (!GetSystemTimes(&dummy_ft1, &dummy_ft2, &dummy_ft3) ||
            !GetProcessTimes(process_handle_, &dummy_ft1, &dummy_ft2, &dummy_ft3, &dummy_ft4)) {
            Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::initialize_internal] Error: Initial GetSystemTimes or GetProcessTimes failed.");
            process_handle_ = NULL;
            return false;
        }

        // --- 启动后台采样线程 ---
        stop_sampling_ = false;
        current_cpu_peak_ = -1.0; // 重置峰值
        sampling_thread_ = new std::thread(&Impl::sampling_loop, this);
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::initialize_internal] Sampling thread started.");
        // --- 启动结束 ---

        is_initialized_ = true;
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::initialize_internal] Successfully initialized (Peak GetProcessTimes method).");
        return true;

#else
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::initialize_internal] Error: GetProcessTimes method not supported on non-Windows.");
        return false;
#endif
    }

    // 内部关闭函数
    void shutdown_internal() {
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::shutdown_internal] Shutting down...");

        // --- 停止并等待后台采样线程 ---
        if (sampling_thread_ && sampling_thread_->joinable()) {
            Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::shutdown_internal] Stopping sampling thread...");
            stop_sampling_ = true;
            sampling_thread_->join();
            delete sampling_thread_;
            sampling_thread_ = nullptr;
            Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::shutdown_internal] Sampling thread stopped and joined.");
        }
        // --- 停止结束 ---

        is_initialized_ = false;
        pid_ = 0;
        current_cpu_peak_ = -1.0; // 重置峰值
#ifdef _WIN32
        process_handle_ = NULL;
#endif
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::shutdown_internal] Shutdown complete.");
    }

    // --- 新增：后台采样循环 ---
    // 在独立线程中高频采样 CPU 使用率并更新峰值
    void sampling_loop() {
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::sampling_loop] Sampling thread started loop.");
        // 定义采样间隔 (例如，每 20ms 采样一次)
        const std::chrono::milliseconds sampling_interval(20);

        FILETIME prev_sys_idle, prev_sys_kernel, prev_sys_user;
        FILETIME prev_proc_creation, prev_proc_exit, prev_proc_kernel, prev_proc_user;

        // 获取初始时间点
        if (!GetSystemTimes(&prev_sys_idle, &prev_sys_kernel, &prev_sys_user) ||
            !GetProcessTimes(process_handle_, &prev_proc_creation, &prev_proc_exit, &prev_proc_kernel, &prev_proc_user)) {
            Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::sampling_loop] Error: Initial GetSystemTimes or GetProcessTimes failed in sampling loop.");
            return;
        }

        // 持续采样循环
        while (!stop_sampling_) {
            // 等待指定的采样间隔
            std::this_thread::sleep_for(sampling_interval);

            FILETIME sys_idle, sys_kernel, sys_user;
            FILETIME proc_creation, proc_exit, proc_kernel, proc_user;

            // 获取当前时间点的系统和进程时间
            if (!GetSystemTimes(&sys_idle, &sys_kernel, &sys_user) ||
                !GetProcessTimes(process_handle_, &proc_creation, &proc_exit, &proc_kernel, &proc_user)) {
                Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::sampling_loop] Error: GetSystemTimes or GetProcessTimes failed during sampling.");
                continue; // 跳过本次循环，继续下次尝试
            }

            // 将 FILETIME 转换为 64 位整数 (100-nanosecond intervals)
            ULARGE_INTEGER sys_kernel1, sys_user1, sys_kernel2, sys_user2;
            ULARGE_INTEGER proc_kernel1, proc_user1, proc_kernel2, proc_user2;

            sys_kernel1.LowPart = prev_sys_kernel.dwLowDateTime; sys_kernel1.HighPart = prev_sys_kernel.dwHighDateTime;
            sys_user1.LowPart = prev_sys_user.dwLowDateTime; sys_user1.HighPart = prev_sys_user.dwHighDateTime;
            sys_kernel2.LowPart = sys_kernel.dwLowDateTime; sys_kernel2.HighPart = sys_kernel.dwHighDateTime;
            sys_user2.LowPart = sys_user.dwLowDateTime; sys_user2.HighPart = sys_user.dwHighDateTime;

            proc_kernel1.LowPart = prev_proc_kernel.dwLowDateTime; proc_kernel1.HighPart = prev_proc_kernel.dwHighDateTime;
            proc_user1.LowPart = prev_proc_user.dwLowDateTime; proc_user1.HighPart = prev_proc_user.dwHighDateTime;
            proc_kernel2.LowPart = proc_kernel.dwLowDateTime; proc_kernel2.HighPart = proc_kernel.dwHighDateTime;
            proc_user2.LowPart = proc_user.dwLowDateTime; proc_user2.HighPart = proc_user.dwHighDateTime;

            // 计算系统和进程的时间差
            ULONGLONG sys_kernel_delta = sys_kernel2.QuadPart - sys_kernel1.QuadPart;
            ULONGLONG sys_user_delta = sys_user2.QuadPart - sys_user1.QuadPart;
            ULONGLONG sys_total_delta = sys_kernel_delta + sys_user_delta;

            ULONGLONG proc_kernel_delta = proc_kernel2.QuadPart - proc_kernel1.QuadPart;
            ULONGLONG proc_user_delta = proc_user2.QuadPart - proc_user1.QuadPart;
            ULONGLONG proc_total_delta = proc_kernel_delta + proc_user_delta;

            // 计算 CPU 使用率百分比
            double cpu_usage = 0.0;
            if (sys_total_delta != 0) {
                cpu_usage = (static_cast<double>(proc_total_delta) / static_cast<double>(sys_total_delta)) * 100.0;
            }
            // 确保结果非负
            if (cpu_usage < 0.0) cpu_usage = 0.0;

            // --- 原子地更新峰值 ---
            // 使用 std::atomic<double> 的 compare_exchange_weak 来实现无锁更新
            double current_peak = current_cpu_peak_.load();
            while (cpu_usage > current_peak) {
                // 如果当前计算出的 cpu_usage 高于已记录的峰值 current_peak，
                // 则尝试用 cpu_usage 更新 current_cpu_peak_。
                // compare_exchange_weak 会自动处理并发情况：
                // - 如果 current_cpu_peak_ 的值仍然是 current_peak，则更新为 cpu_usage，并返回 true。
                // - 如果 current_cpu_peak_ 的值已经被其他线程修改（不再是 current_peak），则返回 false，
                //   并将 current_peak 更新为 current_cpu_peak_ 的最新值，然后循环继续尝试。
                if (current_cpu_peak_.compare_exchange_weak(current_peak, cpu_usage)) {
                    // 成功更新峰值
                    Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::sampling_loop] New peak CPU usage: " + std::to_string(cpu_usage) + "%");
                    break; // 跳出循环
                }
                // 如果 compare_exchange_weak 失败，current_peak 会被自动更新为最新的值，然后循环继续尝试
            }
            // --- 更新结束 ---

            // 更新 previous times 供下次迭代使用
            prev_sys_idle = sys_idle;
            prev_sys_kernel = sys_kernel;
            prev_sys_user = sys_user;
            prev_proc_creation = proc_creation;
            prev_proc_exit = proc_exit;
            prev_proc_kernel = proc_kernel;
            prev_proc_user = proc_user;

        } // while (!stop_sampling_)
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::sampling_loop] Sampling thread exiting loop.");
    }
    // --- 新增结束 ---

    // --- 修改：get_cpu_peak_since_last_call 现在只返回并重置峰值 ---
    // 这个函数由 collectCurrentMetrics 调用，获取并重置由后台线程维护的峰值
    double get_cpu_peak_since_last_call() {
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::get_cpu_peak_since_last_call] Called.");
        if (!is_initialized_) {
            Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::get_cpu_peak_since_last_call] Error: Not initialized.");
            return -1.0;
        }
        // 原子地加载当前峰值，并将其重置为 -1.0 (表示下一轮监控周期的开始)
        // exchange 操作是原子的：它返回旧值，并将新值存入 atomic 变量
        double peak = current_cpu_peak_.exchange(-1.0);
        Logger::getInstance().logAndPrint("[ResourceUtilization::Impl::get_cpu_peak_since_last_call] Returning peak: " + std::to_string(peak) + "% and resetting internal peak tracker.");
        return peak;
    }
    // --- 修改结束 ---


private:
    // --- 成员变量 ---
    bool is_initialized_;           // 是否已初始化
    mutable DWORD pid_;             // 当前进程 ID
#ifdef _WIN32
    mutable HANDLE process_handle_; // 当前进程句柄 (伪句柄)
#endif

    // 后台采样相关
    std::thread* sampling_thread_;  // 后台采样线程指针
    std::atomic<bool> stop_sampling_; // 停止采样的标志
    std::atomic<double> current_cpu_peak_; // 存储当前采样周期内的 CPU 使用率峰值
    // --- 成员变量结束 ---
};

// -----------------------------
// Public Interface (公共接口实现)
// -----------------------------

// 获取单例实例
ResourceUtilization& ResourceUtilization::instance() {
    static ResourceUtilization inst;
    return inst;
}

// 构造函数
ResourceUtilization::ResourceUtilization()
    : pimpl_(std::make_unique<Impl>()), is_initialized_(false) {
}

// 析构函数
ResourceUtilization::~ResourceUtilization() {
    shutdown(); // 确保在析构时清理资源
}

// 公共初始化接口
bool ResourceUtilization::initialize() {
    Logger::getInstance().logAndPrint("[ResourceUtilization::initialize] Requested initialization.");
    bool ok = pimpl_->initialize_internal();
    if (ok) {
        is_initialized_ = true;
        Logger::getInstance().logAndPrint("[ResourceUtilization::initialize] Initialization successful.");
    }
    else {
        is_initialized_ = false;
        Logger::getInstance().logAndPrint("[ResourceUtilization::initialize] Initialization failed.");
    }
    return ok;
}

// 公共关闭接口
void ResourceUtilization::shutdown() {
    Logger::getInstance().logAndPrint("[ResourceUtilization::shutdown] Requested shutdown.");
    if (pimpl_) {
        pimpl_->shutdown_internal();
    }
    is_initialized_ = false;
    Logger::getInstance().logAndPrint("[ResourceUtilization::shutdown] Shutdown process completed.");
}

// 【核心】采集当前系统指标
SysMetrics ResourceUtilization::collectCurrentMetrics() const {
    Logger::getInstance().logAndPrint("[ResourceUtilization::collectCurrentMetrics] Collecting metrics...");
    SysMetrics metrics{};

    // 初始化峰值为 -1.0 (无效值)
    metrics.cpu_usage_percent_peak = -1.0;

    // 初始化内存指标
    metrics.memory_peak_kb = 0;
    metrics.memory_current_kb = 0;
    metrics.memory_alloc_count = 0;
    metrics.memory_dealloc_count = 0;
    metrics.memory_current_blocks = 0;

    // 1. CPU 使用率峰值
    // --- 修改：调用新的峰值获取函数 ---
    double cpu_peak = pimpl_->get_cpu_peak_since_last_call();
    // --- 修改结束 ---
    if (cpu_peak >= 0.0) {
        metrics.cpu_usage_percent_peak = cpu_peak;
        Logger::getInstance().logAndPrint("[ResourceUtilization::collectCurrentMetrics] CPU usage peak (valid): " + std::to_string(cpu_peak));
    }
    else if (cpu_peak == -1.0) {
        // 这可能意味着初始化失败，或者在采样周期内没有获取到有效数据 (例如，进程非常空闲)
        metrics.cpu_usage_percent_peak = -1.0;
        Logger::getInstance().logAndPrint("[ResourceUtilization::collectCurrentMetrics] CPU usage peak collection returned -1.0 (no data or error).");
    }
    // 注意：由于后台线程持续运行，理论上不太可能返回 < -1.0 的值

    // 2. 内存统计来自 GloMemPool
    Logger::getInstance().logAndPrint("[ResourceUtilization::collectCurrentMetrics] Collecting memory stats from GloMemPool...");
    auto mem_stats = GloMemPool::getStats();
    metrics.memory_peak_kb = static_cast<unsigned long long>(mem_stats.peak_usage / 1024);
    metrics.memory_current_kb = static_cast<unsigned long long>(mem_stats.total_allocated / 1024);
    metrics.memory_alloc_count = mem_stats.alloc_count;
    metrics.memory_dealloc_count = mem_stats.dealloc_count;
    metrics.memory_current_blocks = mem_stats.current_blocks;
    Logger::getInstance().logAndPrint("[ResourceUtilization::collectCurrentMetrics] Memory stats collected.");

    Logger::getInstance().logAndPrint("[ResourceUtilization::collectCurrentMetrics] Metrics collection complete.");
    return metrics;
}