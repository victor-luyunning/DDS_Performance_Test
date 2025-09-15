#pragma once
// DR_Pong.h
#ifndef DR_PONG_H
#define DR_PONG_H

#include "DDSManager.h"

#include<mutex>

class DR_Pong {
public:
    DR_Pong(DDSManager* manager, const char* topic_name);
    ~DR_Pong();

    bool is_valid() const { return mPong_datareader != nullptr && mPong_datawriter != nullptr; }

    // 核心：在 on_data_available 中调用此函数处理收到的 Ping 并立即发回
    void handle_ping(const TestData& ping_data);

	void runPong();
private:
    DDSManager* m_manager;
    DDS::Topic* m_topic;
    TestDataDataReader* mPong_datareader;
    TestDataDataWriter* mPong_datawriter;

    DR_Pong(const DR_Pong&) = delete;
    DR_Pong& operator=(const DR_Pong&) = delete;
    std::mutex m_mutex;

    bool initialize();
};

#endif // DR_PONG_H