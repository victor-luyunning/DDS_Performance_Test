// Throughput.h
#pragma once
#include "runEntityInterface.h"
#include "DDSManager.h"
#include <atomic>
#include <mutex>
#include <condition_variable>

class Throughput : public runEntityInterface {
public:
    explicit Throughput(DDSManager& ddsManager);

    int runPublisher(const ConfigData& config) override;
    int runSubscriber(const ConfigData& config) override;

private:
    DDSManager& ddsManager_;

    std::atomic<int> receivedCount_{ 0 };
    std::atomic<bool> roundFinished_{ false };

    std::mutex mtx_;
    std::condition_variable cv_;

    bool waitForWriterMatch();
    bool waitForReaderMatch();
    void waitForRoundEnd();
}; 
