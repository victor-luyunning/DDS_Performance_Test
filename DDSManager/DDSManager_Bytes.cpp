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
#include <chrono>

// Packet Header 结构
struct PacketHeader {
    uint32_t sequence;
    uint64_t timestamp;
    uint8_t  packet_type; // 0=数据包, 1=结束包
};

// 内部 Listener 类
class DDSManager_Bytes::MyDataReaderListener
    : public virtual DDS::SimpleDataReaderListener<DDS::Bytes, DDS::BytesSeq, DDS::ZRDDSDataReader<DDS::Bytes, DDS::BytesSeq>>
{
public:
    MyDataReaderListener(
        OnDataReceivedCallback_Bytes dataCb,
        OnEndOfRoundCallback endCb
    ) : onDataReceived_(std::move(dataCb)), onEndOfRound_(std::move(endCb)) {
    }

    void on_process_sample(
        DDS::DataReader*,
        const DDS::Bytes& sample,
        const DDS::SampleInfo& info
    ) override {
        if (!info.valid_data || sample.value.length() < sizeof(PacketHeader)) {
            Logger::getInstance().logAndPrint("[DDSManager_Bytes] 收到无效或过短的数据包");
            return;
        }

        const uint8_t* buffer = sample.value.get_contiguous_buffer();
        if (!buffer) {
            Logger::getInstance().error("[DDSManager_Bytes] buffer 为空");
            return;
        }

        const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(buffer);

        // 判断是否为结束包
        if (hdr->packet_type == 1) {
            Logger::getInstance().logAndPrint(
                "[DDSManager_Bytes] 收到结束包 | seq=" + std::to_string(hdr->sequence) +
                " | ts=" + std::to_string(hdr->timestamp) +
                " | length=" + std::to_string(sample.value.length())
            );
            if (onEndOfRound_) {
                onEndOfRound_();
            }
            return;
        }

        if (onDataReceived_) {
            onDataReceived_(sample, info);
        }
    }

private:
    OnDataReceivedCallback_Bytes onDataReceived_;
    OnEndOfRoundCallback onEndOfRound_;
};

// 构造函数：从 ConfigData 初始化基本参数
DDSManager_Bytes::DDSManager_Bytes(const ConfigData& config, const std::string& xml_qos_file_path)
    : domain_id_(config.m_domainId)
    , base_topic_name_(config.m_topicName)
    , type_name_(config.m_typeName)
    , participant_factory_qos_name_(config.m_dpfQosName)
    , participant_qos_name_(config.m_dpQosName)
    , data_writer_qos_name_(config.m_writerQosName)
    , data_reader_qos_name_(config.m_readerQosName)
    , xml_qos_file_path_(xml_qos_file_path)
    , is_positive_role_(config.m_isPositive)
{
}

DDSManager_Bytes::~DDSManager_Bytes() {
    if (is_initialized_) {
        shutdown();
    }
}

// -------------------------------
// make_*: 自动生成 topic 名
// -------------------------------

std::string DDSManager_Bytes::make_ping_topic_name() const {
    return base_topic_name_ + "_ping";
}

std::string DDSManager_Bytes::make_pong_topic_name() const {
    return base_topic_name_ + "_pong";
}

// -------------------------------
// initialize: 根据模式初始化
// -------------------------------

bool DDSManager_Bytes::initialize(TestMode mode,
    OnDataReceivedCallback_Bytes ping_callback,
    OnDataReceivedCallback_Bytes pong_callback,
    OnEndOfRoundCallback end_callback) {
    current_mode_ = mode;
    Logger::getInstance().logAndPrint(
        std::string("[DDSManager_Bytes] 初始化模式: ") +
        (mode == DDSManager_Bytes::TestMode::THROUGHPUT ? "THROUGHPUT" : "LATENCY") +
        " | Topic Base: " + base_topic_name_
    );

    if (!create_type_and_participant()) {
        return false;
    }

    bool result = false;
    switch (mode) {
    case TestMode::THROUGHPUT:
        result = create_throughput_entities();
        break;
    case TestMode::LATENCY:
        result = create_latency_entities(std::move(ping_callback),
            std::move(pong_callback),
            std::move(end_callback));
        break;
    }

    if (!result) {
        shutdown();
    }
    else {
        is_initialized_ = true;
    }

    return result;
}

