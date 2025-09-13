// DDSManager_Bytes.h
#pragma once
#include <string>
#include <functional>

#include "ConfigData.h"
#include "DomainParticipant.h"
#include "DomainParticipantFactory.h"
#include "ZRBuiltinTypes.h"  

using OnDataReceivedCallback_Bytes = std::function<void(const DDS_Bytes&, const DDS::SampleInfo&)>;
using OnEndOfRoundCallback = std::function<void()>;

class DDSManager_Bytes {
public:
    explicit DDSManager_Bytes(const ConfigData& config, const std::string& xml_qos_file_path);
    ~DDSManager_Bytes();

    DDSManager_Bytes(const DDSManager_Bytes&) = delete;
    DDSManager_Bytes& operator=(const DDSManager_Bytes&) = delete;

    // 初始化 DDS 实体，传入回调（供外部测试模块使用）
    bool initialize(
        OnDataReceivedCallback_Bytes dataCallback = nullptr,
        OnEndOfRoundCallback endCallback = nullptr
    );

    void shutdown();

    // 提供实体访问接口
    DDS::DomainParticipant* get_participant() const { return participant_; }
    DDS::DataWriter* get_data_writer() const { return data_writer_; }
    DDS::DataReader* get_data_reader() const { return data_reader_; }
    bool is_initialized() const { return is_initialized_; }

    // 辅助函数：准备 Bytes 测试数据
    bool prepareBytesData(DDS_Bytes& sample, int minSize, int maxSize);
    void cleanupBytesData(DDS_Bytes& sample);

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