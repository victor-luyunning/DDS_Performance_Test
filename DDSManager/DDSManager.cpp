// DDSManager.cpp
#include "DDSManager.h"
#include "Logger.h"

#include "Topic.h"
#include "DataWriter.h"
#include "DataReader.h"
#include "DataReaderListener.h"
#include "ReturnCode_t.h"

#include "TestDataDataReader.h"
#include "TestDataDataWriter.h"
#include "TestData.h"
#include "TestDataTypeSupport.h"

// --- 初始化回调函数 ---
class DDSManager::MyDataReaderListener : public virtual DDS::SimpleDataReaderListener<TestData, TestDataSeq, DDS::ZRDDSDataReader<TestData, TestDataSeq>> {
public:
    explicit MyDataReaderListener(OnDataAvailableCallback cb) : callback_(std::move(cb)) {}

    void on_process_sample(DDS::ZRDDSDataReader<TestData, TestDataSeq>* /*reader*/, const TestData& sample) {
        if (callback_) {
            callback_(&sample);
        }
    }

private:
    OnDataAvailableCallback callback_;
};

// 构造函数：从 ConfigData 初始化所有字段
DDSManager::DDSManager(const ConfigData& config, const std::string xml_qos_file_path)
    : domain_id_(config.m_domainId)
    , topic_name_(config.m_topicName)
    , type_name_(config.m_typeName)
    , role_(config.m_isPositive ? "publisher" : "subscriber") 
    , participant_factory_qos_name_(config.m_dpfQosName)  
	, participant_qos_name_(config.m_dpQosName)
    , data_writer_qos_name_(config.m_writerQosName)          
    , data_reader_qos_name_(config.m_readerQosName)           
    , factory_(nullptr)
    , participant_(nullptr)
    , topic_(nullptr)
    , data_writer_(nullptr)
    , data_reader_(nullptr)
    , listener_(nullptr)
    , is_initialized_(false)
	, xml_qos_file_path_(xml_qos_file_path)
{
    // 所有配置参数已通过成员初始化列表赋值
}

DDSManager::~DDSManager() {
    if (is_initialized_) {
        shutdown();
    }
}

