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
#include "DDSManager_Bytes.h"
#include "DDSManager_ZeroCopyBytes.h"
#include "Config.h"
#include "Logger.h"
#include "GloMemPool.h"
#include "Throughput_Bytes.h"
#include "Throughput_ZeroCopyBytes.h"  
#include "LatencyTest_Bytes.h"
#include "MetricsReport.h"
#include "TestRoundResult.h"
#include "ResourceUtilization.h"

namespace {
    std::string json_file_path = GlobalConfig::DEFAULT_JSON_CONFIG_PATH;
    std::string qos_file_path = GlobalConfig::DEFAULT_QOS_XML_PATH;
    std::string logDir = GlobalConfig::LOG_DIRECTORY;
    std::string logPrefix = GlobalConfig::LOG_FILE_PREFIX;
    std::string logSuffix = GlobalConfig::LOG_FILE_SUFFIX;
    std::string resultDir = GlobalConfig::DEFAULT_RESULT_PATH;
    bool loggingEnabled = true;
}

int main() {
    try {
        // ================= 初始化全局内存池 =================
        if (!GloMemPool::initialize()) {
            std::cerr << "[Error] GloMemPool 初始化失败！" << std::endl;
            std::cin.get(); // 等待用户按键，防止窗口关闭
            return EXIT_FAILURE;
        }
        Logger::getInstance().logAndPrint("[Memory] 使用 GloMemPool 管理全局内存");

        // ================= 初始化日志系统 =================
        Logger::setupLogger(logDir, logPrefix, logSuffix);

        // ================= 加载并选择配置 =================
        Config config(json_file_path);
        if (!config.promptAndSelectConfig(&Logger::getInstance())) {
            Logger::getInstance().logAndPrint("用户取消选择或配置加载失败");
            std::cin.get(); // 等待用户按键，防止窗口关闭
            return EXIT_FAILURE;
        }

        const ConfigData& base_config = config.getCurrentConfig();
        const int total_rounds = base_config.m_loopNum;

        Logger::getInstance().logAndPrint("\n=== 当前选中的配置模板 ===");
        std::ostringstream cfgStream;
        config.printCurrentConfig(cfgStream);
        Logger::getInstance().logAndPrint(cfgStream.str());

        // ==================== 新增：解析测试类型（吞吐 or 时延）====================
        std::string config_name = base_config.name;
        bool is_throughput_test = false;
        bool is_latency_test = false;

        if (config_name.rfind("tp::", 0) == 0) {
            is_throughput_test = true;
            Logger::getInstance().logAndPrint("[Test Mode] 吞吐测试模式 (tp::)");
        }
        else if (config_name.rfind("delay::", 0) == 0) {
            is_latency_test = true;
            Logger::getInstance().logAndPrint("[Test Mode] 时延测试模式 (delay::)");
        }
        else {
            Logger::getInstance().error("[Test Mode] 配置名 '" + config_name + "' 必须以 'tp::' 或 'delay::' 开头！");
            std::cin.get();
            return EXIT_FAILURE;
        }

        if (total_rounds <= 0) {
            Logger::getInstance().logAndPrint("[Error] m_loopNum 必须大于 0");
            std::cin.get();
            return EXIT_FAILURE;
        }

        Logger::getInstance().logAndPrint("开始执行 " + std::to_string(total_rounds) + " 轮测试...");

        // ==================== 根据传输模式选择 Bytes 或 ZeroCopy ====================
        bool is_zero_copy_mode = (base_config.m_typeName == "DDS::ZeroCopyBytes");

        // --- 定义所有可能需要的管理器和测试对象 ---
        std::unique_ptr<DDSManager_Bytes> bytes_manager;
        std::unique_ptr<DDSManager_ZeroCopyBytes> zc_manager;

        std::unique_ptr<Throughput_Bytes> throughput_bytes;
        std::unique_ptr<Throughput_ZeroCopyBytes> throughput_zc;

        std::unique_ptr<LatencyTest_Bytes> latency_test_bytes;

        MetricsReport metricsReport;

        // ========== 主循环：多轮测试 ==========
        int total_result = EXIT_SUCCESS;

        for (int round = 0; round < total_rounds; ++round) {
            Logger::getInstance().logAndPrint(
                "=== 第 " + std::to_string(round + 1) + "/" + std::to_string(total_rounds) +
                " 轮测试开始 (m_activeLoop=" + std::to_string(round) + ") ==="
            );

            // 创建本轮配置副本
            ConfigData current_cfg = base_config;
            current_cfg.m_activeLoop = round;

            // 打印本轮参数
            std::ostringstream roundCfgStream;
            Config::printConfigToStream(current_cfg, roundCfgStream);
            Logger::getInstance().logAndPrint(roundCfgStream.str());

            // ------------------- 第一步：创建 DDSManager（如果尚未创建）-------------------
            if (round == 0) {
                if (is_zero_copy_mode) {
                    zc_manager = std::make_unique<DDSManager_ZeroCopyBytes>(current_cfg, qos_file_path);
                    if (is_throughput_test) {
                        throughput_zc = std::make_unique<Throughput_ZeroCopyBytes>(
                            *zc_manager,
                            [&metricsReport](const TestRoundResult& result) {
                                metricsReport.addResult(result);
                            }
                        );
                    }
                    // TODO: if (is_latency_test) latency_test_zc = ...
                }
                else {
                    bytes_manager = std::make_unique<DDSManager_Bytes>(current_cfg, qos_file_path);
                    if (is_throughput_test) {
                        throughput_bytes = std::make_unique<Throughput_Bytes>(
                            *bytes_manager,
                            [&metricsReport](const TestRoundResult& result) {
                                metricsReport.addResult(result);
                            }
                        );
                    }
                    else if (is_latency_test) {
                        latency_test_bytes = std::make_unique<LatencyTest_Bytes>(
                            *bytes_manager,
                            [&metricsReport](const TestRoundResult& result) {
                                metricsReport.addResult(result);
                            }
                        );
                    }
                }

                // ==================== 初始化 ResourceUtilization（仅一次）====================
                if (!ResourceUtilization::instance().initialize()) {
                    Logger::getInstance().logAndPrint("[Warning] ResourceUtilization 初始化失败！CPU 监控可能无效。");
                }
                else {
                    Logger::getInstance().logAndPrint("[Resource] ResourceUtilization 初始化成功");
                }
            }

            // ------------------- 第二步：重新初始化 DDSManager（每轮都要）-------------------
            bool init_success = false;

            if (is_throughput_test) {
                if (is_zero_copy_mode) {
                    if (current_cfg.m_isPositive) {
                        init_success = zc_manager->initialize();
                    }
                    else {
                        auto end_callback = [&]() { throughput_zc->onEndOfRound(); };
                        init_success = zc_manager->initialize(
                            [&](const DDS::ZeroCopyBytes& sample, const DDS::SampleInfo& info) {
                                throughput_zc->onDataReceived(sample, info);
                            },
                            end_callback
                        );
                    }
                }
                else {
                    if (current_cfg.m_isPositive) {
                        init_success = bytes_manager->initialize();
                    }
                    else {
                        auto end_callback = [&]() { throughput_bytes->onEndOfRound(); };
                        init_success = bytes_manager->initialize(
                            [&](const DDS::Bytes& sample, const DDS::SampleInfo& info) {
                                throughput_bytes->onDataReceived(sample, info);
                            },
                            end_callback
                        );
                    }
                }
            }
            else if (is_latency_test) {
                // 注意：LatencyTest_Bytes 内部会在 runXXX 中调用 initialize_latency()
                // 所以这里不需要提前初始化
                // 我们只做一次 setup
                if (round == 0) {
                    if (is_zero_copy_mode) {
                        // 假设有 LatencyTest_ZeroCopyBytes
                        // latency_test_zc = std::make_unique<LatencyTest_ZeroCopyBytes>(*zc_manager, ...);
                    }
                    else {
                        latency_test_bytes = std::make_unique<LatencyTest_Bytes>(
                            *bytes_manager,
                            [&metricsReport](const TestRoundResult& result) {
                                metricsReport.addResult(result);
                            }
                        );
                    }
                }
                init_success = true; // initialize_latency 由 LatencyTest 内部负责
            }

            if (!init_success && is_throughput_test) {
                Logger::getInstance().logAndPrint("[Error] DDSManager 初始化失败（第 " + std::to_string(round + 1) + " 轮）");
                total_result = EXIT_FAILURE;
                break;
            }

            // ------------------- 第三步：等待重连（Publisher 角色 + 非首轮回合）-------------------
            if (current_cfg.m_isPositive && round > 0) {
                Logger::getInstance().logAndPrint("等待订阅者重新上线...");

                bool connected = false;
                if (is_throughput_test) {
                    connected = is_zero_copy_mode ?
                        throughput_zc->waitForSubscriberReconnect(std::chrono::seconds(1)) :
                        throughput_bytes->waitForSubscriberReconnect(std::chrono::seconds(1));
                }
                // 时延模式不使用此机制（由 LatencyTest 自行处理连接）
                if (!connected && is_throughput_test) {
                    Logger::getInstance().logAndPrint("警告：未检测到订阅者重连，超时继续...");
                }
            }

            // ------------------- 第四步：运行单轮测试 -------------------
            int result = 0;

            if (current_cfg.m_isPositive) {
                if (is_throughput_test) {
                    result = is_zero_copy_mode ?
                        throughput_zc->runPublisher(current_cfg) :
                        throughput_bytes->runPublisher(current_cfg);
                }
                else if (is_latency_test) {
                    result = is_zero_copy_mode ?
                        /*latency_test_zc->runPublisher(current_cfg)*/ 0 :  // 未实现
                        latency_test_bytes->runPublisher(current_cfg);
                }
            }
            else {
                if (is_throughput_test) {
                    result = is_zero_copy_mode ?
                        throughput_zc->runSubscriber(current_cfg) :
                        throughput_bytes->runSubscriber(current_cfg);
                }
                else if (is_latency_test) {
                    result = is_zero_copy_mode ?
                        /*latency_test_zc->runSubscriber(current_cfg)*/ 0 :
                        latency_test_bytes->runSubscriber(current_cfg);
                }
            }

            if (result == 0) {
                Logger::getInstance().logAndPrint("第 " + std::to_string(round + 1) + " 轮测试完成。");
            }
            else {
                Logger::getInstance().logAndPrint("第 " + std::to_string(round + 1) + " 轮测试发生错误。");
                total_result = EXIT_FAILURE;
            }

            // ------------------- 第五步：清理本轮回合资源 -------------------
            if (is_throughput_test) {
                if (is_zero_copy_mode) {
                    zc_manager->shutdown();
                }
                else {
                    bytes_manager->shutdown();
                }
            }
            // 时延模式：也必须 shutdown（因为 LatencyTest 内部可能调用了 initialize_latency）
            else if (is_latency_test) {
                if (is_zero_copy_mode && zc_manager) {
                    zc_manager->shutdown();
                }
                else if (bytes_manager) {
                    bytes_manager->shutdown();
                }
            }

            // 缓冲时间，防止端口冲突
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // ==================== 测试结束，生成报告 ====================
        Logger::getInstance().logAndPrint("\n--- 开始生成系统资源使用报告 ---");
        metricsReport.generateSummary();

        // 关闭资源采集
        ResourceUtilization::instance().shutdown();
        GloMemPool::finalize();

        // --- 新增：程序结束前暂停，防止 cmd 窗口关闭 ---
        std::cout << "\n程序执行完毕，按任意键退出..." << std::endl;
        std::cin.get();
        // --- 新增结束 ---

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
        std::cout << "\n程序因异常终止，按任意键退出..." << std::endl;
        std::cin.get(); // 等待用户按键，防止窗口关闭
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
        std::cout << "\n程序因未捕获异常终止，按任意键退出..." << std::endl;
        std::cin.get(); // 等待用户按键，防止窗口关闭
        return EXIT_FAILURE;
    }
}