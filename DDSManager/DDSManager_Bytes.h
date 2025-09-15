// DDSManager_Bytes.h
#pragma once

#include "ConfigData.h"
#include "ZRBuiltinTypes.h"
#include "ZRDDSDataReader.h"
#include "ZRDDSDataWriter.h"
#include "DomainParticipant.h"
#include "DomainParticipantFactory.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

class DDSManager_Bytes {
public:
    using OnDataReceivedCallback_Bytes = std::function<void(const DDS::Bytes&, const DDS::SampleInfo&)>;
    using OnEndOfRoundCallback = std::function<void()>;

    enum class TestMode {
        THROUGHPUT,
        LATENCY
    };

    DDSManager_Bytes(const ConfigData& config, const std::string& xml_qos_file_path);
    ~DDSManager_Bytes();

    bool initialize(TestMode mode,
        OnDataReceivedCallback_Bytes ping_callback = nullptr,
        OnDataReceivedCallback_Bytes pong_callback = nullptr,
        OnEndOfRoundCallback end_callback = nullptr);

    void shutdown();

    // -------------------------------
    // 专为时延测试提供的四个独立接口
    // -------------------------------
    DDS::DataWriter* get_Ping_data_writer() const { return m_ping_writer; }
    DDS::DataReader* get_Ping_data_reader() const { return m_ping_reader; }
    DDS::DataWriter* get_Pong_data_writer() const { return m_pong_writer; }
    DDS::DataReader* get_Pong_data_reader() const { return m_pong_reader; }

    // 原有吞吐接口（保持兼容性）
    DDS::DataWriter* get_data_writer() const { return m_throughput_writer; }
    DDS::DataReader* get_data_reader() const { return m_throughput_reader; }

    // 数据准备
    bool prepareBytesData(
        DDS::Bytes& sample,
        int minSize,
        int maxSize,
        uint32_t sequence,
        uint64_t timestamp
    );
    bool prepareEndBytesData(DDS::Bytes& sample, int minSize);
    void cleanupBytesData(DDS::Bytes& sample);

private:
    // === 所有来自 ConfigData 的字段 ===
    int domain_id_;
    std::string base_topic_name_;         // 来自 config.m_topicName
    std::string type_name_;
    std::string participant_factory_qos_name_;
    std::string participant_qos_name_;
    std::string data_writer_qos_name_;
    std::string data_reader_qos_name_;
    std::string xml_qos_file_path_;

    bool is_positive_role_;               // 来自 config.m_isPositive

    TestMode current_mode_ = TestMode::THROUGHPUT;

    // === DDS 实体 ===
    DDS::DomainParticipantFactory* factory_ = nullptr;
    DDS::DomainParticipant* participant_ = nullptr;

    // 🔹 吞吐用实体（单 topic）
    DDS::Topic* throughput_topic_ = nullptr;
    DDS::DataWriter* m_throughput_writer = nullptr;
    DDS::DataReader* m_throughput_reader = nullptr;

    // 🔹 时延用实体（双 topic）
    DDS::Topic* ping_topic_ = nullptr;
    DDS::Topic* pong_topic_ = nullptr;
    DDS::DataWriter* m_ping_writer = nullptr;
    DDS::DataReader* m_ping_reader = nullptr;
    DDS::DataWriter* m_pong_writer = nullptr;
    DDS::DataReader* m_pong_reader = nullptr;

    // Listener（用于时延中的回调）
    class MyDataReaderListener;
    MyDataReaderListener* m_ping_listener_ = nullptr;
    MyDataReaderListener* m_pong_listener_ = nullptr;

    bool is_initialized_ = false;

    // === 内部辅助函数 ===
    bool create_type_and_participant();
    bool create_throughput_entities();
    bool create_latency_entities(
        OnDataReceivedCallback_Bytes ping_cb,
        OnDataReceivedCallback_Bytes pong_cb,
        OnEndOfRoundCallback end_cb
    );

    // === 自动生成 topic 名 ===
    std::string make_ping_topic_name() const;
    std::string make_pong_topic_name() const;
};