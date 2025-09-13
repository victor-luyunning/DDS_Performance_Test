// Throughput_ZeroCopyBytes.cpp
#include "Throughput_ZeroCopyBytes.h"

#include "ZRDDSDataWriter.h"
#include "ZRDDSDataReader.h"
#include "ZRBuiltinTypes.h"
#include "ZRBuiltinTypesTypeSupport.h"

#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace DDS;

// ========================
// �ڲ��ࣺWriterListener (ר���� ZeroCopy)
// ========================

class Throughput_ZeroCopyBytes::WriterListener : public virtual DDS::DataWriterListener {
public:
    WriterListener(std::atomic<bool>& flag, std::mutex& mtx, std::condition_variable& cv)
        : reconnected_flag_(flag), mutex_(mtx), cond_var_(cv), last_current_count_(0) {
    }

    void on_liveliness_lost(DDS::DataWriter*, const DDS::LivelinessLostStatus&) override {}
    void on_offered_deadline_missed(DDS::DataWriter*, const DDS::OfferedDeadlineMissedStatus&) override {}
    void on_offered_incompatible_qos(DDS::DataWriter*, const DDS::OfferedIncompatibleQosStatus&) override {}
    void on_publication_matched(DDS::DataWriter*, const DDS::PublicationMatchedStatus&) override {}

    /*void on_subscription_matched(DDS::DataWriter*, const DDS::SubscriptionMatchedStatus& status) override {
        int32_t current = status.current_count;
        int32_t previous = last_current_count_.load();

        last_current_count_.store(current);

        if (current > 0 && previous == 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            reconnected_flag_.store(true);
            cond_var_.notify_all();
            Logger::getInstance().logAndPrint("Throughput_ZeroCopy: ��⵽��������������");
        }
    }*/

private:
    std::atomic<bool>& reconnected_flag_;
    std::mutex& mutex_;
    std::condition_variable& cond_var_;
    std::atomic<int32_t> last_current_count_;
};

// ========================
// ���캯�� & ����
// ========================

Throughput_ZeroCopyBytes::Throughput_ZeroCopyBytes(DDSManager_ZeroCopyBytes& ddsManager, ResultCallback callback)
    : ddsManager_(ddsManager)
    , result_callback_(std::move(callback))
    , subscriber_reconnected_(false)
{
    writer_listener_ = std::make_unique<WriterListener>(
        subscriber_reconnected_,
        reconnect_mtx_,
        reconnect_cv_
    );

    DataWriter* writer = ddsManager_.get_data_writer();
    if (writer) {
        ReturnCode_t ret = writer->set_listener(writer_listener_.get(), DDS::SUBSCRIPTION_MATCHED_STATUS);
        if (ret != DDS::RETCODE_OK) {
            Logger::getInstance().logAndPrint("���棺�޷�Ϊ DataWriter ���ü�����");
        }
    }
}

Throughput_ZeroCopyBytes::~Throughput_ZeroCopyBytes() = default;

// ========================
// ͬ���ȴ�����
// ========================

bool Throughput_ZeroCopyBytes::waitForSubscriberReconnect(const std::chrono::seconds& timeout) {
    std::unique_lock<std::mutex> lock(reconnect_mtx_);
    subscriber_reconnected_ = false;
    return reconnect_cv_.wait_for(lock, timeout, [this] { return subscriber_reconnected_.load(); });
}

void Throughput_ZeroCopyBytes::waitForRoundEnd() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return roundFinished_.load(); });
}

