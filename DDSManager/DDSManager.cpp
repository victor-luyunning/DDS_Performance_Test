// DDSManager.cpp
#include "DDSManager.h"
#include "Logger.h"
#include "GloMemPool.h"
#include "TestDataDataReader.h"
#include "TestDataDataWriter.h"
#include "TestData.h"
#include "TestDataTypeSupport.h"

#include <iostream>
#include <sstream>
#include <random>

// 内部 Listener 类
class DDSManager::MyDataReaderListener
    : public virtual DDS::SimpleDataReaderListener<TestData, TestDataSeq, DDS::ZRDDSDataReader<TestData, TestDataSeq>>
{
public:
    MyDataReaderListener(
        OnDataReceivedCallback dataCb,
        OnEndOfRoundCallback endCb
    ) : onDataReceived_(std::move(dataCb)), onEndOfRound_(std::move(endCb)) {
    }

    virtual void on_process_sample(
        DDS::DataReader* /*reader*/,
        const TestData& sample,
        const DDS::SampleInfo& info
    ) override {
        if (!info.valid_data) return;

        // 检查是否为结束包（首字节 255）
        if (sample.value.length() > 0 && static_cast<unsigned char>(sample.value[0]) == 255) {
            if (onEndOfRound_) {
                onEndOfRound_();
            }
            return;
        }

        // 普通数据包
        if (onDataReceived_) {
            onDataReceived_(sample, info);
        }
    }

private:
    OnDataReceivedCallback onDataReceived_;
    OnEndOfRoundCallback onEndOfRound_;
};

// 构造函数
DDSManager::DDSManager(const ConfigData& config, const std::string& xml_qos_file_path)
    : domain_id_(config.m_domainId)
    , topic_name_(config.m_topicName)
    , type_name_(config.m_typeName)
    , role_(config.m_isPositive ? "publisher" : "subscriber")
    , participant_factory_qos_name_(config.m_dpfQosName)
    , participant_qos_name_(config.m_dpQosName)
    , data_writer_qos_name_(config.m_writerQosName)
    , data_reader_qos_name_(config.m_readerQosName)
    , xml_qos_file_path_(xml_qos_file_path)
{
}

DDSManager::~DDSManager() {
    if (is_initialized_) {
        shutdown();
    }
}

bool DDSManager::initialize(
    OnDataReceivedCallback dataCallback,
    OnEndOfRoundCallback endCallback
) {
    std::cout << "[DDSManager] Initializing DDS entities...\n";

    const char* qosFilePath = xml_qos_file_path_.c_str();
    const char* p_lib_name = "default_lib";
    const char* p_prof_name = "default_profile";
    const char* pf_qos_name = participant_factory_qos_name_.empty() ? nullptr : participant_factory_qos_name_.c_str();
    const char* p_qos_name = participant_qos_name_.empty() ? nullptr : participant_qos_name_.c_str();

    // 获取工厂
    factory_ = DDS::DomainParticipantFactory::get_instance_w_profile(
        qosFilePath, p_lib_name, p_prof_name, pf_qos_name);
    if (!factory_) {
        std::cerr << "[DDSManager] Failed to get DomainParticipantFactory.\n";
        return false;
    }

    // 创建 Participant
    participant_ = factory_->create_participant_with_qos_profile(
        domain_id_, p_lib_name, p_prof_name, p_qos_name, nullptr, DDS::STATUS_MASK_NONE);
    if (!participant_) {
        std::cerr << "[DDSManager] Failed to create DomainParticipant.\n";
        return false;
    }

    // 注册类型
    TestDataTypeSupport* type_support = TestDataTypeSupport::get_instance();
    const char* registered_type_name = type_support->get_type_name();
    if (!registered_type_name) {
        std::cerr << "[DDSManager] Type name is null.\n";
        return false;
    }

    if (type_support->register_type(participant_, registered_type_name) != DDS::RETCODE_OK) {
        std::cerr << "[DDSManager] Failed to register type '" << registered_type_name << "'.\n";
        return false;
    }

    // 创建 Topic
    topic_ = participant_->create_topic(
        topic_name_.c_str(), registered_type_name,
        DDS::TOPIC_QOS_DEFAULT, nullptr, DDS::STATUS_MASK_NONE);
    if (!topic_) {
        std::cerr << "[DDSManager] Failed to create Topic '" << topic_name_ << "'.\n";
        return false;
    }

    // 创建 Writer 或 Reader
    if (role_ == "publisher") {
        data_writer_ = participant_->create_datawriter_with_topic_and_qos_profile(
            topic_->get_name(), type_support,
            "default_lib", "default_profile", data_writer_qos_name_.c_str(),
            nullptr, DDS::STATUS_MASK_NONE);
        if (!data_writer_) {
            std::cerr << "[DDSManager] Failed to create DataWriter.\n";
            return false;
        }
        std::cout << "[DDSManager] Created DataWriter.\n";
    }
    else if (role_ == "subscriber") {
        void* mem = GloMemPool::allocate(sizeof(MyDataReaderListener), __FILE__, __LINE__);
        if (!mem) {
            std::cerr << "[DDSManager] Memory allocation failed for listener.\n";
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
            std::cerr << "[DDSManager] Failed to create DataReader.\n";
            return false;
        }
        std::cout << "[DDSManager] Created DataReader with listener.\n";
    }
    else {
        std::cerr << "[DDSManager] Invalid role: " << role_ << "\n";
        return false;
    }

    is_initialized_ = true;
    std::cout << "[DDSManager] Initialization successful.\n";
    return true;
}

void DDSManager::shutdown() {
    if (!factory_) return;

    if (listener_) {
        listener_->~MyDataReaderListener();
        GloMemPool::deallocate(listener_);
        listener_ = nullptr;
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
    std::cout << "[DDSManager] Shutdown completed.\n";
}

// 内部辅助：准备测试数据
bool DDSManager::prepareTestData(TestData& sample, int minSize, int maxSize) {
    int actualSize = minSize;
    if (minSize != maxSize) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(minSize, maxSize);
        actualSize = dis(gen);
    }

    if (!TestDataInitialize(&sample)) {
        return false;
    }

    DDS_ULong ul_size = static_cast<DDS_ULong>(actualSize);
    sample.value._contiguousBuffer = static_cast<DDS_Octet*>(
        GloMemPool::allocate(ul_size * sizeof(DDS_Octet), __FILE__, __LINE__)
        );

    if (!sample.value._contiguousBuffer) {
        TestDataFinalize(&sample);
        return false;
    }

    sample.value._length = ul_size;
    sample.value._maximum = ul_size;
    sample.value._owned = DDS_TRUE;
    sample.value._sequenceInit = DDS_INITIALIZE_NUMBER;

    for (DDS_ULong i = 0; i < ul_size; ++i) {
        sample.value._contiguousBuffer[i] = static_cast<DDS::Octet>(i % 256);
    }

    return true;
}

void DDSManager::cleanupTestData(TestData& sample) {
    TestDataFinalize(&sample);
}