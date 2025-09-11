// Main.cpp
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
#include "DDSManager.h"
#include "Config.h"
#include "Logger.h"
#include "GloMemPool.h"
#include "Throughput.h"
#include "MetricsReport.h"
#include "TestRoundResult.h"
#include "ResourceUtilization.h"

namespace {
    std::string json_file_path = GlobalConfig::DEFAULT_JSON_CONFIG_PATH;
    std::string qos_file_path = GlobalConfig::DEFAULT_QOS_XML_PATH;
    std::string logDir = GlobalConfig::LOG_DIRECTORY;
    std::string logPrefix = GlobalConfig::LOG_FILE_PREFIX;
    std::string logSuffix = GlobalConfig::LOG_FILE_SUFFIX;
    bool loggingEnabled = true;
}

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

        const ConfigData& base_config = config.getCurrentConfig();
        const int total_rounds = base_config.m_loopNum;

        Logger::getInstance().logAndPrint("\n=== 当前选中的配置模板 ===");
        std::ostringstream cfgStream;
        config.printCurrentConfig(cfgStream);
        Logger::getInstance().logAndPrint(cfgStream.str());

        if (total_rounds <= 0) {
            Logger::getInstance().logAndPrint("[Error] m_loopNum 必须大于 0");
            return EXIT_FAILURE;
        }

        Logger::getInstance().logAndPrint("开始执行 " + std::to_string(total_rounds) + " 轮测试...");

        // ================= 多轮测试主循环 =================
        int total_result = EXIT_SUCCESS;
        MetricsReport metricsReport;

        for (int round = 0; round < total_rounds; ++round) {
            Logger::getInstance().logAndPrint(
                "=== 第 " + std::to_string(round + 1) + "/" + std::to_string(total_rounds) +
                " 轮测试开始 (m_activeLoop=" + std::to_string(round) + ") ==="
            );

            // 创建本轮专用配置
            ConfigData current_cfg = base_config;
            current_cfg.m_activeLoop = round;

            // 打印本轮实际配置
            std::ostringstream roundCfgStream;
            Config::printConfigToStream(current_cfg, roundCfgStream);
            Logger::getInstance().logAndPrint(roundCfgStream.str());

            // 创建 DDSManager
            std::unique_ptr<DDSManager> dds_manager = std::make_unique<DDSManager>(current_cfg, qos_file_path);

            // 定义结果回调
            auto result_callback = [&metricsReport](const TestRoundResult& result) {
                metricsReport.addResult(result);
                };

            // 创建测试器
            Throughput tester(*dds_manager, result_callback);

            // 数据与结束回调（仅订阅者需要）
            OnDataReceivedCallback data_callback = [](const TestData& sample, const DDS::SampleInfo& info) {
                // 可扩展：数据校验等
                };

            OnEndOfRoundCallback end_callback = []() {
                // 已由 Throughput 内部处理
                };

            // 初始化
            bool init_success = current_cfg.m_isPositive
                ? dds_manager->initialize()
                : dds_manager->initialize(data_callback, end_callback);

            if (!init_success) {
                Logger::getInstance().logAndPrint("[Error] DDSManager 初始化失败（第 " + std::to_string(round + 1) + " 轮）");
                total_result = EXIT_FAILURE;
                break;
            }

            // ========== Publisher: 等待订阅者重新连接（同步点）==========
            if (current_cfg.m_isPositive && round > 0) {
                Logger::getInstance().logAndPrint("等待订阅者重新上线以启动第 " + std::to_string(round + 1) + " 轮...");
                if (!tester.waitForSubscriberReconnect(std::chrono::seconds(30))) {
                    Logger::getInstance().logAndPrint("警告：未检测到订阅者重连，超时继续...");
                }
            }

            // ========== 运行单轮测试 ==========
            int result = current_cfg.m_isPositive
                ? tester.runPublisher(current_cfg)
                : tester.runSubscriber(current_cfg);

            if (result == 0) {
                Logger::getInstance().logAndPrint("第 " + std::to_string(round + 1) + " 轮测试完成。");
            }
            else {
                Logger::getInstance().logAndPrint("第 " + std::to_string(round + 1) + " 轮测试发生错误。");
                total_result = EXIT_FAILURE;
            }

            // 清理资源
            dds_manager->shutdown();
            dds_manager.reset();

            // 防止端口冲突
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        // ================= 测试结束 =================
        Logger::getInstance().logAndPrint("\n--- 开始生成系统资源使用报告 ---");
        metricsReport.generateSummary();

        ResourceUtilization::instance().shutdown();
        GloMemPool::finalize();

        return total_result == EXIT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
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