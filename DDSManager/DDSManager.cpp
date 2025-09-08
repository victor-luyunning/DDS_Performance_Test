// DDSManager.cpp
#include <string>
#include <thread>


#include "DDSManager.h"
#include "Logger.h"
#include "GloMemPool.h"  // 添加：引入全局内存池

#include "Topic.h"
#include "DataWriter.h"
#include "DataReader.h"
#include "DataReaderListener.h"
#include "ReturnCode_t.h"

#include "TestDataDataReader.h"
#include "TestDataDataWriter.h"
#include "TestData.h"
#include "TestDataTypeSupport.h"
#include <random>

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
        std::cerr << "[DDSManager] Failed to create DomainParticipant with QoS profile.\n";
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

        std::string dw_library_name = "default_lib";
        std::string dw_profile_name = "default_profile";
        const char* dw_lib_name = dw_library_name.c_str();
        const char* dw_prof_name = dw_profile_name.c_str();
        const char* dw_qos_name = data_writer_qos_name_.c_str();

        const char* topic_name_str = topic_->get_name();
        if (!topic_name_str) {
            std::cerr << "[DDSManager] Failed to get topic name string.\n";
            return false;
        }

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
            std::cerr << "[DDSManager] Failed to create DataWriter.\n";
            return false;
        }
        std::cout << "[DDSManager] Created DataWriter.\n";

    }
    else if (role_ == "subscriber") {
        std::cout << "[DDSManager] Creating entities for Subscriber role.\n";

        if (!callback) {
            std::cerr << "[DDSManager] Warning: Subscriber role specified but no data available callback provided.\n";
        }

        // 使用 GloMemPool 分配 listener 对象内存
        void* listener_mem = GloMemPool::allocate(sizeof(MyDataReaderListener), __FILE__, __LINE__);
        if (!listener_mem) {
            std::cerr << "[DDSManager] Failed to allocate memory for MyDataReaderListener.\n";
            return false;
        }
        listener_ = new (listener_mem) MyDataReaderListener(callback);

        std::string dr_library_name = "default_lib";
        std::string dr_profile_name = "default_profile";
        const char* dr_lib_name = dr_library_name.c_str();
        const char* dr_prof_name = dr_profile_name.c_str();
        const char* dr_qos_name = data_reader_qos_name_.c_str();

        const char* topic_name_str_dr = topic_->get_name();
        if (!topic_name_str_dr) {
            std::cerr << "[DDSManager] Failed to get topic name string for DataReader.\n";
            return false;
        }

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
            std::cerr << "[DDSManager] Failed to create DataReader.\n";
            // 立即释放已分配的 listener 内存
            listener_->~MyDataReaderListener();
            GloMemPool::deallocate(listener_);
            listener_ = nullptr;
            return false;
        }
        std::cout << "[DDSManager] Created DataReader with listener.\n";

    }
    else {
        std::cerr << "[DDSManager] Invalid role specified: '" << role_ << "'.\n";
        return false;
    }

    is_initialized_ = true;
    std::cout << "[DDSManager] DDS entities initialized successfully.\n";
    return true;
}

bool DDSManager::prepareTestData(TestData& sample, int minSize, int maxSize) {
    int actualSize = minSize;
    if (minSize != maxSize) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(minSize, maxSize);
        actualSize = dis(gen);
    }

    if (!TestDataInitialize(&sample)) {
        std::cerr << "[DDSManager] TestDataInitialize failed.\n";
        return false;
    }

    DDS_ULong ul_size = static_cast<DDS_ULong>(actualSize);

    // 注意：GloMemPool::allocate 使用 s_pool = nullptr，与 ZRDealloc(NULL, ptr) 匹配
    // 因此 TestDataFinalize 可安全释放该 buffer
    sample.value._contiguousBuffer = static_cast<DDS_Octet*>(
        GloMemPool::allocate(ul_size * sizeof(DDS_Octet), __FILE__, __LINE__)
        );

    if (!sample.value._contiguousBuffer) {
        std::cerr << "[DDSManager] GloMemPool::allocate failed for payload of size " << actualSize << ".\n";
        TestDataFinalize(&sample);  // 这会释放可能已分配的资源
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
    // TestDataFinalize 应该内部调用 ZRDealloc 来释放 buffer
    // 而 ZRDealloc 会匹配 GloMemPool::allocate 的分配
    TestDataFinalize(&sample);
}

template<typename T>
T getWithFallbackToFirst(const std::vector<T>& vec, int index) {
    if (index < 0 || index >= static_cast<int>(vec.size())) {
        return vec[0]; // 越界 → 回退到第一个元素
    }
    return vec[index];
}