bool DDSManager::initialize(OnDataAvailableCallback callback) {
    std::cout << "[DDSManager] Initializing DDS entities...\n";

    const char* qosFilePath = xml_qos_file_path_.c_str();
    const char* p_lib_name = "default_lib";
    const char* p_prof_name = "default_profile";
    const char* pf_qos_name = participant_factory_qos_name_.empty() ? nullptr : participant_factory_qos_name_.c_str();
	const char* p_qos_name = participant_qos_name_.empty() ? nullptr : participant_qos_name_.c_str();

    // --- 创建域参与者工厂 ---
    factory_ = DDS::DomainParticipantFactory::get_instance_w_profile(
        qosFilePath,
        p_lib_name,
        p_prof_name,
        pf_qos_name
    );

    if (!factory_) {
        std::cerr << "[DDSManager] Failed to get DomainParticipantFactory instance with profile.\n";
        return false;
    }

    std::cout << "[DDSManager] Got DomainParticipantFactory instance with QoS profile.\n";

    std::string participant_library_name, participant_profile_name;  

    std::cout << "[DDSManager] create_participant_with_qos_profile args:\n"
        << "  library_name: '" << (p_lib_name ? p_lib_name : "nullptr") << "'\n"
        << "  profile_name: '" << (p_prof_name ? p_prof_name : "nullptr") << "'\n"
        << "  p_qos_name:     '" << (p_qos_name ? p_qos_name : "nullptr") << "'\n";

    // --- 创建域参与者 ---
    participant_ = factory_->create_participant_with_qos_profile(
        domain_id_,
        p_lib_name,
        p_prof_name,
        p_qos_name,
        nullptr,
        DDS::STATUS_MASK_NONE
    );
    if (!participant_) {
        std::cerr << "[DDSManager] Failed to create DomainParticipant with QoS profile '"
            << participant_profile_name << "' in library '" << participant_library_name << "'.\n";
        return false;
    }
    std::cout << "[DDSManager] Created DomainParticipant for domain " << domain_id_ << ".\n";

    // --- 注册数据类型 ---
    TestDataTypeSupport* type_support_instance = TestDataTypeSupport::get_instance();
    if (!type_support_instance) {
        std::cerr << "[DDSManager] Failed to get TestDataTypeSupport instance.\n";
        return false;
    }

    const char* registered_type_name = type_support_instance->get_type_name();
    if (!registered_type_name) {
        std::cerr << "[DDSManager] TestDataTypeSupport::get_type_name() returned nullptr.\n";
        return false;
    }

    if (type_name_ != std::string(registered_type_name)) {
        std::cerr << "[DDSManager] Warning: Configured type name '" << type_name_
            << "' does not match generated type name '" << registered_type_name << "'. Using generated name.\n";
    }

    DDS::ReturnCode_t register_ret = type_support_instance->register_type(participant_, registered_type_name);
    if (register_ret != DDS::RETCODE_OK) {
        std::cerr << "[DDSManager] Failed to register data type '" << registered_type_name << "'. Return code: " << register_ret << "\n";
        return false;
    }
    std::cout << "[DDSManager] Registered data type '" << registered_type_name << "'.\n";

    // --- 创建主题 ---
    topic_ = participant_->create_topic(
        topic_name_.c_str(),
        registered_type_name,
        DDS::TOPIC_QOS_DEFAULT,
        nullptr,
        DDS::STATUS_MASK_NONE
    );
    if (!topic_) {
        std::cerr << "[DDSManager] Failed to create Topic '" << topic_name_ << "'.\n";
        return false;
    }
    std::cout << "[DDSManager] Created Topic '" << topic_name_ << "'.\n";

    // --- 创建对应实体 ---
    if (role_ == "publisher") {
        std::cout << "[DDSManager] Creating entities for Publisher role.\n";

        std::string dw_library_name, dw_profile_name;
        dw_library_name = "default_lib";
        dw_profile_name = "default_profile";
        const char* dw_lib_name = dw_library_name.empty() ? nullptr : dw_library_name.c_str();
        const char* dw_prof_name = dw_profile_name.c_str();
        const char* dw_qos_name = data_writer_qos_name_.c_str();

        const char* topic_name_str = topic_->get_name();
        if (!topic_name_str) {
            std::cerr << "[DDSManager] Failed to get topic name string.\n";
            return false;
        }

        std::cout << "[DEBUG] DataWriter QoS names:\n"
            << "  Library: '" << (dw_lib_name ? dw_lib_name : "nullptr") << "'\n"
            << "  Profile: '" << (dw_prof_name ? dw_prof_name : "nullptr") << "'\n"
            << "  QoS:     '" << (dw_qos_name ? dw_qos_name : "nullptr") << "'\n";

        data_writer_ = participant_->create_datawriter_with_topic_and_qos_profile(
            topic_name_str,
            type_support_instance,
            dw_lib_name,
            dw_prof_name,
            dw_qos_name,
            nullptr,
            DDS::STATUS_MASK_NONE
        );

        if (!data_writer_) {
            std::cerr << "[DDSManager] Failed to create DataWriter with QoS profile '"
                << dw_profile_name << "' in library '" << dw_library_name << "'.\n";
            return false;
        }
        std::cout << "[DDSManager] Created DataWriter.\n";

        const char* registered_type = data_writer_->get_topic()->get_type_name();
        std::cout << "[DEBUG] DataWriter type name: " << (registered_type ? registered_type : "nullptr") << std::endl;

    }
    else if (role_ == "subscriber") {
        std::cout << "[DDSManager] Creating entities for Subscriber role.\n";

        if (!callback) {
            std::cerr << "[DDSManager] Warning: Subscriber role specified but no data available callback provided.\n";
        }

        listener_ = new MyDataReaderListener(callback);

        std::string dr_library_name, dr_profile_name;
        dr_library_name = "default_lib";
        dr_profile_name = "default_profile";
        const char* dr_lib_name = dr_library_name.empty() ? nullptr : dr_library_name.c_str();
        const char* dr_prof_name = dr_profile_name.c_str();
        const char* dr_qos_name = data_reader_qos_name_.c_str();

        const char* topic_name_str_dr = topic_->get_name();
        if (!topic_name_str_dr) {
            std::cerr << "[DDSManager] Failed to get topic name string for DataReader.\n";
            delete listener_;
            listener_ = nullptr;
            return false;
        }
        std::cout << "[DEBUG] DataReader QoS names:\n"
            << "  Library: '" << (dr_lib_name ? dr_lib_name : "nullptr") << "'\n"
            << "  Profile: '" << (dr_prof_name ? dr_prof_name : "nullptr") << "'\n"
            << "  QoS:     '" << (dr_qos_name ? dr_qos_name : "nullptr") << "'\n";

        data_reader_ = participant_->create_datareader_with_topic_and_qos_profile(
            topic_name_str_dr,
            type_support_instance,
            dr_lib_name,
            dr_prof_name,
            dr_qos_name,
            listener_,
            DDS::DATA_AVAILABLE_STATUS
        );

        if (!data_reader_) {
            std::cerr << "[DDSManager] Failed to create DataReader with QoS profile '"
                << dr_profile_name << "' in library '" << dr_library_name << "'.\n";
            delete listener_;
            listener_ = nullptr;
            return false;
        }
        std::cout << "[DDSManager] Created DataReader with listener.\n";

    }
    else {
        std::cerr << "[DDSManager] Invalid role specified: '" << role_ << "'. Must be 'publisher' or 'subscriber'.\n";
        return false;
    }

    is_initialized_ = true;
    std::cout << "[DDSManager] DDS entities initialized successfully.\n";
    return true;
}