bool Throughput_ZeroCopyBytes::waitForWriterMatch() {
    auto writer = ddsManager_.get_data_writer();
    if (!writer) return false;

    while (true) {
        PublicationMatchedStatus status{};
        ReturnCode_t ret = writer->get_publication_matched_status(status);
        if (ret == RETCODE_OK) {
            Logger::getInstance().logAndPrint(
                "Writer wait match(" + std::to_string(status.current_count) + "/1)"
            );
            if (status.current_count > 0) return true;
        }
        else {
            Logger::getInstance().logAndPrint("Error: Failed to get publication matched status.");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool Throughput_ZeroCopyBytes::waitForReaderMatch() {
    auto reader = ddsManager_.get_data_reader();
    if (!reader) return false;

    while (true) {
        SubscriptionMatchedStatus status{};
        ReturnCode_t ret = reader->get_subscription_matched_status(status);
        if (ret == RETCODE_OK) {
            Logger::getInstance().logAndPrint(
                "Reader wait match(" + std::to_string(status.current_count) + "/1)"
            );
            if (status.current_count > 0) return true;
        }
        else {
            Logger::getInstance().logAndPrint("Error: Failed to get subscription matched status.");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ========================
// runPublisher - �����߼����㿽��ר�ã�
// ========================

int Throughput_ZeroCopyBytes::runPublisher(const ConfigData& config) {
    using WriterType = DDS::ZRDDSDataWriter<DDS::ZeroCopyBytes>;
    WriterType* writer = dynamic_cast<WriterType*>(ddsManager_.get_data_writer());
    if (!writer) {
        Logger::getInstance().logAndPrint("Throughput_ZeroCopyBytes: DataWriter Ϊ�գ��޷�����");
        return -1;
    }

    const int round_index = config.m_activeLoop;
    const int dataSize = config.m_minSize[round_index];  // ����ʹ�� minSize ��Ϊ�̶���С��ZeroCopy ͨ���̶���������
    const int sendCount = config.m_sendCount[round_index];

    if (!waitForWriterMatch()) {
        Logger::getInstance().logAndPrint("Throughput_ZeroCopyBytes: �ȴ� Subscriber ƥ�䳬ʱ");
        return -1;
    }

    std::ostringstream oss;
    oss << "�� " << (round_index + 1) << " �����²��� | ����: " << sendCount
        << " �� | ���ݴ�С: " << dataSize << " �ֽ� (ZeroCopy)";
    Logger::getInstance().logAndPrint(oss.str());

    auto& resUtil = ResourceUtilization::instance();
    resUtil.initialize();
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();

    DDS::ZeroCopyBytes sample;
    if (!ddsManager_.prepareZeroCopyData(sample, dataSize)) {
        Logger::getInstance().logAndPrint("Throughput_ZeroCopyBytes: ׼�� ZeroCopy ��������ʧ��");
        return -1;
    }

    // === ���������� ===
    for (int j = 0; j < sendCount; ++j) {
        writer->write(sample, DDS_HANDLE_NIL_NATIVE);
    }

    // �ȴ�ȷ��
    writer->wait_for_acknowledgments({ 10, 0 });

    // === ���ͽ���������Ǳ��ֽ�����===
    if (sample.userLength > 0) {
        sample.userBuffer[0] = 255;  // ������־
    }
    for (int k = 0; k < 3; ++k) {
        writer->write(sample, DDS_HANDLE_NIL_NATIVE);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // === �ռ��������� ===
    SysMetrics end_metrics = resUtil.collectCurrentMetrics();
    if (result_callback_) {
        result_callback_(TestRoundResult{ round_index + 1, start_metrics, end_metrics });
    }

    Logger::getInstance().logAndPrint("�� " + std::to_string(round_index + 1) + " �ַ������ (ZeroCopy)");
    return 0;
}

// ========================
// runSubscriber - �����߼����㿽��ר�ã�
// ========================

int Throughput_ZeroCopyBytes::runSubscriber(const ConfigData& config) {
    using ReaderType = DDS::ZRDDSDataReader<DDS::ZeroCopyBytes, DDS::ZeroCopyBytesSeq>;
    ReaderType* reader = dynamic_cast<ReaderType*>(ddsManager_.get_data_reader());
    if (!reader) {
        Logger::getInstance().logAndPrint("Throughput_ZeroCopyBytes: DataReader Ϊ�գ��޷�����");
        return -1;
    }

    const int round_index = config.m_activeLoop;
    const int expected = config.m_sendCount[round_index];

    if (!waitForReaderMatch()) {
        Logger::getInstance().logAndPrint("Throughput_ZeroCopyBytes: �ȴ� Publisher ƥ�䳬ʱ");
        return -1;
    }

    Logger::getInstance().logAndPrint("�� " + std::to_string(round_index + 1) + " �ֽ��տ�ʼ (ZeroCopy)");

    auto& resUtil = ResourceUtilization::instance();
    resUtil.initialize();
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();

    receivedCount_.store(0);
    roundFinished_.store(false);

    waitForRoundEnd(); // ����ֱ���յ�������

    SysMetrics end_metrics = resUtil.collectCurrentMetrics();
    if (result_callback_) {
        result_callback_(TestRoundResult{ round_index + 1, start_metrics, end_metrics });
    }

    int received = receivedCount_.load();
    int lost = expected - received;
    double lossRate = expected > 0 ? (double)lost / expected * 100.0 : 0.0;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6)
        << "���²��� (ZeroCopy) | �� " << (round_index + 1) << " �� | "
        << "����: " << received << " | "
        << "����: " << lost << " | "
        << "������: " << lossRate << "%";
    Logger::getInstance().logAndPrint(oss.str());

    return 0;
}

// ========================
// �ص�����ʵ��
// ========================

void Throughput_ZeroCopyBytes::onDataReceived(const DDS::ZeroCopyBytes& /*sample*/, const DDS::SampleInfo& info) {
    if (!info.valid_data) return;

    ++receivedCount_;
}

void Throughput_ZeroCopyBytes::onEndOfRound() {
    roundFinished_.store(true);
    cv_.notify_one();
}