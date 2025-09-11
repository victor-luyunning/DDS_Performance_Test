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
     * ִ�е��ַ���������
     * �˺���Ӧ����ѭ����ÿһ���е���һ�Ρ�
     */
    int runPublisher(const ConfigData& config);

    /**
     * ִ�е��ֶ���������
     * �˺���Ӧ����ѭ����ÿһ���е���һ�Ρ�
     */
    int runSubscriber(const ConfigData& config);

    /**
     * Publisher ר�ã��ȴ��������������ӡ�
     * @param timeout ���ȴ�ʱ��
     * @return true ����ڳ�ʱǰ��⵽����������
     */
    bool waitForSubscriberReconnect(const std::chrono::seconds& timeout);

private:
    // �ڲ��ࣺ���ڼ��� DataWriter ״̬�仯
    class WriterListener;

    // ��������
    DDSManager& ddsManager_;
    ResultCallback result_callback_;

    // Subscriber ͬ������
    std::atomic<int> receivedCount_{ 0 };
    std::atomic<bool> roundFinished_{ false };
    std::mutex mtx_;
    std::condition_variable cv_;

    // Publisher ͬ������
    std::atomic<bool> subscriber_reconnected_{ false };
    std::mutex reconnect_mtx_;
    std::condition_variable reconnect_cv_;

    // Listener ʵ��
    std::unique_ptr<WriterListener> writer_listener_;

    // ��������
    void waitForRoundEnd();
    bool waitForWriterMatch();
    bool waitForReaderMatch();
};
