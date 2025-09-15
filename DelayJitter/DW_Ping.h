#ifndef DW_PING_H
#define DW_PING_H

#include "DDSManager_Bytes.h"
#include "ThroughPut_Bytes.h"
#include "ZRBuiltinTypes.h"

#include <chrono>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional> 

// ���� TestData �ṹ��ʹ�� value �ֶδ洢����
// Լ����ʽ (ʹ��ǰ16�ֽ�):
// Bytes 0-3:   Sequence Number (int32_t)
// Bytes 4-11:  Timestamp (int64_t nanoseconds since epoch)
// Bytes 12-15: Message Type (int32_t, e.g., 1 for Ping, 2 for Pong)

using Timestamp = std::chrono::time_point<std::chrono::high_resolution_clock>;
using TimePointNs = std::chrono::nanoseconds; // Using nanoseconds for precision

class DW_Ping {
public:
    enum class Role {
        INITIATOR,  // ����Ping���ȴ�Pong�Լ���RTT
        RESPONDER   // ����Ping���ط�Pong
    };

    // ���캯��������һ��DDSManagerָ�롢��ɫ����������
    // ע�⣺����˫��ͨ�ţ�������Ҫ�������⣺һ������Ping��һ������Pong
    // �����Ϊʹ��һ�����⣬ͨ����Ϣ�������֡�
    DW_Ping(DDSManager_Bytes* manager, Role role, const char* topic_name);

    // ����������������Դ
    ~DW_Ping();

    // ����Ping��Ϣ�ĺ��ķ��� (���� INITIATOR ����)
    // ping_data.value ӦԤ�ȷ�������16�ֽ�
    DDS_ReturnCode_t send_ping(TestData& ping_data); // ���кź�ʱ������ڲ�����

    // ��ʼ����Ping���� (���� INITIATOR ����)
    void run_ping_test(int count = 1, int interval_ms = 1000);

    // ������̨�����߳� (RESPONDER ��Ҫ����)
    void start_listening();

    // ֹͣ��̨�����߳�
    void stop_listening();

    // ����Ƿ��ʼ���ɹ�
    bool is_valid() const {
        return (m_role == Role::INITIATOR) ? (m_ping_writer != nullptr && m_pong_reader != nullptr) :
            (m_ping_reader != nullptr && m_pong_writer != nullptr);
    }


private:
    Role m_role;
    DDSManager* m_manager; // ָ���ⲿDDSManager��ָ�룬����������������

    // DataWriters and DataReaders for Ping and Pong
    // INITIATOR uses: m_ping_writer, m_pong_reader
    // RESPONDER uses: m_ping_reader, m_pong_writer
    TestDataDataWriter* m_ping_writer;
    TestDataDataReader* m_ping_reader;
    TestDataDataWriter* m_pong_writer;
    TestDataDataReader* m_pong_reader;

    // Listeners (�����Ҫ�����ӵĻص��������Ա���)
    // DDS::DataReaderListener* m_ping_listener = nullptr;
    // DDS::DataReaderListener* m_pong_listener = nullptr;

    // For timing in INITIATOR
    std::mutex m_mutex;
    std::atomic<bool> m_running{ false };
    std::atomic<int> m_ping_count{ 0 };
    std::condition_variable m_cv;
    std::thread m_listener_thread; // Thread for listening in RESPONDER or background listening in INITIATOR

    // ���ÿ�������͸�ֵ������
    DW_Ping(const DW_Ping&) = delete;
    DW_Ping& operator=(const DW_Ping&) = delete;

    // ��ʼ��DataWriters and DataReaders based on role
    bool initialize(const char* topic_name);

    // Helper to pack data into TestData.value
    void pack_message(TestData& data, int32_t seq_num, TimePointNs timestamp, int32_t msg_type);
    // Helper to unpack data from TestData.value
    bool unpack_message(const TestData& data, int32_t& seq_num, TimePointNs& timestamp, int32_t& msg_type);

    // Internal handler for received messages
    void process_received_ping(const TestData& ping_data);
    void process_received_pong(const TestData& pong_data);

    // Background listening function
    void listen_loop();
};

#endif // DW_PING_H