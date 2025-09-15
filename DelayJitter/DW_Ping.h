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

// 假设 TestData 结构中使用 value 字段存储数据
// 约定格式 (使用前16字节):
// Bytes 0-3:   Sequence Number (int32_t)
// Bytes 4-11:  Timestamp (int64_t nanoseconds since epoch)
// Bytes 12-15: Message Type (int32_t, e.g., 1 for Ping, 2 for Pong)

using Timestamp = std::chrono::time_point<std::chrono::high_resolution_clock>;
using TimePointNs = std::chrono::nanoseconds; // Using nanoseconds for precision

class DW_Ping {
public:
    enum class Role {
        INITIATOR,  // 发起Ping并等待Pong以计算RTT
        RESPONDER   // 监听Ping并回发Pong
    };

    // 构造函数：接收一个DDSManager指针、角色和主题名称
    // 注意：对于双向通信，可能需要两个主题：一个用于Ping，一个用于Pong
    // 这里简化为使用一个主题，通过消息内容区分。
    DW_Ping(DDSManager_Bytes* manager, Role role, const char* topic_name);

    // 析构函数：清理资源
    ~DW_Ping();

    // 发送Ping消息的核心方法 (仅由 INITIATOR 调用)
    // ping_data.value 应预先分配至少16字节
    DDS_ReturnCode_t send_ping(TestData& ping_data); // 序列号和时间戳在内部设置

    // 开始运行Ping测试 (仅由 INITIATOR 调用)
    void run_ping_test(int count = 1, int interval_ms = 1000);

    // 启动后台监听线程 (RESPONDER 需要调用)
    void start_listening();

    // 停止后台监听线程
    void stop_listening();

    // 检查是否初始化成功
    bool is_valid() const {
        return (m_role == Role::INITIATOR) ? (m_ping_writer != nullptr && m_pong_reader != nullptr) :
            (m_ping_reader != nullptr && m_pong_writer != nullptr);
    }


private:
    Role m_role;
    DDSManager* m_manager; // 指向外部DDSManager的指针，不负责其生命周期

    // DataWriters and DataReaders for Ping and Pong
    // INITIATOR uses: m_ping_writer, m_pong_reader
    // RESPONDER uses: m_ping_reader, m_pong_writer
    TestDataDataWriter* m_ping_writer;
    TestDataDataReader* m_ping_reader;
    TestDataDataWriter* m_pong_writer;
    TestDataDataReader* m_pong_reader;

    // Listeners (如果需要更复杂的回调处理，可以保留)
    // DDS::DataReaderListener* m_ping_listener = nullptr;
    // DDS::DataReaderListener* m_pong_listener = nullptr;

    // For timing in INITIATOR
    std::mutex m_mutex;
    std::atomic<bool> m_running{ false };
    std::atomic<int> m_ping_count{ 0 };
    std::condition_variable m_cv;
    std::thread m_listener_thread; // Thread for listening in RESPONDER or background listening in INITIATOR

    // 禁用拷贝构造和赋值操作符
    DW_Ping(const DW_Ping&) = delete;
    DW_Ping& operator=(const DW_Ping&) = delete;

    // 初始化DataWriters and DataReaders based on role
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