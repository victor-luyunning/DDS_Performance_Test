// Main.cpp

// --- 1. 标准库头文件 ---
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <ctime>

// --- 项目头文件 ---
#include "Main.h"

namespace {
    std::string json_file_path = GlobalConfig::DEFAULT_JSON_CONFIG_PATH;
    std::string qos_file_path = GlobalConfig::DEFAULT_QOS_XML_PATH;
    std::string logDir = GlobalConfig::LOG_DIRECTORY;
    std::string logPrefix = GlobalConfig::LOG_FILE_PREFIX;
    std::string logSuffix = GlobalConfig::LOG_FILE_SUFFIX;
    bool loggingEnabled = true;
}

// --- 主函数 ---
int main() {
    try {
        // ================= 初始化全局内存池 =================
        if (!GloMemPool::initialize()) {
            std::cerr << "[Error] GloMemPool 初始化失败！" << std::endl;
            return EXIT_FAILURE;
        }
        Logger::getInstance().logAndPrint("[Memory] 使用 GloMemPool 管理全局内存");

        // ================= 初始化日志系统 =================
        Logger::setupLogger(logDir, logPrefix, logSuffix);

        // ================= 加载并选择配置 =================
        Config config(json_file_path);
        if (!config.promptAndSelectConfig(&Logger::getInstance())) {
            Logger::getInstance().logAndPrint("用户取消选择或配置加载失败");
            return EXIT_FAILURE;
        }

        Logger::getInstance().logAndPrint("\n=== 当前选中的配置 ===");
        std::ostringstream cfgStream;
        config.printCurrentConfig(cfgStream);
        Logger::getInstance().logAndPrint(cfgStream.str());

        const ConfigData& cfg = config.getCurrentConfig();

        // ================= 创建 DDSManager =================
        std::unique_ptr<DDSManager> dds_manager = std::make_unique<DDSManager>(cfg, qos_file_path);

        // ================= 创建测试策略对象 =================
        std::unique_ptr<runEntityInterface> tester = std::make_unique<Throughput>(*dds_manager);

        // ================= 定义回调函数 =================
        // 注意：这些回调在 initialize 时传入，用于通知 tester
        OnDataReceivedCallback data_callback = [](const TestData& sample, const DDS::SampleInfo& info) {
            // 可选：记录收到数据（通常 tester 内部统计即可）
            // Logger::getInstance().logAndPrint("[Subscriber] 收到一条数据");
            };

        OnEndOfRoundCallback end_callback = []() {
            // 通知 ThroughputTester 本轮结束
            // 实际逻辑在 tester 内部通过 condition_variable 唤醒
            // 这里可以空实现，因为 tester 已绑定状态
            // 如果 tester 需要感知，可通过成员函数触发
            };

        // ================= 初始化 DDSManager =================
        if (cfg.m_isPositive) {
            // Publisher：不需要数据回调
            if (!dds_manager->initialize()) {
                Logger::getInstance().logAndPrint("[Error] DDSManager 初始化失败（Publisher）");
                return EXIT_FAILURE;
            }
        }
        else {
            // Subscriber：需要接收数据和结束通知
            if (!dds_manager->initialize(data_callback, end_callback)) {
                Logger::getInstance().logAndPrint("[Error] DDSManager 初始化失败（Subscriber）");
                return EXIT_FAILURE;
            }
        }

        Logger::getInstance().logAndPrint("DDSManager 初始化成功，开始运行测试...");

        // ================= 启动测试 =================
        int result = cfg.m_isPositive
            ? tester->runPublisher(cfg)   // 发布者模式
            : tester->runSubscriber(cfg); // 订阅者模式

        if (result == 0) {
            Logger::getInstance().logAndPrint("测试运行完成，结果正常。");
        }
        else {
            Logger::getInstance().logAndPrint("测试运行中发生错误。");
        }

        // ================= 清理资源 =================
        dds_manager->shutdown();
        Logger::getInstance().logAndPrint("DDS 资源已清理");

        GloMemPool::finalize();
        GloMemPool::logStats();  // 输出内存池统计

        return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& e) {
        std::string errorMsg = "[Error] 异常: " + std::string(e.what());
        if (loggingEnabled) {
            Logger::getInstance().logAndPrint(errorMsg);
        }
        else {
            std::cerr << errorMsg << std::endl;
        }
        return EXIT_FAILURE;
    }
    catch (...) {
        std::string errorMsg = "[Error] 未捕获的异常";
        if (loggingEnabled) {
            Logger::getInstance().logAndPrint(errorMsg);
        }
        else {
            std::cerr << errorMsg << std::endl;
        }
        return EXIT_FAILURE;
    }
}