// -------------------------------
// create_type_and_participant
// -------------------------------

bool DDSManager_Bytes::create_type_and_participant() {
    const char* qosFilePath = xml_qos_file_path_.c_str();
    const char* p_lib_name = "default_lib";
    const char* p_prof_name = "default_profile";
    const char* pf_qos_name = participant_factory_qos_name_.empty() ? nullptr : participant_factory_qos_name_.c_str();
    const char* p_qos_name = participant_qos_name_.empty() ? nullptr : participant_qos_name_.c_str();

    factory_ = DDS::DomainParticipantFactory::get_instance_w_profile(
        qosFilePath, p_lib_name, p_prof_name, pf_qos_name);
    if (!factory_) {
        Logger::getInstance().error("[DDSManager_Bytes] 获取 DomainParticipantFactory 失败");
        return false;
    }

    participant_ = factory_->create_participant_with_qos_profile(
        domain_id_, p_lib_name, p_prof_name, p_qos_name, nullptr, DDS::STATUS_MASK_NONE);
    if (!participant_) {
        Logger::getInstance().error("[DDSManager_Bytes] 创建 DomainParticipant 失败");
        return false;
    }

    DDS::BytesTypeSupport* type_support = DDS::BytesTypeSupport::get_instance();
    if (!type_support) {
        Logger::getInstance().error("[DDSManager_Bytes] 获取 BytesTypeSupport 实例失败");
        return false;
    }

    if (type_support->register_type(participant_, type_support->get_type_name()) != DDS::RETCODE_OK) {
        std::ostringstream oss;
        oss << "[DDSManager_Bytes] 注册类型 '" << type_support->get_type_name() << "' 失败";
        Logger::getInstance().error(oss.str());
        return false;
    }

    return true;
}

// -------------------------------
// create_throughput_entities
// -------------------------------

bool DDSManager_Bytes::create_throughput_entities() {
    throughput_topic_ = participant_->create_topic(
        base_topic_name_.c_str(),
        DDS::BytesTypeSupport::get_instance()->get_type_name(),
        DDS::TOPIC_QOS_DEFAULT, nullptr, DDS::STATUS_MASK_NONE);
    if (!throughput_topic_) {
        Logger::getInstance().error("[DDSManager_Bytes] 创建吞吐 Topic 失败: " + base_topic_name_);
        return false;
    }

    if (is_positive_role_) {
        m_throughput_writer = participant_->create_datawriter_with_topic_and_qos_profile(
            throughput_topic_->get_name(), DDS::BytesTypeSupport::get_instance(),
            "default_lib", "default_profile", data_writer_qos_name_.c_str(),
            nullptr, DDS::STATUS_MASK_NONE);
        if (!m_throughput_writer) {
            Logger::getInstance().error("[DDSManager_Bytes] 创建吞吐 DataWriter 失败");
            return false;
        }
        Logger::getInstance().logAndPrint("[DDSManager_Bytes] 吞吐 DataWriter 创建成功");
    }
    else {
        void* mem = GloMemPool::allocate(sizeof(MyDataReaderListener), __FILE__, __LINE__);
        if (!mem) {
            Logger::getInstance().error("[DDSManager_Bytes] 分配监听器内存失败");
            return false;
        }
        m_throughput_reader = participant_->create_datareader_with_topic_and_qos_profile(
            throughput_topic_->get_name(), DDS::BytesTypeSupport::get_instance(),
            "default_lib", "default_profile", data_reader_qos_name_.c_str(),
            new (mem) MyDataReaderListener(nullptr, nullptr), DDS::STATUS_MASK_ALL);
        if (!m_throughput_reader) {
            GloMemPool::deallocate(mem);
            Logger::getInstance().error("[DDSManager_Bytes] 创建吞吐 DataReader 失败");
            return false;
        }
        Logger::getInstance().logAndPrint("[DDSManager_Bytes] 吞吐 DataReader 创建成功");
    }

    return true;
}

// -------------------------------
// create_latency_entities
// -------------------------------

