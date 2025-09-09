// DDSManager.h
#pragma once
#include <string>
#include <functional>

#include "ConfigData.h"
#include "DomainParticipant.h"
#include "DomainParticipantFactory.h"

#ifndef DDS_TRUE
#define DDS_TRUE  true
#endif

#ifndef DDS_FALSE
#define DDS_FALSE false
#endif

struct TestData;
struct TestDataSeq;

using OnDataReceivedCallback = std::function<void(const TestData&, const DDS::SampleInfo&)>;
using OnEndOfRoundCallback = std::function<void()>;

class DDSManager {
public:
    explicit DDSManager(const ConfigData& config, const std::string& xml_qos_file_path);
    ~DDSManager();

    DDSManager(const DDSManager&) = delete;
    DDSManager& operator=(const DDSManager&) = delete;

    // 初始化 DDS 实体，传入回调（供外部测试模块使用）
    bool initialize(
        OnDataReceivedCallback dataCallback = nullptr,
        OnEndOfRoundCallback endCallback = nullptr
    );

    void shutdown();

    // 提供实体访问接口
    DDS::DomainParticipant* get_participant() const { return participant_; }
    DDS::DataWriter* get_data_writer() const { return data_writer_; }
    DDS::DataReader* get_data_reader() const { return data_reader_; }
    bool is_initialized() const { return is_initialized_; }

    // 辅助函数
    bool prepareTestData(TestData& sample, int minSize, int maxSize);
    void cleanupTestData(TestData& sample);

private:
    std::string xml_qos_file_path_;

    // 配置字段
    DDS::DomainId_t domain_id_;
    std::string topic_name_;
    std::string type_name_;
    std::string role_;
    std::string participant_factory_qos_name_;
    std::string participant_qos_name_;
    std::string data_writer_qos_name_;
    std::string data_reader_qos_name_;

    // DDS 实体
    DDS::DomainParticipantFactory* factory_ = nullptr;
    DDS::DomainParticipant* participant_ = nullptr;
    DDS::Topic* topic_ = nullptr;
    DDS::DataWriter* data_writer_ = nullptr;
    DDS::DataReader* data_reader_ = nullptr;

    class MyDataReaderListener;
    MyDataReaderListener* listener_ = nullptr;

    bool is_initialized_ = false;
};