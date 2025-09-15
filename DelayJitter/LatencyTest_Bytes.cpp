// LatencyTest_Bytes.cpp
#include "LatencyTest_Bytes.h" // 必须放在最前面
// 现在可以安全地包含这些头文件
#include "Logger.h"
#include "ResourceUtilization.h"
#include "SysMetrics.h"
#include "ZRDDSDataWriter.h"
#include "ZRDDSDataReader.h"
#include "ZRBuiltinTypes.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <numeric>

using namespace DDS;
using namespace std;

struct PacketHeader {
    uint32_t sequence;
    uint64_t timestamp;
    uint8_t  packet_type; // 0 = 数据包, 1 = 结束包
};

// ========================
// 实现细节 (Impl 结构体和 WriterListener)
// ========================

// 在 .cpp 文件中定义 Impl 结构体
struct LatencyTest_Bytes::Impl {
    // ========================
    // 内部类：WriterListener - 只在此 .cpp 文件可见
    // ========================
    class WriterListener : public virtual DDS::DataWriterListener {
    public:
        WriterListener(std::atomic<bool>& flag, std::mutex& mtx, std::condition_variable& cv)
            : reconnected_flag_(flag), mutex_(mtx), cond_var_(cv), last_current_count_(0) {
        }
        void on_liveliness_lost(DDS::DataWriter*, const LivelinessLostStatus&) override {}
        void on_offered_deadline_missed(DDS::DataWriter*, const OfferedDeadlineMissedStatus&) override {}
        void on_offered_incompatible_qos(DDS::DataWriter*, const OfferedIncompatibleQosStatus&) override {}
        void on_publication_matched(DDS::DataWriter*, const PublicationMatchedStatus&) override {}

        void on_subscription_matched(DDS::DataWriter*, const SubscriptionMatchedStatus& status) {
            int32_t current = status.current_count;
            int32_t previous = last_current_count_.load();
            last_current_count_.store(current);
            if (current > 0 && previous == 0) {
                std::lock_guard<std::mutex> lock(mutex_);
                reconnected_flag_.store(true);
                cond_var_.notify_all();
                Logger::getInstance().logAndPrint("LatencyTest_Bytes: 检测到 Responder 重新上线");
            }
        }

    private:
        std::atomic<bool>& reconnected_flag_;
        std::mutex& mutex_;
        std::condition_variable& cond_var_;
        std::atomic<int32_t> last_current_count_;
    };

    // Impl 的成员
    std::unique_ptr<WriterListener> writer_listener_;
    std::atomic<bool> responder_reconnected_{ false };
    std::mutex reconnect_mtx_;
    std::condition_variable reconnect_cv_;

    // +++ 新增：用于控制 runSubscriber 退出 +++
    std::atomic<bool> end_of_round_received_{ false };
    std::mutex end_mtx_;
    std::condition_variable end_cv_;
};

// ========================
// 构造函数 & 析构
// ========================

LatencyTest_Bytes::LatencyTest_Bytes(DDSManager_Bytes& dds_manager, ResultCallback callback)
    : dds_manager_(dds_manager)
    , result_callback_(std::move(callback))
{
    // 创建 Impl 对象
    p_impl_ = std::make_unique<Impl>();
    auto* ping_writer = dynamic_cast<ZRDDSDataWriter<DDS::Bytes>*>(dds_manager_.get_Ping_data_writer());
    if (ping_writer) {
        // 创建 Listener
        p_impl_->writer_listener_ = std::make_unique<Impl::WriterListener>(
            p_impl_->responder_reconnected_,
            p_impl_->reconnect_mtx_,
            p_impl_->reconnect_cv_
        );
        ReturnCode_t ret = ping_writer->set_listener(p_impl_->writer_listener_.get(), DDS::SUBSCRIPTION_MATCHED_STATUS);
        if (ret != DDS::RETCODE_OK) {
            Logger::getInstance().logAndPrint("警告：无法为 Ping DataWriter 设置监听器");
        }
    }
}

LatencyTest_Bytes::~LatencyTest_Bytes() = default; // 默认析构即可，unique_ptr 会自动清理

// ========================
// 同步函数：等待 Responder 上线
// ========================

