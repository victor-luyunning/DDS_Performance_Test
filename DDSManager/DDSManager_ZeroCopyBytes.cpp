// DDSZeroCopyManager.cpp
#include "DDSManager_ZeroCopyBytes.h"
#include "Logger.h"
#include "GloMemPool.h"

#include "ZRDDSDataReader.h"
#include "ZRDDSTypeSupport.h"
#include "ZRBuiltinTypesTypeSupport.h"
#include "ZRDDSDataWriter.h"

#include <iostream>
#include <sstream>
#include <random>

// �ڲ� Listener �� - ʹ�� ZeroCopyBytes ����
class DDSManager_ZeroCopyBytes::MyDataReaderListener
    : public virtual DDS::SimpleDataReaderListener<
    DDS_ZeroCopyBytes,
    DDS_ZeroCopyBytesSeq,
    DDS::ZRDDSDataReader<DDS_ZeroCopyBytes, DDS_ZeroCopyBytesSeq>
    >
{
public:
    MyDataReaderListener(
        OnDataReceivedCallback_ZC dataCb,
        OnEndOfRoundCallback endCb
    ) : onDataReceived_(std::move(dataCb)), onEndOfRound_(std::move(endCb)) {
    }

    virtual void on_process_sample(
        DDS::DataReader*,
        const DDS_ZeroCopyBytes& sample,
        const DDS::SampleInfo& info
    ) override {
        if (!info.valid_data) return;

        // ����Ƿ�Ϊ�����������ֽ� 255��
        // ע�⣺userBuffer ָ��ʵ�����ݿ�ʼ��
        if (sample.userLength > 0 && static_cast<unsigned char>(sample.userBuffer[0]) == 255) {
            if (onEndOfRound_) {
                onEndOfRound_();
            }
            return;
        }

        // ��ͨ���ݰ�
        if (onDataReceived_) {
            onDataReceived_(sample, info);
        }
    }

private:
    OnDataReceivedCallback_ZC onDataReceived_;
    OnEndOfRoundCallback onEndOfRound_;
};

// ���캯��
DDSManager_ZeroCopyBytes::DDSManager_ZeroCopyBytes(const ConfigData& config, const std::string& xml_qos_file_path)
    : domain_id_(config.m_domainId)
    , topic_name_(config.m_topicName)
    , type_name_(config.m_typeName)
    , role_(config.m_isPositive ? "publisher" : "subscriber")
    , participant_factory_qos_name_(config.m_dpfQosName)
    , participant_qos_name_(config.m_dpQosName)
    , data_writer_qos_name_(config.m_writerQosName)
    , data_reader_qos_name_(config.m_readerQosName)
    , xml_qos_file_path_(xml_qos_file_path)
    , max_possible_size_(/* ����Ҫ�������л�ȡ������ݴ�С��������һ��Ĭ��ֵ */ 64 * 1024) // 64KB
    , global_buffer_(nullptr)
{
}

DDSManager_ZeroCopyBytes::~DDSManager_ZeroCopyBytes() {
    if (is_initialized_) {
        shutdown();
    }
}

