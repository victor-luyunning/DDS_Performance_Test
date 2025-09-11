// Throughput.h
#pragma once

#include "DDSManager.h"

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

struct TestRoundResult;

namespace DDS {
    class DataWriter;
}

class Throughput {
public:
    using ResultCallback = std::function<void(const TestRoundResult&)>;

    explicit Throughput(DDSManager& ddsManager, ResultCallback callback = nullptr);
    ~Throughput();

    /**
     * 执行单轮发布者任务。
     * 此函数应由主循环在每一轮中调用一次。
     */
    int runPublisher(const ConfigData& config);

    /**
     * 执行单轮订阅者任务。
     * 此函数应由主循环在每一轮中调用一次。
     */
    int runSubscriber(const ConfigData& config);

    /**
     * Publisher 专用：等待订阅者重新连接。
     * @param timeout 最大等待时间
     * @return true 如果在超时前检测到订阅者上线
     */
    bool waitForSubscriberReconnect(const std::chrono::seconds& timeout);

private:
    // 内部类：用于监听 DataWriter 状态变化
    class WriterListener;

    // 核心依赖
    DDSManager& ddsManager_;
    ResultCallback result_callback_;

    // Subscriber 同步变量
    std::atomic<int> receivedCount_{ 0 };
    std::atomic<bool> roundFinished_{ false };
    std::mutex mtx_;
    std::condition_variable cv_;

    // Publisher 同步变量
    std::atomic<bool> subscriber_reconnected_{ false };
    std::mutex reconnect_mtx_;
    std::condition_variable reconnect_cv_;

    // Listener 实例
    std::unique_ptr<WriterListener> writer_listener_;

    // 辅助函数
    void waitForRoundEnd();
    bool waitForWriterMatch();
    bool waitForReaderMatch();
};