bool LatencyTest_Bytes::waitForResponderReady(const std::chrono::seconds& timeout) {
    if (!p_impl_) return false; // 防御性编程
    auto* ping_writer = dynamic_cast<ZRDDSDataWriter<DDS::Bytes>*>(dds_manager_.get_Ping_data_writer());
    if (!ping_writer) {
        Logger::getInstance().error("LatencyTest_Bytes: Ping DataWriter 为空");
        return false;
    }
    auto start = std::chrono::steady_clock::now();
    auto end = start + timeout;
    while (std::chrono::steady_clock::now() < end) {
        DDS::PublicationMatchedStatus status{};
        ReturnCode_t ret = ping_writer->get_publication_matched_status(status);
        if (ret == RETCODE_OK) {
            Logger::getInstance().logAndPrint(
                "LatencyTest_Bytes: 等待 Responder | wait match(" +
                std::to_string(status.current_count) + "/1)"
            );
            if (status.current_count >= 1) { // >=1 表示至少有一个匹配
                Logger::getInstance().logAndPrint("检测到 Responder 已上线并匹配成功");
                return true;
            }
        }
        else {
            Logger::getInstance().logAndPrint("Error: 获取 Publication Matched 状态失败");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    Logger::getInstance().logAndPrint("等待 Responder 上线超时 (" +
        std::to_string(timeout.count()) + "s)");
    return false;
}

// ========================
// 回调函数
// ========================

void LatencyTest_Bytes::onDataReceived(const DDS::Bytes& sample, const DDS::SampleInfo& info) {
    if (!info.valid_data) return;
    const uint8_t* buffer = sample.value.get_contiguous_buffer();
    if (!buffer || sample.value.length() < sizeof(PacketHeader)) return;
    const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(buffer);

    if (hdr->packet_type == 1) { // 是结束包
        Logger::getInstance().logAndPrint(
            "[onDataReceived] 收到结束包 | seq=" + std::to_string(hdr->sequence) +
            " | ts=" + std::to_string(hdr->timestamp) + " | length=" + std::to_string(sample.value.length())
        );

        // === 回复一个 Pong 类型的结束包 ===
        Bytes pong_end_sample;
        dds_manager_.cleanupBytesData(pong_end_sample);
        if (dds_manager_.prepareEndBytesData(pong_end_sample, static_cast<int>(sample.value._length))) {
            ZRDDSDataWriter<DDS::Bytes>* pong_writer =
                dynamic_cast<ZRDDSDataWriter<DDS::Bytes>*>(dds_manager_.get_Pong_data_writer());
            if (pong_writer) {
                ReturnCode_t ret = pong_writer->write(pong_end_sample, DDS_HANDLE_NIL_NATIVE);
                if (ret != RETCODE_OK) {
                    Logger::getInstance().error("Pong End write failed: " + to_string(ret));
                }
                else {
                    Logger::getInstance().logAndPrint("已回复 Pong 结束包");
                }
            }
        }
        dds_manager_.cleanupBytesData(pong_end_sample);

        // === 触发 onEndOfRound 并通知 runSubscriber 退出 ===
        this->onEndOfRound(); // 调用自己的 onEndOfRound

        // 设置标志位，让 runSubscriber 退出循环
        p_impl_->end_of_round_received_.store(true);
        p_impl_->end_cv_.notify_one();
        return;
    }

    // 否则就是普通 Ping 包，正常回复 Pong
    Bytes pong_sample;
    dds_manager_.cleanupBytesData(pong_sample);
    size_t reply_size = sample.value._length;
    if (dds_manager_.prepareBytesData(pong_sample,
        static_cast<int>(reply_size), static_cast<int>(reply_size),
        hdr->sequence, hdr->timestamp)) {
        PacketHeader* out_hdr = reinterpret_cast<PacketHeader*>(
            pong_sample.value.get_contiguous_buffer());
        out_hdr->packet_type = 0; // 普通数据包

        ZRDDSDataWriter<DDS::Bytes>* pong_writer =
            dynamic_cast<ZRDDSDataWriter<DDS::Bytes>*>(dds_manager_.get_Pong_data_writer());
        if (pong_writer) {
            ReturnCode_t ret = pong_writer->write(pong_sample, DDS_HANDLE_NIL_NATIVE);
            if (ret != RETCODE_OK) {
                Logger::getInstance().error("Pong write failed: " + to_string(ret));
            }
        }
    }
    dds_manager_.cleanupBytesData(pong_sample);

    // 可选日志
    static int count = 0;
    if (++count % 10000 == 0) {
        Logger::getInstance().logAndPrint("已回复 " + to_string(count) + " 个 Pong");
    }
}

void LatencyTest_Bytes::onEndOfRound() {
    Logger::getInstance().logAndPrint("[onEndOfRound] 收到结束信号，准备退出本轮");
    // 这里可以添加其他清理逻辑
}

// ========================
// 报告结果
// ========================

void LatencyTest_Bytes::report_results(int round_index, int expected_count, int avg_packet_size) {
    int received = static_cast<int>(received_sequences_.size());
    int lost = expected_count - received;
    double loss_rate = expected_count > 0 ? (double)lost / expected_count * 100.0 : 0.0;
    if (rtt_times_us_.empty()) {
        Logger::getInstance().logAndPrint("警告：未收到任何 Pong 回包");
        return;
    }
    double min_rtt = *min_element(rtt_times_us_.begin(), rtt_times_us_.end());
    double max_rtt = *max_element(rtt_times_us_.begin(), rtt_times_us_.end());
    double avg_rtt = accumulate(rtt_times_us_.begin(), rtt_times_us_.end(), 0.0) / rtt_times_us_.size();
    ostringstream oss;
    oss << fixed << setprecision(2)
        << "时延测试结果 | 第 " << (round_index + 1) << " 轮 | "
        << "发送: " << expected_count << " | "
        << "收到: " << received << " | "
        << "丢包: " << lost << " (" << loss_rate << "%) | "
        << "Avg RTT: " << avg_rtt << " μs | "
        << "Min RTT: " << min_rtt << " μs | "
        << "Max RTT: " << max_rtt << " μs";
    Logger::getInstance().logAndPrint(oss.str());
}

// ========================
// runPublisher - Initiator: 发送 Ping 并接收 Pong
// ========================

int LatencyTest_Bytes::runPublisher(const ConfigData& config) {
    using WriterType = ZRDDSDataWriter<DDS::Bytes>;
    using ReaderType = ZRDDSDataReader<DDS::Bytes, DDS::BytesSeq>;
    WriterType* ping_writer = dynamic_cast<WriterType*>(dds_manager_.get_Ping_data_writer());
    ReaderType* pong_reader = dynamic_cast<ReaderType*>(dds_manager_.get_Pong_data_reader());
    if (!ping_writer) {
        Logger::getInstance().error("LatencyTest_Bytes: Ping DataWriter 为空");
        return -1;
    }
    const int round_index = config.m_activeLoop;
    const int min_size = config.m_minSize[round_index];
    const int max_size = config.m_maxSize[round_index];
    const int send_count = config.m_sendCount[round_index];
    const int print_gap = config.m_sendPrintGap[round_index];

    // ✅ 使用标准模式等待 Responder 上线
    if (!waitForResponderReady(std::chrono::seconds(10))) {
        Logger::getInstance().logAndPrint("LatencyTest_Bytes: 等待 Responder 上线失败");
        return -1;
    }

    ostringstream oss;
    oss << "第 " << (round_index + 1) << " 轮时延测试（Initiator）| 发送: " << send_count
        << " 次 | 数据大小: [" << min_size << ", " << max_size << "]";
    Logger::getInstance().logAndPrint(oss.str());

    auto& resUtil = ResourceUtilization::instance();
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();
    start_time_ = chrono::steady_clock::now();
    Bytes ping_sample;
    rtt_times_us_.clear();
    received_sequences_.clear();

    // 🔴 删除了这里多余的 initialize_latency 调用！
    // 初始化已在 main.cpp 中完成

    int sent = 0;
    for (int i = 0; i < send_count; ++i) {
        uint64_t send_timestamp_us = chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now().time_since_epoch()
        ).count();
        if (!dds_manager_.prepareBytesData(ping_sample, min_size, max_size, i, send_timestamp_us)) {
            Logger::getInstance().error("准备第 " + to_string(i) + " 个 Ping 包失败");
            continue;
        }
        PacketHeader* hdr = reinterpret_cast<PacketHeader*>(ping_sample.value.get_contiguous_buffer());
        hdr->packet_type = 0; // DATA_PACKET

        ReturnCode_t ret = ping_writer->write(ping_sample, DDS_HANDLE_NIL_NATIVE);
        if (ret == RETCODE_OK) {
            ++sent;
            if (sent % print_gap == 0) {
                Logger::getInstance().logAndPrint("已发送 " + to_string(sent) + " 个 Ping");
            }
        }
        else {
            Logger::getInstance().error("Ping write failed: " + to_string(ret));
        }
        dds_manager_.cleanupBytesData(ping_sample);
        // 这里可以添加 sendDelay 逻辑
        this_thread::sleep_for(chrono::microseconds(0));
    }

    // ✅ 发送结束包（通知 Responder 本轮结束）===
    Bytes end_sample;
    if (dds_manager_.prepareEndBytesData(end_sample, min_size)) {
        ReturnCode_t ret = ping_writer->write(end_sample, DDS_HANDLE_NIL_NATIVE);
        if (ret == RETCODE_OK) {
            Logger::getInstance().logAndPrint("已发送结束包，通知 Responder 本轮结束");
        }
        else {
            Logger::getInstance().error("发送结束包失败: " + to_string(ret));
        }
    }
    dds_manager_.cleanupBytesData(end_sample);

    // ✅ 等待所有回复（简单休眠，实际应用中可更复杂）
    this_thread::sleep_for(chrono::seconds(5));
    end_time_ = chrono::steady_clock::now();
    SysMetrics end_metrics = resUtil.collectCurrentMetrics();

    // 📊 计算并打印时延统计
    report_results(round_index, send_count, (min_size + max_size) / 2);

    // 如果需要上报资源，取消注释下行
    // if (result_callback_) { result_callback_(TestRoundResult{ round_index + 1, start_metrics, end_metrics }); }
    return 0;
}

// ========================
// handlePongReceived - 处理从 Pong Reader 收到的数据
// ========================

void LatencyTest_Bytes::handlePongReceived(const DDS::Bytes& sample, const DDS::SampleInfo& info) {
    if (!info.valid_data) return;
    const uint8_t* buffer = sample.value.get_contiguous_buffer();
    if (!buffer || sample.value.length() < sizeof(PacketHeader)) return;
    const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(buffer);

    if (hdr->packet_type == 1) { // 是结束包
        Logger::getInstance().logAndPrint("[handlePongReceived] 收到来自 Responder 的 Pong 结束包");
        return; // 不做特殊处理，只是确认对方收到了
    }

    uint64_t recv_time_us = chrono::duration_cast<chrono::microseconds>(
        chrono::steady_clock::now().time_since_epoch()
    ).count();
    int64_t rtt = static_cast<int64_t>(recv_time_us - hdr->timestamp);
    if (rtt > 0) {
        rtt_times_us_.push_back(static_cast<double>(rtt));
    }
    received_sequences_.insert(hdr->sequence);
}

// ========================
// runSubscriber - Responder: 接收 Ping 并自动回 Pong
// ========================

// LatencyTest_Bytes.cpp - 修复后的 runSubscriber
int LatencyTest_Bytes::runSubscriber(const ConfigData& config) {
    using ReaderType = ZRDDSDataReader<DDS::Bytes, DDS::BytesSeq>;
    ReaderType* ping_reader = dynamic_cast<ReaderType*>(dds_manager_.get_Ping_data_reader());
    if (!ping_reader) {
        Logger::getInstance().error("LatencyTest_Bytes: Ping DataReader 为空");
        return -1;
    }

    const int round_index = config.m_activeLoop;
    const int expected_count = config.m_sendCount[round_index];
    const int print_gap = config.m_recvPrintGap[round_index];

    // --- 等待 Initiator 上线 ---
    while (true) {
        SubscriptionMatchedStatus status{};
        if (ping_reader->get_subscription_matched_status(status) == RETCODE_OK) {
            Logger::getInstance().logAndPrint(
                "LatencyTest_Bytes: 等待 Initiator | wait match(" +
                std::to_string(status.current_count) + "/1)"
            );
            if (status.current_count > 0) {
                Logger::getInstance().logAndPrint("检测到 Initiator 已上线");
                break;
            }
        }
        else {
            Logger::getInstance().logAndPrint("Error: 获取 Subscription Matched 状态失败");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // ✅ 在可能收到数据前，先重置状态
    p_impl_->end_of_round_received_.store(false); // 确保标志位干净

    Logger::getInstance().logAndPrint("第 " + to_string(round_index + 1) + " 轮时延测试开始（Responder 模式）");

    auto& resUtil = ResourceUtilization::instance();
    SysMetrics start_metrics = resUtil.collectCurrentMetrics();

    // ✅ 先检查一次，防止 notify 提前发生
    if (p_impl_->end_of_round_received_.load()) {
        Logger::getInstance().logAndPrint("在开始等待前已收到结束包，立即退出");
        // 直接返回，不执行后续等待
        SysMetrics end_metrics = resUtil.collectCurrentMetrics();
        // 如果需要回调结果，可以在这里调用 result_callback_
        return 0;
    }

    // ✅ 开始等待，但增加一个快速轮询机制以防万一
    const auto start_wait = std::chrono::steady_clock::now();
    const auto max_wait_time = std::chrono::seconds(30);

    while (std::chrono::steady_clock::now() - start_wait < max_wait_time) {
        std::unique_lock<std::mutex> lock(p_impl_->end_mtx_, std::try_to_lock);
        if (lock.owns_lock()) {
            if (p_impl_->end_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return p_impl_->end_of_round_received_.load();
                })) {
                Logger::getInstance().logAndPrint("成功收到结束包，本轮 Responder 正常退出");
                goto EXIT;
            }
        }
        else {
            // 锁被占用，可能是 onDataReceived 正在设置标志
            // 短暂休眠，避免忙等
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // 额外保险：直接检查原子变量（无锁）
        if (p_impl_->end_of_round_received_.load()) {
            Logger::getInstance().logAndPrint("无需加锁，检测到结束包，退出");
            goto EXIT;
        }
    }

    Logger::getInstance().logAndPrint("等待结束包超时，强制退出本轮");

EXIT:
    SysMetrics end_metrics = resUtil.collectCurrentMetrics();
    return 0;
}