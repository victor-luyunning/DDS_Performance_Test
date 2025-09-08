// Logger.cpp
#include "Logger.h"
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

class Logger::Impl {
public:
    std::ofstream file_;
    std::string log_directory_;
    std::string log_file_prefix_ = "log_";
    std::string log_file_suffix_ = ".log";
    bool isInitialized_ = false;
    mutable std::atomic<int> file_counter_{ 0 };

    // 日志队列
    std::queue<std::string> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{ false };
    std::thread writer_thread_;

    struct LogTimestamp {
        int year, month, day, hour, minute, second, millisecond;

        std::string formatTime() const {
            std::ostringstream oss;
            oss << year << '-'
                << std::setfill('0') << std::setw(2) << month << '-'
                << std::setfill('0') << std::setw(2) << day << ' '
                << std::setfill('0') << std::setw(2) << hour << ':'
                << std::setfill('0') << std::setw(2) << minute << ':'
                << std::setfill('0') << std::setw(2) << second;
            return oss.str();
        }

        std::string formatTimeWithMs() const {
            std::ostringstream oss;
            oss << year << '-'
                << std::setfill('0') << std::setw(2) << month << '-'
                << std::setfill('0') << std::setw(2) << day << ' '
                << std::setfill('0') << std::setw(2) << hour << ':'
                << std::setfill('0') << std::setw(2) << minute << ':'
                << std::setfill('0') << std::setw(2) << second
                << '.' << std::setfill('0') << std::setw(3) << millisecond;
            return oss.str();
        }

        std::string formatFilename() const {
            std::ostringstream oss;
            oss << year
                << std::setfill('0') << std::setw(2) << month
                << std::setfill('0') << std::setw(2) << day
                << "_"
                << std::setfill('0') << std::setw(2) << hour
                << std::setfill('0') << std::setw(2) << minute
                << std::setfill('0') << std::setw(2) << second
                << "_" << std::setfill('0') << std::setw(3) << millisecond;
            return oss.str();
        }
    };

    LogTimestamp getLogTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        auto time_t = std::chrono::system_clock::to_time_t(now);

        tm tm;
#ifdef _WIN32
        localtime_s(&tm, &time_t);
#else
        localtime_r(&time_t, &tm);
#endif

        auto ms_count = ms.time_since_epoch().count();
        auto sec_count = std::chrono::duration_cast<std::chrono::seconds>(ms.time_since_epoch()).count();
        int millisecond = static_cast<int>(ms_count - sec_count * 1000);

        return LogTimestamp{
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            millisecond
        };
    }

    std::string getCurrentTimeStr() const {
        return getLogTimestamp().formatTimeWithMs();
    }

    std::string generateLogFileName() const {
        auto ts = getLogTimestamp();
        int seq = file_counter_.fetch_add(1);
        std::ostringstream oss;
        oss << log_file_prefix_
            << ts.formatFilename()
            << "_" << std::setfill('0') << std::setw(2) << seq
            << log_file_suffix_;
        return (std::filesystem::path(log_directory_) / oss.str()).string();
    }

    // 后台写入线程函数
    void backgroundWrite() {
        while (true) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return !log_queue_.empty() || stop_.load(); });

            if (stop_.load() && log_queue_.empty()) {
                break;
            }

            std::string logLine = std::move(log_queue_.front());
            log_queue_.pop();
            lock.unlock();

            if (isInitialized_ && file_.is_open()) {
                file_ << logLine << '\n';
            }
        }

        if (isInitialized_ && file_.is_open()) {
            file_ << "[" << getCurrentTimeStr() << "] === ZRDDS-Perf-Bench Log Finished ===\n";
            file_.flush();
            file_.close();
            isInitialized_ = false;
        }
    }

    // 入队日志
    void pushLog(const std::string& logLine) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_.load()) return;
            log_queue_.push(logLine);
        }
        cv_.notify_one();
    }

    // 初始化日志系统
    bool initialize(
        const std::string& logDirectory,
        const std::string& filePrefix,
        const std::string& fileSuffix)
    {
        if (isInitialized_) return true;

        log_directory_ = logDirectory;
        log_file_prefix_ = filePrefix;
        log_file_suffix_ = fileSuffix;

        if (!std::filesystem::exists(log_directory_)) {
            std::cout << "[Logger] 日志目录不存在，正在创建: " << log_directory_ << std::endl;
            if (!std::filesystem::create_directories(log_directory_)) {
                std::cerr << "[ERROR] 创建日志目录失败，请检查路径权限或磁盘状态: " << log_directory_ << std::endl;
                return false;
            }
            std::cout << "[Logger] 日志目录已创建: " << log_directory_ << std::endl;
        }

        std::string logFileName = generateLogFileName();
        file_.open(logFileName, std::ios::out | std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "[Error] 无法打开日志文件: " << logFileName << std::endl;
            return false;
        }

        std::string header = "[" + getCurrentTimeStr() + "] === ZRDDS-Perf-Bench Log Started ===\n"
            "Log File: " + logFileName + "\n"
            "----------------------------------------";
        pushLog(header);

        isInitialized_ = true;
        std::cout << "[Logger] 日志系统已启动\n";

        return true;
    }

    void close() {
        if (stop_.exchange(true)) return;
        cv_.notify_all();
    }

    // 构造时启动线程
    Impl() {
        writer_thread_ = std::thread(&Impl::backgroundWrite, this);
    }

    ~Impl() {
        close();
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
    }
};


