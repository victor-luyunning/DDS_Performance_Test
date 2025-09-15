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

    DDSManager_Bytes(const ConfigData& config, const std::string& xml_qos_file_path);
    ~DDSManager_Bytes();

    // -------------------------------
    // 【原有接口】保持完全不变（用于吞吐测试）
    // -------------------------------
    bool initialize(
        OnDataReceivedCallback_Bytes dataCallback = nullptr,
        OnEndOfRoundCallback endCallback = nullptr
    );

    // -------------------------------
    // 【新增接口】专用于时延测试
    // -------------------------------
    bool initialize_latency(
        OnDataReceivedCallback_Bytes ping_callback = nullptr,
        OnDataReceivedCallback_Bytes pong_callback = nullptr,
        OnEndOfRoundCallback end_callback = nullptr
    );

    void shutdown();

    // -------------------------------
    // 获取实体指针（提供两套接口）
    // -------------------------------

    // 吞吐用
    DDS::DataWriter* get_data_writer() const { return m_throughput_writer; }
    DDS::DataReader* get_data_reader() const { return m_throughput_reader; }

    // 时延用（可选暴露）
    DDS::DataWriter* get_Ping_data_writer() const { return m_ping_writer; }
    DDS::DataReader* get_Ping_data_reader() const { return m_ping_reader; }
    DDS::DataWriter* get_Pong_data_writer() const { return m_pong_writer; }
    DDS::DataReader* get_Pong_data_reader() const { return m_pong_reader; }

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
    // === 配置字段 ===
    int domain_id_;
    std::string base_topic_name_;         // 来自 config.m_topicName
    std::string type_name_;
    std::string participant_factory_qos_name_;
    std::string participant_qos_name_;
    std::string data_writer_qos_name_;
    std::string data_reader_qos_name_;
    std::string xml_qos_file_path_;
    bool is_positive_role_;

    // === DDS 实体 ===
    DDS::DomainParticipantFactory* factory_ = nullptr;
    DDS::DomainParticipant* participant_ = nullptr;

    // 🔹 吞吐实体（单 Topic）
    DDS::Topic* throughput_topic_ = nullptr;
    DDS::DataWriter* m_throughput_writer = nullptr;
    DDS::DataReader* m_throughput_reader = nullptr;

    // 🔹 时延实体（双 Topic）
    DDS::Topic* ping_topic_ = nullptr;
    DDS::Topic* pong_topic_ = nullptr;
    DDS::DataWriter* m_ping_writer = nullptr;
    DDS::DataReader* m_ping_reader = nullptr;
    DDS::DataWriter* m_pong_writer = nullptr;
    DDS::DataReader* m_pong_reader = nullptr;

    // Listener
    class MyDataReaderListener;
    MyDataReaderListener* m_ping_listener_ = nullptr;
    MyDataReaderListener* m_pong_listener_ = nullptr;
    MyDataReaderListener* m_throughput_listener_ = nullptr;  // 原来的 listener_

    bool is_initialized_ = false;

    // === 内部辅助函数 ===
    bool create_type_and_participant();

    // 分开创建不同模式的实体
    bool create_throughput_entities(OnDataReceivedCallback_Bytes data_cb, OnEndOfRoundCallback end_cb);
    bool create_latency_entities(
        OnDataReceivedCallback_Bytes ping_cb,
        OnDataReceivedCallback_Bytes pong_cb,
        OnEndOfRoundCallback end_cb
    );

    // Topic 名生成
    std::string make_ping_topic_name() const;
    std::string make_pong_topic_name() const;
};