bool DDSManager_Bytes::create_latency_entities(
    OnDataReceivedCallback_Bytes ping_callback,
    OnDataReceivedCallback_Bytes pong_callback,
    OnEndOfRoundCallback end_callback
) {
    const std::string ping_topic_name = make_ping_topic_name();
    const std::string pong_topic_name = make_pong_topic_name();

    // 创建两个 Topic
    ping_topic_ = participant_->create_topic(
        ping_topic_name.c_str(),
        DDS::BytesTypeSupport::get_instance()->get_type_name(),
        DDS::TOPIC_QOS_DEFAULT, nullptr, DDS::STATUS_MASK_NONE);
    pong_topic_ = participant_->create_topic(
        pong_topic_name.c_str(),
        DDS::BytesTypeSupport::get_instance()->get_type_name(),
        DDS::TOPIC_QOS_DEFAULT, nullptr, DDS::STATUS_MASK_NONE);

    if (!ping_topic_ || !pong_topic_) {
        Logger::getInstance().error("[DDSManager_Bytes] 创建 Latency Topics 失败");
        return false;
    }

    // === 根据角色决定创建哪些 writer/reader ===

    // 我作为 Initiator（发端），我要：
    // - 发 Ping → 创建 m_ping_writer
    // - 收 Pong → 创建 m_pong_reader + listener
    if (is_positive_role_) {
        // 创建 Ping DataWriter
        m_ping_writer = participant_->create_datawriter_with_topic_and_qos_profile(
            ping_topic_->get_name(), DDS::BytesTypeSupport::get_instance(),
            "default_lib", "default_profile", data_writer_qos_name_.c_str(),
            nullptr, DDS::STATUS_MASK_NONE);
        if (!m_ping_writer) {
            Logger::getInstance().error("[DDSManager_Bytes] 创建 Ping DataWriter 失败");
            return false;
        }

        // 创建 Pong DataReader（用来收对方回的 Pong）
        if (pong_callback) {
            void* mem = GloMemPool::allocate(sizeof(MyDataReaderListener), __FILE__, __LINE__);
            if (!mem) return false;
            m_pong_listener_ = new (mem) MyDataReaderListener(std::move(pong_callback), nullptr);

            m_pong_reader = participant_->create_datareader_with_topic_and_qos_profile(
                pong_topic_->get_name(), DDS::BytesTypeSupport::get_instance(),
                "default_lib", "default_profile", data_reader_qos_name_.c_str(),
                m_pong_listener_, DDS::STATUS_MASK_ALL);
            if (!m_pong_reader) {
                Logger::getInstance().error("[DDSManager_Bytes] 创建 Pong DataReader 失败");
                return false;
            }
        }
    }
    // 我作为 Responder（收端），我要：
    // - 收 Ping → 创建 m_ping_reader + listener
    // - 回 Pong → 创建 m_pong_writer
    else {
        // 创建 Ping DataReader（收对方发来的 Ping）
        if (ping_callback || end_callback) {
            void* mem = GloMemPool::allocate(sizeof(MyDataReaderListener), __FILE__, __LINE__);
            if (!mem) return false;
            m_ping_listener_ = new (mem) MyDataReaderListener(std::move(ping_callback), std::move(end_callback));

            m_ping_reader = participant_->create_datareader_with_topic_and_qos_profile(
                ping_topic_->get_name(), DDS::BytesTypeSupport::get_instance(),
                "default_lib", "default_profile", data_reader_qos_name_.c_str(),
                m_ping_listener_, DDS::STATUS_MASK_ALL);
            if (!m_ping_reader) {
                Logger::getInstance().error("[DDSManager_Bytes] 创建 Ping DataReader 失败");
                return false;
            }
        }

        // 创建 Pong DataWriter（回复 Pong）
        m_pong_writer = participant_->create_datawriter_with_topic_and_qos_profile(
            pong_topic_->get_name(), DDS::BytesTypeSupport::get_instance(),
            "default_lib", "default_profile", data_writer_qos_name_.c_str(),
            nullptr, DDS::STATUS_MASK_NONE);
        if (!m_pong_writer) {
            Logger::getInstance().error("[DDSManager_Bytes] 创建 Pong DataWriter 失败");
            return false;
        }
    }

    Logger::getInstance().logAndPrint("[DDSManager_Bytes] 成功创建时延专用四元组实体");
    return true;
}

// -------------------------------
// shutdown
// -------------------------------

