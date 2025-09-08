// DDSManager.h
#pragma once
#include <iostream>
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
class TestDataTypeSupport;

using OnDataAvailableCallback = std::function<void(const TestData*)>;

class DDSManager {
public:
    explicit DDSManager(const ConfigData& config, const std::string xml_qos_file_path);

    ~DDSManager();

    DDSManager(const DDSManager&) = delete;
    DDSManager& operator=(const DDSManager&) = delete;

    bool initialize(OnDataAvailableCallback callback = nullptr);
    void shutdown();

    int runPublisher(const ConfigData& config);
    int runSubscriber(const ConfigData& config); // 阻塞直到收到一条消息

    DDS::DomainParticipant* get_participant() const { return participant_; }
    DDS::DataWriter* get_data_writer() const { return data_writer_; }
    DDS::DataReader* get_data_reader() const { return data_reader_; }
    bool is_initialized() const { return is_initialized_; }

    bool hasDataReceived() const { return data_received_.load(); }
    void resetDataReceived() { data_received_.store(false); }

private:
    std::string xml_qos_file_path_;

    DDS::DomainId_t domain_id_;
    std::string topic_name_;
    std::string type_name_;
    std::string role_;
    std::string participant_factory_qos_name_;
    std::string participant_qos_name_;
    std::string data_writer_qos_name_;
    std::string data_reader_qos_name_;

    DDS::DomainParticipantFactory* factory_;
    DDS::DomainParticipant* participant_;
    DDS::Topic* topic_;
    DDS::DataWriter* data_writer_;
    DDS::DataReader* data_reader_;

    class MyDataReaderListener;
    MyDataReaderListener* listener_;
    std::atomic<bool> data_received_{ false };

    bool is_initialized_;

    bool prepareTestData(TestData& sample, int minSize, int maxSize);
    void cleanupTestData(TestData& sample);
};