bool DDSManager_ZeroCopyBytes::initialize(
    OnDataReceivedCallback_ZC dataCallback,
    OnEndOfRoundCallback endCallback
) {
    std::cout << "[DDSManager_ZeroCopyBytes] Initializing DDS entities...\n";

    const char* qosFilePath = xml_qos_file_path_.c_str();
    const char* p_lib_name = "default_lib";
    const char* p_prof_name = "default_profile";
    const char* pf_qos_name = participant_factory_qos_name_.empty() ? nullptr : participant_factory_qos_name_.c_str();
    const char* p_qos_name = participant_qos_name_.empty() ? nullptr : participant_qos_name_.c_str();

    // ��ȡ����
    factory_ = DDS::DomainParticipantFactory::get_instance_w_profile(
        qosFilePath, p_lib_name, p_prof_name, pf_qos_name);
    if (!factory_) {
        std::cerr << "[DDSManager_ZeroCopyBytes] Failed to get DomainParticipantFactory.\n";
        return false;
    }

    // ���� Participant
    participant_ = factory_->create_participant_with_qos_profile(
        domain_id_, p_lib_name, p_prof_name, p_qos_name, nullptr, DDS::STATUS_MASK_NONE);
    if (!participant_) {
        std::cerr << "[DDSManager_ZeroCopyBytes] Failed to create DomainParticipant.\n";
        return false;
    }

    // ע������ - ʹ�� ZeroCopyBytes TypeSupport
    DDS::ZeroCopyBytesTypeSupport* type_support = DDS::ZeroCopyBytesTypeSupport::get_instance();
    if (!type_support) {
        std::cerr << "[DDSManager_ZeroCopyBytes] Failed to get ZeroCopyBytesTypeSupport instance.\n";
        return false;
    }

    const char* registered_type_name = type_support->get_type_name();
    if (!registered_type_name || strlen(registered_type_name) == 0) {
        std::cerr << "[DDSManager_ZeroCopyBytes] Type name is null or empty.\n";
        return false;
    }

    if (type_support->register_type(participant_, registered_type_name) != DDS::RETCODE_OK) {
        std::cerr << "[DDSManager_ZeroCopyBytes] Failed to register type '" << registered_type_name << "'.\n";
        return false;
    }

    // ���� Topic
    topic_ = participant_->create_topic(
        topic_name_.c_str(), registered_type_name,
        DDS::TOPIC_QOS_DEFAULT, nullptr, DDS::STATUS_MASK_NONE);
    if (!topic_) {
        std::cerr << "[DDSManager_ZeroCopyBytes] Failed to create Topic '" << topic_name_ << "'.\n";
        return false;
    }

    // ========== �㿽���ؼ����裺Ԥ����ȫ�ֻ����� ==========
    // ������������������������ڸ��ã�����Ƶ������
    size_t totalBufferSize = max_possible_size_ + DEFAULT_HEADER_RESERVE;
    global_buffer_ = static_cast<char*>(GloMemPool::allocate(totalBufferSize, __FILE__, __LINE__));
    if (!global_buffer_) {
        std::cerr << "[DDSManager_ZeroCopyBytes] Failed to allocate global buffer for zero-copy.\n";
        return false;
    }
    std::cout << "[DDSManager_ZeroCopyBytes] Allocated global zero-copy buffer of size: " << totalBufferSize << " bytes\n";

    // ���� Writer �� Reader
    if (role_ == "publisher") {
        data_writer_ = participant_->create_datawriter_with_topic_and_qos_profile(
            topic_->get_name(), type_support,
            "default_lib", "default_profile", data_writer_qos_name_.c_str(),
            nullptr, DDS::STATUS_MASK_NONE);
        if (!data_writer_) {
            GloMemPool::deallocate(global_buffer_);
            global_buffer_ = nullptr;
            std::cerr << "[DDSManager_ZeroCopyBytes] Failed to create DataWriter.\n";
            return false;
        }
        std::cout << "[DDSManager_ZeroCopyBytes] Created DataWriter.\n";
    }
    else if (role_ == "subscriber") {
        void* mem = GloMemPool::allocate(sizeof(MyDataReaderListener), __FILE__, __LINE__);
        if (!mem) {
            GloMemPool::deallocate(global_buffer_);
            global_buffer_ = nullptr;
            std::cerr << "[DDSManager_ZeroCopyBytes] Memory allocation failed for listener.\n";
            return false;
        }
        listener_ = new (mem) MyDataReaderListener(std::move(dataCallback), std::move(endCallback));

        data_reader_ = participant_->create_datareader_with_topic_and_qos_profile(
            topic_->get_name(), type_support,
            "default_lib", "default_profile", data_reader_qos_name_.c_str(),
            listener_, DDS::DATA_AVAILABLE_STATUS);
        if (!data_reader_) {
            listener_->~MyDataReaderListener();
            GloMemPool::deallocate(listener_);
            listener_ = nullptr;
            GloMemPool::deallocate(global_buffer_);
            global_buffer_ = nullptr;
            std::cerr << "[DDSManager_ZeroCopyBytes] Failed to create DataReader.\n";
            return false;
        }
        std::cout << "[DDSManager_ZeroCopyBytes] Created DataReader with listener.\n";
    }
    else {
        std::cerr << "[DDSManager_ZeroCopyBytes] Invalid role: " << role_ << "\n";
        GloMemPool::deallocate(global_buffer_);
        global_buffer_ = nullptr;
        return false;
    }

    is_initialized_ = true;
    std::cout << "[DDSManager_ZeroCopyBytes] Initialization successful.\n";
    return true;
}

void DDSManager_ZeroCopyBytes::shutdown() {
    if (!factory_) return;

    if (listener_) {
        listener_->~MyDataReaderListener();
        GloMemPool::deallocate(listener_);
        listener_ = nullptr;
    }

    // ========== �ͷ�Ԥ�����ȫ�ֻ����� ==========
    if (global_buffer_) {
        GloMemPool::deallocate(global_buffer_);
        global_buffer_ = nullptr;
    }

    if (participant_) {
        participant_->delete_contained_entities();
        factory_->delete_participant(participant_);
        participant_ = nullptr;
        topic_ = nullptr;
        data_writer_ = nullptr;
        data_reader_ = nullptr;
    }

    is_initialized_ = false;
    std::cout << "[DDSManager_ZeroCopyBytes] Shutdown completed.\n";
}

// ׼���������� (ZeroCopy �汾)
// ע�⣺����������ٸ�������ڴ棬���Ǹ���Ԥ����� global_buffer_
// ��ֻ�������ýṹ���ֶκ�����û�����
bool DDSManager_ZeroCopyBytes::prepareZeroCopyData(DDS_ZeroCopyBytes& sample, int dataSize) {
    if (!global_buffer_) {
        std::cerr << "[DDSManager_ZeroCopyBytes] Global buffer not allocated. Call initialize first!\n";
        return false;
    }

    if (static_cast<size_t>(dataSize) > max_possible_size_) {
        std::cerr << "[DDSManager_ZeroCopyBytes] Data size (" << dataSize
            << ") exceeds maximum possible size (" << max_possible_size_ << ").\n";
        return false;
    }

    // 1. ���ýṹ���Ա
    sample.totalLength = max_possible_size_ + DEFAULT_HEADER_RESERVE;
    sample.reservedLength = DEFAULT_HEADER_RESERVE;
    sample.value = global_buffer_;                          // ������������ʼ
    sample.userBuffer = global_buffer_ + DEFAULT_HEADER_RESERVE; // �û�������ʼ
    sample.userLength = dataSize;                           // <<< �ؼ�������Ҫ���͵�ʵ�ʳ���

    // 2. ����û�����
    for (int i = 0; i < dataSize; ++i) {
        sample.userBuffer[i] = static_cast<DDS::Octet>(i % 256);
    }

    Logger::getInstance().logAndPrint(
        "prepareZeroCopyData: userLength=" + std::to_string(sample.userLength) +
        " reservedLength=" + std::to_string(sample.reservedLength) +
        " totalLength=" + std::to_string(sample.totalLength)
    );

    return true;
}