void DDSManager_Bytes::shutdown() {
    if (!is_initialized_ || !factory_) return;

    if (participant_) {
        participant_->delete_contained_entities();
    }

    if (factory_) {
        factory_->delete_participant(participant_);
        participant_ = nullptr;
    }

    // 清理 listeners
    if (m_ping_listener_) {
        m_ping_listener_->~MyDataReaderListener();
        GloMemPool::deallocate(m_ping_listener_);
        m_ping_listener_ = nullptr;
    }
    if (m_pong_listener_) {
        m_pong_listener_->~MyDataReaderListener();
        GloMemPool::deallocate(m_pong_listener_);
        m_pong_listener_ = nullptr;
    }

    is_initialized_ = false;
    Logger::getInstance().logAndPrint("[DDSManager_Bytes] 已关闭");
}

// -------------------------------
// prepare/cleanup 数据
// -------------------------------

bool DDSManager_Bytes::prepareBytesData(
    DDS::Bytes& sample,
    int minSize,
    int maxSize,
    uint32_t sequence,
    uint64_t timestamp
) {
    int actualSize = minSize;
    if (minSize != maxSize) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(minSize, maxSize);
        actualSize = dis(gen);
    }

    const size_t header_size = sizeof(PacketHeader);
    if (actualSize < static_cast<int>(header_size)) {
        actualSize = header_size;
    }

    DDS_ULong ul_size = static_cast<DDS_ULong>(actualSize);
    DDS_ULong reserve_extra = (ul_size > 65536) ? 256 : (ul_size > 4096) ? 64 : 16;
    DDS_ULong alloc_size = ul_size + reserve_extra;

    DDS_Octet* buffer = static_cast<DDS_Octet*>(
        GloMemPool::allocate(alloc_size * sizeof(DDS_Octet), __FILE__, __LINE__)
        );
    if (!buffer) {
        Logger::getInstance().error("[DDSManager_Bytes] 内存分配失败，大小: " + std::to_string(alloc_size));
        return false;
    }

    DDS_OctetSeq_initialize(&sample.value);
    ZR_BOOLEAN loan_result = DDS_OctetSeq_loan_contiguous(&sample.value, buffer, ul_size, alloc_size);
    if (!loan_result) {
        GloMemPool::deallocate(buffer);
        DDS_OctetSeq_finalize(&sample.value);
        Logger::getInstance().error("[DDSManager_Bytes] 租借内存失败");
        return false;
    }

    PacketHeader* hdr = reinterpret_cast<PacketHeader*>(buffer);
    hdr->sequence = sequence;
    hdr->timestamp = timestamp;
    hdr->packet_type = 0;

    for (DDS_ULong i = header_size; i < ul_size; ++i) {
        sample.value[i] = static_cast<DDS::Octet>((i + sequence) % 255);
    }
    sample.value._length = ul_size;

    return true;
}

bool DDSManager_Bytes::prepareEndBytesData(DDS::Bytes& sample, int minSize) {
    DDS_ULong ul_size = static_cast<DDS_ULong>(minSize);
    const size_t header_size = sizeof(PacketHeader);
    if (ul_size < header_size) ul_size = header_size;

    const DDS_ULong reserve_extra = 16;
    DDS_ULong alloc_size = ul_size + reserve_extra;

    DDS_Octet* buffer = static_cast<DDS_Octet*>(
        GloMemPool::allocate(alloc_size * sizeof(DDS_Octet), __FILE__, __LINE__)
        );
    if (!buffer) {
        Logger::getInstance().error("[DDSManager_Bytes] 结束包内存分配失败");
        return false;
    }

    DDS_OctetSeq_initialize(&sample.value);
    ZR_BOOLEAN loan_result = DDS_OctetSeq_loan_contiguous(&sample.value, buffer, ul_size, alloc_size);
    if (!loan_result) {
        GloMemPool::deallocate(buffer);
        DDS_OctetSeq_finalize(&sample.value);
        return false;
    }

    PacketHeader* hdr = reinterpret_cast<PacketHeader*>(buffer);
    hdr->sequence = 0xFFFFFFFF;
    hdr->timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    hdr->packet_type = 1;

    for (DDS_ULong i = header_size; i < ul_size; ++i) {
        sample.value[i] = 0;
    }
    sample.value._length = ul_size;

    return true;
}

void DDSManager_Bytes::cleanupBytesData(DDS::Bytes& sample) {
    DDS_OctetSeq_finalize(&sample.value);
}