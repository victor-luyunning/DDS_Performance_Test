// DDSManager_Bytes.cpp
#include "DDSManager_Bytes.h"
#include "Logger.h"
#include "GloMemPool.h"

#include "ZRDDSDataReader.h"
#include "ZRDDSTypeSupport.h"
#include "ZRBuiltinTypesTypeSupport.h"
#include "ZRDDSDataWriter.h"

#include <iostream>
#include <sstream>
#include <random>


// 内部 Listener 类 - 使用 Bytes 类型
class DDSManager_Bytes::MyDataReaderListener
    : public virtual DDS::SimpleDataReaderListener<DDS_Bytes, DDS_BytesSeq, DDS::ZRDDSDataReader<DDS_Bytes, DDS_BytesSeq>>
{
public:
    MyDataReaderListener(
        OnDataReceivedCallback_Bytes dataCb,
        OnEndOfRoundCallback endCb
    ) : onDataReceived_(std::move(dataCb)), onEndOfRound_(std::move(endCb)) {
    }

    virtual void on_process_sample(
        DDS::DataReader*,
        const DDS_Bytes& sample,
        const DDS::SampleInfo& info
    ) override {
        if (!info.valid_data) return;

        // 检查是否为结束包（首字节 255）
        if (sample.value._length > 0 && static_cast<unsigned char>(sample.value[0]) == 255) {
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
    OnDataReceivedCallback_Bytes onDataReceived_;
    OnEndOfRoundCallback onEndOfRound_;
};

// 构造函数
DDSManager_Bytes::DDSManager_Bytes(const ConfigData& config, const std::string& xml_qos_file_path)
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

DDSManager_Bytes::~DDSManager_Bytes() {
    if (is_initialized_) {
        shutdown();
    }
}

bool DDSManager_Bytes::initialize(
    OnDataReceivedCallback_Bytes dataCallback,
    OnEndOfRoundCallback endCallback
) {
    std::cout << "[DDSManager_Bytes] Initializing DDS entities...\n";

    const char* qosFilePath = xml_qos_file_path_.c_str();
    const char* p_lib_name = "default_lib";
    const char* p_prof_name = "default_profile";
    const char* pf_qos_name = participant_factory_qos_name_.empty() ? nullptr : participant_factory_qos_name_.c_str();
    const char* p_qos_name = participant_qos_name_.empty() ? nullptr : participant_qos_name_.c_str();

    // 获取工厂
    factory_ = DDS::DomainParticipantFactory::get_instance_w_profile(
        qosFilePath, p_lib_name, p_prof_name, pf_qos_name);
    if (!factory_) {
        std::cerr << "[DDSManager_Bytes] Failed to get DomainParticipantFactory.\n";
        return false;
    }

    // 创建 Participant
    participant_ = factory_->create_participant_with_qos_profile(
        domain_id_, p_lib_name, p_prof_name, p_qos_name, nullptr, DDS::STATUS_MASK_NONE);
    if (!participant_) {
        std::cerr << "[DDSManager_Bytes] Failed to create DomainParticipant.\n";
        return false;
    }

    // 注册类型
    DDS::BytesTypeSupport* type_support = DDS::BytesTypeSupport::get_instance();
    if (!type_support) {
        std::cerr << "[DDSManager_Bytes] Failed to get DDSInnerTypeSupport instance.\n";
        return false;
    }

    const char* registered_type_name = type_support->get_type_name();
    if (!registered_type_name || strlen(registered_type_name) == 0) {
        std::cerr << "[DDSManager_Bytes] Type name is null or empty.\n";
        return false;
    }

    if (type_support->register_type(participant_, registered_type_name) != DDS::RETCODE_OK) {
        std::cerr << "[DDSManager_Bytes] Failed to register type '" << registered_type_name << "'.\n";
        return false;
    }

    // 创建 Topic
    topic_ = participant_->create_topic(
        topic_name_.c_str(), registered_type_name,
        DDS::TOPIC_QOS_DEFAULT, nullptr, DDS::STATUS_MASK_NONE);
    if (!topic_) {
        std::cerr << "[DDSManager_Bytes] Failed to create Topic '" << topic_name_ << "'.\n";
        return false;
    }

    // 创建 Writer 或 Reader
    if (role_ == "publisher") {
        data_writer_ = participant_->create_datawriter_with_topic_and_qos_profile(
            topic_->get_name(), type_support,
            "default_lib", "default_profile", data_writer_qos_name_.c_str(),
            nullptr, DDS::STATUS_MASK_NONE);
        if (!data_writer_) {
            std::cerr << "[DDSManager_Bytes] Failed to create DataWriter.\n";
            return false;
        }
        std::cout << "[DDSManager_Bytes] Created DataWriter.\n";
    }
    else if (role_ == "subscriber") {
        void* mem = GloMemPool::allocate(sizeof(MyDataReaderListener), __FILE__, __LINE__);
        if (!mem) {
            std::cerr << "[DDSManager_Bytes] Memory allocation failed for listener.\n";
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
            std::cerr << "[DDSManager_Bytes] Failed to create DataReader.\n";
            return false;
        }
        std::cout << "[DDSManager_Bytes] Created DataReader with listener.\n";
    }
    else {
        std::cerr << "[DDSManager_Bytes] Invalid role: " << role_ << "\n";
        return false;
    }

    is_initialized_ = true;
    std::cout << "[DDSManager_Bytes] Initialization successful.\n";
    return true;
}

void DDSManager_Bytes::shutdown() {
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
    std::cout << "[DDSManager_Bytes] Shutdown completed.\n";
}

// 准备测试数据
bool DDSManager_Bytes::prepareBytesData(DDS_Bytes& sample, int minSize, int maxSize) {
    int actualSize = minSize;
    if (minSize != maxSize) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(minSize, maxSize);
        actualSize = dis(gen);
    }

    DDS_ULong ul_size = static_cast<DDS_ULong>(actualSize);

    // 改进点：为序列化留余量
    DDS_ULong reserve_extra = 16;
    if (ul_size > 4096) reserve_extra = 64;
    if (ul_size > 65536) reserve_extra = 256;
    if (ul_size > 1048576) reserve_extra = 4096;

    DDS_ULong alloc_size = ul_size + reserve_extra;

    // 1. 分配内存池内存
    DDS_Octet* buffer = static_cast<DDS_Octet*>(
        GloMemPool::allocate(alloc_size * sizeof(DDS_Octet), __FILE__, __LINE__)
        );
    if (!buffer) {
        return false; // 注意：此时 sample.value 尚未初始化，无需 finalize
    }

    // 2. 初始化 sequence -> 这是一个宏，展开后是 void 函数，不能赋值！
    DDS_OctetSeq_initialize(&sample.value);

    // 3. 租借内存 -> 返回 ZR_BOOLEAN (bool)
    ZR_BOOLEAN loan_result = DDS_OctetSeq_loan_contiguous(&sample.value, buffer, ul_size, alloc_size);

    if (!loan_result) { // 使用 !loan_result 判断失败
        // 租借失败，需要手动释放 buffer
        GloMemPool::deallocate(buffer);
        // 并 finalize 已初始化但未成功租借的 sequence
        DDS_OctetSeq_finalize(&sample.value);
        return false;
    }

    // 4. 填充数据
    for (DDS_ULong i = 0; i < ul_size; ++i) {
        sample.value[i] = static_cast<DDS::Octet>(i % 256);
    }

    // 5. 更新 length，确保序列知道当前有效数据长度
    // （虽然 loan_contiguous 可能已设置，但显式设置更安全）
    sample.value._length = ul_size;

    Logger::getInstance().logAndPrint(
        "prepareBytesData: length=" + std::to_string(sample.value._length) +
        " maximum=" + std::to_string(sample.value._maximum)
    );

    return true;
}

// 清理 Bytes 数据
void DDSManager_Bytes::cleanupBytesData(DDS_Bytes& sample) {
    DDS_OctetSeq_finalize(&sample.value);  
}