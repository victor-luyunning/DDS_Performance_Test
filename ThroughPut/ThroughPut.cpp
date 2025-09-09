// ThroughPut.cpp
#include "Throughput.h"
#include "Logger.h"
#include "TestDataDataWriter.h"
#include "TestDataDataReader.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

Throughput::Throughput(DDSManager& ddsManager)
    : ddsManager_(ddsManager)
{
}

bool Throughput::waitForWriterMatch() {
    auto writer = ddsManager_.get_data_writer();
    if (!writer) return false;

    int matched = 0;
    while (matched == 0) {
        DDS::PublicationMatchedStatus status;
        if (writer->get_publication_matched_status(status) == DDS::RETCODE_OK) {
            matched = status.current_count;
            if (matched > 0) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return true;
}

bool Throughput::waitForReaderMatch() {
    auto reader = ddsManager_.get_data_reader();
    if (!reader) return false;

    int matched = 0;
    while (matched == 0) {
        DDS::SubscriptionMatchedStatus status;
        if (reader->get_subscription_matched_status(status) == DDS::RETCODE_OK) {
            matched = status.current_count;
            if (matched > 0) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return true;
}

void Throughput::waitForRoundEnd() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return roundFinished_.load(); });
}

int Throughput::runPublisher(const ConfigData& config) {
    if (ddsManager_.get_data_writer() == nullptr) {
        Logger::getInstance().logAndPrint("Throughput: DataWriter is null.");
        return -1;
    }

    TestDataDataWriter* writer = dynamic_cast<TestDataDataWriter*>(ddsManager_.get_data_writer());
    if (!writer) {
        Logger::getInstance().logAndPrint("Throughput: Failed to cast to TestDataDataWriter.");
        return -1;
    }

    if (!waitForWriterMatch()) {
        Logger::getInstance().logAndPrint("ThroughputTester: No subscriber matched.");
        return -1;
    }

    int totalLoops = config.m_loopNum > 0 ? config.m_loopNum : 1;

    for (int i = 0; i < totalLoops; ++i) {
        int minSize = config.m_minSize[i];
        int maxSize = config.m_maxSize[i];
        int sendCount = config.m_sendCount[i];

        std::ostringstream oss;
        oss << "第 " << (i + 1) << " 轮吞吐测试 | 发送: " << sendCount
            << " 条 | 数据大小: [" << minSize << ", " << maxSize << "]";
        Logger::getInstance().logAndPrint(oss.str());

        TestData sample;
        if (!ddsManager_.prepareTestData(sample, minSize, maxSize)) {
            Logger::getInstance().logAndPrint("ThroughputTester: prepareTestData failed.");
            continue;
        }

        // 发送数据
        for (int j = 0; j < sendCount; ++j) {
            if (minSize != maxSize) {
                ddsManager_.cleanupTestData(sample);
                if (!ddsManager_.prepareTestData(sample, minSize, maxSize)) break;
            }
            writer->write(sample, DDS_HANDLE_NIL_NATIVE);
        }

        // 等待 ACK
        writer->wait_for_acknowledgments({ 10, 0 });

        // 发送结束包（3 次确保送达）
        ddsManager_.cleanupTestData(sample);
        if (ddsManager_.prepareTestData(sample, minSize, maxSize)) {
            if (sample.value._length > 0) {
                sample.value[0] = 255;  // 自动转为 Octet
            }
            for (int k = 0; k < 3; ++k) {
                writer->write(sample, DDS_HANDLE_NIL_NATIVE);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        ddsManager_.cleanupTestData(sample);

        Logger::getInstance().logAndPrint("第 " + std::to_string(i + 1) + " 轮发送完成");

        if (i < totalLoops - 1) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    return 0;
}

int Throughput::runSubscriber(const ConfigData& config) {
    if (ddsManager_.get_data_reader() == nullptr) {
        Logger::getInstance().logAndPrint("Throughput: DataReader is null.");
        return -1;
    }

    TestDataDataReader* reader = dynamic_cast<TestDataDataReader*>(ddsManager_.get_data_reader());
    if (!reader) {
        Logger::getInstance().logAndPrint("Throughput: Failed to cast to TestDataDataReader.");
        return -1;
    }

    if (!waitForReaderMatch()) {
        Logger::getInstance().logAndPrint("Throughput: No publisher matched.");
        return -1;
    }

    int totalLoops = config.m_loopNum > 0 ? config.m_loopNum : 1;

    for (int i = 0; i < totalLoops; ++i) {
        int expected = config.m_sendCount[i];
        receivedCount_.store(0);
        roundFinished_.store(false);

        Logger::getInstance().logAndPrint("第 " + std::to_string(i + 1) + " 轮接收开始");

        // 设置回调（在 initialize 时传入）
        auto onData = [this](const TestData&, const DDS::SampleInfo&) {
            receivedCount_.fetch_add(1);
            };
        auto onEnd = [this]() {
            roundFinished_.store(true);
            cv_.notify_all();
            };

        // 注意：这里假设在 initialize 时已传入这些回调
        // 否则需要提供 setCallbacks() 方法

        waitForRoundEnd();

        int received = receivedCount_.load();
        int lost = expected - received;
        double lossRate = expected > 0 ? (static_cast<double>(lost) / expected * 100.0) : 0.0;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6)
            << "吞吐测试 | 第 " << (i + 1) << " 轮 | "
            << "接收: " << received << " | "
            << "丢包: " << lost << " | "
            << "丢包率: " << lossRate << "%";
        Logger::getInstance().logAndPrint(oss.str());
    }

    return 0;
}