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

#include "Main.h"

namespace {
    std::atomic<bool> g_data_received{ false };
    std::string json_file_path = GlobalConfig::DEFAULT_JSON_CONFIG_PATH;
    std::string qos_file_path = GlobalConfig::DEFAULT_QOS_XML_PATH;
    std::string logDir = GlobalConfig::LOG_DIRECTORY;
    std::string logPrefix = GlobalConfig::LOG_FILE_PREFIX;
    std::string logSuffix = GlobalConfig::LOG_FILE_SUFFIX;
    bool loggingEnabled = true;
}

// --- Subscriber 回调函数 ---
static void subscriber_callback(const TestData* sample) {
    if (sample) {
        DDS_ULong data_len = sample->value._length;
        std::string logMsg = "[Subscriber Callback] 收到数据，长度: " + std::to_string(data_len);
        Logger::getInstance().logAndPrint(logMsg);
    }
    g_data_received.store(true);
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
        Config config(json_file_path);
        

        // ================= 与用户交互选择配置 =================
        if (!config.promptAndSelectConfig(&Logger::getInstance())) {
            Logger::getInstance().logAndPrint("用户选择配置失败或取消");
            return EXIT_FAILURE;
        }
        Logger::getInstance().logAndPrint("\n=== 当前选中的配置 ===");
        std::ostringstream cfgStream;
        config.printCurrentConfig(cfgStream);
        Logger::getInstance().logAndPrint(cfgStream.str());

        const auto& cfg = config.getCurrentConfig();


        // ================= 创建 DDSManager 并初始化 =================
        std::unique_ptr<DDSManager> dds_manager = std::make_unique<DDSManager>(cfg, qos_file_path);

        OnDataAvailableCallback callback = nullptr;
        if (!cfg.m_isPositive) {
            // Subscriber 需要回调（虽然 runSubscriber 内部用了临时 listener，但 initialize 时最好传入）
            callback = subscriber_callback;
        }

        if (!dds_manager->initialize(callback)) {
            Logger::getInstance().logAndPrint("[Error] DDSManager 初始化失败！");
            return EXIT_FAILURE;
        }

        Logger::getInstance().logAndPrint("DDSManager 初始化成功，开始运行...");

        // ========== 根据角色启动相应模式 ==========
        int result = cfg.m_isPositive
            ? dds_manager->runPublisher(100, 100)  
            : dds_manager->runSubscriber();         

        // ================= 清理资源 =================
        dds_manager->shutdown();
        Logger::getInstance().logAndPrint("DDS 资源清理完成");

        GloMemPool::finalize();
        GloMemPool::logStats(); // 输出最终内存使用统计，便于性能分析

        // 可选：防止闪退（仅调试时有用）
        // std::cout << "\n按回车键退出..." << std::endl;
        // std::cin.get();

        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::string errorMsg = "[Error] " + std::string(e.what());
        if (loggingEnabled) {
            Logger::getInstance().logAndPrint(errorMsg);
        }
        else {
            std::cerr << errorMsg << std::endl;
        }
        return EXIT_FAILURE;
    }
    catch (...) {
        std::string errorMsg = "[Error] 程序发生未捕获的异常";
        if (loggingEnabled) {
            Logger::getInstance().logAndPrint(errorMsg);
        }
        else {
            std::cerr << errorMsg << std::endl;
        }
        return EXIT_FAILURE;
    }
}