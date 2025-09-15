// DR_Pong.cpp
#include "DR_Pong.h"
#include <iostream>

DR_Pong::DR_Pong(DDSManager* manager, const char* topic_name)
    : m_manager(manager), m_topic(nullptr), mPong_datareader(nullptr), mPong_datawriter(nullptr) {

    if (!m_manager) {
        std::cerr << "Error: DDSManager pointer is null!" << std::endl;
        return;
    }

    if (!initialize()) {
        std::cerr << "Error: Failed to initialize DR_Pong!" << std::endl;
    }
}

DR_Pong::~DR_Pong() {
    // 通常由 DDSManager 统一管理生命周期，这里可不删，或按需删
    // 注意：如果 DDSManager 在析构时已清理，则这里不要再删，避免 double-free
}

bool DR_Pong::initialize() {
    // 获取 DataReader 和 DataWriter
    mPong_datareader = dynamic_cast<TestDataDataReader*>(m_manager->get_data_reader());
    mPong_datawriter = dynamic_cast<TestDataDataWriter*>(m_manager->get_data_writer());

    if (!mPong_datareader) {
        std::cerr << "Error: Failed to get DataReader!" << std::endl;
        return false;
    }
    if (!mPong_datawriter) {
        std::cerr << "Error: Failed to get DataWriter!" << std::endl;
        return false;
    }

    return true;
}

void DR_Pong::handle_ping(const TestData& ping_data) {
    if (!mPong_datawriter) {
        std::cerr << "Warning: DataWriter not ready in DR_Pong!" << std::endl;
        return;
    }

    // 可选：可以修改数据，比如设置 is_pong = true，或添加回显标记
    // 但通常直接原样发回即可，让 Ping 端通过序列号匹配

    DDS::InstanceHandle_t handle = mPong_datawriter->register_instance(ping_data);
    DDS_ReturnCode_t ret = mPong_datawriter->write(ping_data, handle);

    if (ret != DDS_RETCODE_OK) {
        std::cerr << "Warning: Failed to send Pong!" << std::endl;
    }
}

void DR_Pong::runPong() {
    if (!mPong_datareader) {
        std::cerr << "Error: DataReader not initialized!" << std::endl;
        return;
    }

    DelayDataCallback callback = [this](const TestData& received_data) {
        if (!mPong_datawriter) return;

        // ✅ 立即回写，不 register_instance（除非必要）
        DDS_ReturnCode_t ret = mPong_datawriter->write(received_data, DDS_HANDLE_NIL);
        if (ret != DDS_RETCODE_OK) {
            std::cerr << "Failed to send Pong response!" << std::endl;
        }
        // else: silent success for performance
        };

    // ✅ 创建并设置 Listener
    DDS::DataReaderListener* listener = new DDSManager::DelayDataReaderListener(callback);
    mPong_datareader->set_listener(listener, DDS_DATA_AVAILABLE_STATUS);

    std::cout << "Pong listener started. Waiting for Ping..." << std::endl;

    // 👉 注意：这里不阻塞，由外部主线程保持程序运行
}