int DDSManager::runPublisher(const ConfigData& config) {
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

    // 只使用 config.m_activeLoop 指定的轮次参数，但无限重复执行该轮
    int activeLoopIndex = config.m_activeLoop;

    if (activeLoopIndex < 0) {
        std::cerr << "[DDSManager] Error: m_activeLoop is not set (must be >= 0).\n";
        return -1;
    }

    // 无限重复执行 activeLoopIndex 轮
    int round = 1;
    while (true) {
        // 每轮都重新读取参数（虽然值不变，但保持逻辑清晰）
        int minSize = getWithFallbackToFirst(config.m_minSize, activeLoopIndex);
        int maxSize = getWithFallbackToFirst(config.m_maxSize, activeLoopIndex);
        int sendCount = getWithFallbackToFirst(config.m_sendCount, activeLoopIndex);
        int sendDelayCount = getWithFallbackToFirst(config.m_sendDelayCount, activeLoopIndex);
        int sendDelay = getWithFallbackToFirst(config.m_sendDelay, activeLoopIndex);
        int sendPrintGap = getWithFallbackToFirst(config.m_sendPrintGap, activeLoopIndex);

        // 准备样本数据
        TestData sample;
        if (!prepareTestData(sample, minSize, maxSize)) {
            std::cerr << "[DDSManager] Failed to prepare test data for round " << round << ".\n";
            std::this_thread::sleep_for(std::chrono::seconds(1)); // 等待1秒后重试
            continue;
        }

        // 打印本轮开始信息
        Logger::getInstance().logAndPrint(
            "第 " + std::to_string(round) + " 轮发送开始（基于配置轮次 " + std::to_string(activeLoopIndex + 1) + "） | "
            "数据大小: [" + std::to_string(minSize) + ", " + std::to_string(maxSize) + "] | "
            "发送总量: " + std::to_string(sendCount) + " | "
            "打印间隔: " + std::to_string(sendPrintGap) + " | "
            "延迟策略: 每 " + std::to_string(sendDelayCount) + " 条延迟 " + std::to_string(sendDelay) + "μs"
        );

        // 模拟等待匹配
        Logger::getInstance().logAndPrint("Writer waiting matching(0/1)..");

        // 发送本轮数据
        for (int i = 0; i < sendCount; ++i) {
            if (minSize != maxSize) {
                cleanupTestData(sample);
                if (!prepareTestData(sample, minSize, maxSize)) {
                    std::cerr << "[DDSManager] Failed to prepare dynamic-sized data at index " << i << ".\n";
                    break; // 跳出本轮，进入下一轮重试
                }
            }

            DDS::ReturnCode_t ret = writer->write(sample, DDS_HANDLE_NIL_NATIVE);
            if (ret != DDS::RETCODE_OK) {
                Logger::getInstance().logAndPrint("发送失败，ReturnCode: " + std::to_string(ret) + "，第 " + std::to_string(i + 1) + " 条");
            }

            if (sendPrintGap > 0 && (i + 1) % sendPrintGap == 0) {
                Logger::getInstance().logAndPrint(
                    "第 " + std::to_string(round) + " 轮：已发送第 " +
                    std::to_string(i + 1) + " 条数据（当前大小：" + std::to_string(sample.value.length()) + "）"
                );
            }

            if (sendDelayCount > 0 && (i + 1) % sendDelayCount == 0 && sendDelay > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(sendDelay));
            }
        }

        cleanupTestData(sample);
        Logger::getInstance().logAndPrint("第 " + std::to_string(round) + " 轮发送完成");

        // 模拟未收到接收端响应 → 重复本轮
        Logger::getInstance().logAndPrint("未检测到接收端响应，重复执行第 " + std::to_string(activeLoopIndex + 1) + " 轮...");

        round++;
        // 可选：防止 round 溢出
        if (round <= 0) round = 1;
    }

    // 以下代码不会被执行（因上面是无限循环）
    Logger::getInstance().logAndPrint("所有轮次 Publisher 发送完成");
    return 0;
}

int DDSManager::runSubscriber(const ConfigData& config) {
    if (role_ != "subscriber") {
        Logger::getInstance().logAndPrint("runSubscriber: 当前不是 subscriber 角色");
        return -1;
    }

    if (!data_reader_) {
        Logger::getInstance().logAndPrint("DataReader 未创建");
        return -1;
    }

    // 获取打印间隔参数，越界回退到索引0
    int recvPrintGap = getWithFallbackToFirst(config.m_recvPrintGap, 0); // 默认使用第0轮配置
    if (recvPrintGap <= 0) {
        recvPrintGap = 1; // 避免除零或无效间隔，至少每1次循环打印一次
    }

    Logger::getInstance().logAndPrint("Subscriber 等待数据...");

    int waitCount = 0;
    while (!data_received_.load()) {
        if (waitCount % recvPrintGap == 0) {
            Logger::getInstance().logAndPrint("Reader waiting matching(0/1)..");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waitCount++;

        // 防止整数溢出（极端情况）
        if (waitCount < 0) {
            waitCount = 0;
        }
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

    // 安全释放 listener
    if (listener_) {
        listener_->~MyDataReaderListener();           // 显式调用析构
        GloMemPool::deallocate(listener_);           // 释放内存
        listener_ = nullptr;
        std::cout << "[DDSManager] Deleted DataReader listener.\n";
    }

    if (participant_) {
        DDS::ReturnCode_t ret = participant_->delete_contained_entities();
        if (ret != DDS::RETCODE_OK) {
            std::cerr << "[DDSManager] Warning: Failed to delete contained entities. Return code: " << ret << "\n";
        }
        else {
            std::cout << "[DDSManager] Deleted contained entities.\n";
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