Logger::Logger()
    : pImpl_(std::make_unique<Impl>())
{
   
}

Logger::~Logger() = default;  // 析构时自动 delete pImpl_

bool Logger::initialize(const std::string& logDirectory,
    const std::string& filePrefix,
    const std::string& fileSuffix)
{
    return pImpl_->initialize(logDirectory, filePrefix, fileSuffix);
}

void Logger::setupLogger(const std::string& logDirectory,
    const std::string& filePrefix,
    const std::string& fileSuffix)
{
    auto& logger = Logger::getInstance();
    bool success = logger.initialize(logDirectory, filePrefix, fileSuffix);
    if (!success) {
        std::cerr << "日志初始化失败，请检查目录权限或路径\n";
    }
}

void Logger::log(const std::string& msg) {
    if (pImpl_->isInitialized_) {
        std::string line = "[LOG] " + pImpl_->getCurrentTimeStr() + " " + msg;
        pImpl_->pushLog(line);
    }
}

void Logger::info(const std::string& msg) {
    if (pImpl_->isInitialized_) {
        std::string line = "[INFO] " + pImpl_->getCurrentTimeStr() + " " + msg;
        pImpl_->pushLog(line);
    }
}

void Logger::error(const std::string& msg) {
    std::string line = "[ERROR] " + pImpl_->getCurrentTimeStr() + " " + msg;
    if (!pImpl_->isInitialized_) {
        std::cerr << line << " [Warning] (日志未启用)\n";
        return;
    }
    pImpl_->pushLog(line);
}

void Logger::logConfig(const std::string& configInfo) {
    std::ostringstream oss;
    oss << "\n=== 测试配置 ===\n" << configInfo;
    if (pImpl_->isInitialized_) {
        pImpl_->pushLog(oss.str());
    }
    else {
        std::cerr << "[WARNING] 尝试写入配置日志，但日志未初始化\n" << configInfo << std::endl;
    }
}

void Logger::logResult(const std::string& result) {
    std::ostringstream oss;
    oss << "\n=== 测试结果 ===\n" << result;
    if (pImpl_->isInitialized_) {
        pImpl_->pushLog(oss.str());
    }
    else {
        std::cerr << "[WARNING] 尝试写入结果日志，但日志未初始化\n" << result << std::endl;
    }
}

void Logger::logAndPrint(const std::string& msg) {
    std::cout << msg << std::endl;
    if (pImpl_->isInitialized_) {
        std::string line = "[LOG] " + pImpl_->getCurrentTimeStr() + " " + msg;
        pImpl_->pushLog(line);
    }
}

void Logger::close() {
    pImpl_->close();
}