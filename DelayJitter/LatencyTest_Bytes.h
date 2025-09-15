// LatencyTest_Bytes.h
#pragma once

#include "DDSManager_Bytes.h"
#include "TestRoundResult.h" // 包含 TestRoundResult 定义
#include <functional>
#include <vector>
#include <set>
#include <chrono>

// 前向声明
class DDSManager_Bytes;

/**
 * @brief 时延测试类，用于测量 PING-PONG RTT。
 */
class LatencyTest_Bytes {
public:
    using ResultCallback = std::function<void(const TestRoundResult&)>;

    /**
     * @brief 构造函数
     * @param dds_manager 引用一个已配置的 DDSManager_Bytes 实例
     * @param callback 测试结果回调函数
     */
    LatencyTest_Bytes(DDSManager_Bytes& dds_manager, ResultCallback callback);

    /**
     * @brief 析构函数
     */
    ~LatencyTest_Bytes();

    // --- 主要功能接口 ---

    /**
     * @brief 运行作为 Initiator (发送 Ping)
     * @param config 当前轮次的配置
     * @return 0 成功，非0 失败
     */
    int runPublisher(const ConfigData& config);

    /**
     * @brief 运行作为 Responder (接收 Ping 并回复 Pong)
     * @param config 当前轮次的配置
     * @return 0 成功，非0 失败
     */
    int runSubscriber(const ConfigData& config);

    /**
     * @brief 收到 Pong 数据包时的回调
     * @param sample 数据样本
     * @param info 样本信息
     */
    void handlePongReceived(const DDS::Bytes& sample, const DDS::SampleInfo& info);

    /**
     * @brief 收到 Ping 数据包时的回调（由 Responder 调用）
     * @param sample 数据样本
     * @param info 样本信息
     */
    void onDataReceived(const DDS::Bytes& sample, const DDS::SampleInfo& info);

private:
    // --- 私有实现细节 ---
    struct Impl; // PIMPL 模式，隐藏内部实现
    std::unique_ptr<Impl> p_impl_;

    // --- 依赖 ---
    DDSManager_Bytes& dds_manager_;
    ResultCallback result_callback_;

    // --- 状态 ---
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point end_time_;
    std::vector<double> rtt_times_us_;      // 存储每次测得的RTT（微秒）
    std::set<uint32_t> received_sequences_; // 记录收到的Pong序列号，用于计算丢包

    // --- 内部方法 ---

    /**
     * @brief 同步等待 Responder 上线并匹配成功
     * @param timeout 超时时间
     * @return true 成功，false 超时或失败
     */
    bool waitForResponderReady(const std::chrono::seconds& timeout);

    /**
     * @brief 收到结束包时的回调
     */
    void onEndOfRound();

    /**
     * @brief 报告本轮测试结果
     * @param round_index 轮次索引
     * @param expected_count 预期发送数量
     * @param avg_packet_size 平均包大小
     */
    void report_results(int round_index, int expected_count, int avg_packet_size);
};