bool DDSManager::prepareTestData(TestData& sample, int size) {
    if (!TestDataInitialize(&sample)) {
        std::cerr << "[DDSManager] TestDataInitialize failed.\n";
        return false;
    }

    DDS_ULong ul_size = static_cast<DDS_ULong>(size);
    sample.value._contiguousBuffer = (DDS_Octet*)ZRMalloc(nullptr, ul_size * sizeof(DDS_Octet));
    if (!sample.value._contiguousBuffer) {
        std::cerr << "[DDSManager] ZRMalloc failed for payload of size " << size << ".\n";
        TestDataFinalize(&sample);
        return false;
    }

    sample.value._length = ul_size;
    sample.value._maximum = ul_size;
    sample.value._owned = DDS_TRUE;
    sample.value._sequenceInit = DDS_INITIALIZE_NUMBER;

    // 填充测试数据
    for (DDS_ULong i = 0; i < ul_size; ++i) {
        sample.value._contiguousBuffer[i] = static_cast<DDS::Octet>(i % 256);
    }

    return true;
}

void DDSManager::cleanupTestData(TestData& sample) {
    TestDataFinalize(&sample);
}

int DDSManager::runPublisher(int sendCount, int intervalMs) {
    if (role_ != "publisher") {
        std::cerr << "[DDSManager] Error: Cannot runPublisher() when role is not 'publisher'.\n";
        return -1;
    }

    if (!data_writer_) {
        std::cerr << "[DDSManager] No DataWriter available.\n";
        return -1;
    }

    TestDataDataWriter* writer = dynamic_cast<TestDataDataWriter*>(data_writer_);
    if (!writer) {
        std::cerr << "[DDSManager] Failed to cast DataWriter to TestDataDataWriter.\n";
        return -1;
    }

    // 默认负载大小
    int payloadSize = 100;
    if (!participant_qos_name_.empty()) {
        // 可以根据 QoS 名称或配置决定 payload 大小
        // 或者从 Config 中提取 m_minSize —— 更推荐从 config 传入
    }

    TestData sample;
    if (!prepareTestData(sample, payloadSize)) {
        return -1;
    }

    Logger::getInstance().logAndPrint("Publisher 开始发送，共 " + std::to_string(sendCount) + " 次");

    for (int i = 0; i < sendCount; ++i) {
        DDS::ReturnCode_t ret = writer->write(sample, DDS_HANDLE_NIL_NATIVE);
        if (ret == DDS::RETCODE_OK) {
            Logger::getInstance().logAndPrint("已发送第 " + std::to_string(i + 1) + " 条数据（大小：" + std::to_string(payloadSize) + "）");
        }
        else {
            Logger::getInstance().logAndPrint("发送失败，ReturnCode: " + std::to_string(ret));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }

    cleanupTestData(sample);
    Logger::getInstance().logAndPrint("Publisher 发送完成");
    return 0;
}

int DDSManager::runSubscriber() {
    if (role_ != "subscriber") {
        Logger::getInstance().logAndPrint("runSubscriber: 当前不是 subscriber 角色");
        return -1;
    }

    if (!data_reader_) {
        Logger::getInstance().logAndPrint("DataReader 未创建");
        return -1;
    }

    Logger::getInstance().logAndPrint("Subscriber 等待数据...");

    while (!data_received_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    Logger::getInstance().logAndPrint("成功收到数据，退出");
    return 0;
}

void DDSManager::shutdown() {
    std::cout << "[DDSManager] Shutting down DDS entities...\n";

    if (!factory_) {
        std::cerr << "[DDSManager] Factory is null, nothing to shutdown.\n";
        return;
    }

    if (listener_) {
        delete listener_;
        listener_ = nullptr;
        std::cout << "[DDSManager] Deleted DataReader listener.\n";
    }

    if (participant_) {
        DDS::ReturnCode_t ret = participant_->delete_contained_entities();
        if (ret != DDS::RETCODE_OK) {
            std::cerr << "[DDSManager] Warning: Failed to delete contained entities of participant. Return code: " << ret << "\n";
        }
        else {
            std::cout << "[DDSManager] Deleted contained entities of participant.\n";
        }

        ret = factory_->delete_participant(participant_);
        if (ret != DDS::RETCODE_OK) {
            std::cerr << "[DDSManager] Warning: Failed to delete DomainParticipant. Return code: " << ret << "\n";
        }
        else {
            std::cout << "[DDSManager] Deleted DomainParticipant.\n";
        }
        participant_ = nullptr;
        topic_ = nullptr;
        data_writer_ = nullptr;
        data_reader_ = nullptr;
    }

    is_initialized_ = false;
    std::cout << "[DDSManager] DDS entities shut down.\n";
}