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
#include <string_view>

// --- 项目头文件 ---
#include "Main.h"
#include "DDSManager_Bytes.h"
#include "DDSManager_ZeroCopyBytes.h"
#include "Config.h"
#include "Logger.h"
#include "GloMemPool.h"
#include "Throughput_Bytes.h"
#include "Throughput_ZeroCopyBytes.h"
#include "MetricsReport.h"
#include "TestRoundResult.h"
#include "ResourceUtilization.h"
#include "LatencyTest_Bytes.h"  // ✅ 新增：引入时延测试模块

namespace {
    std::string json_file_path = GlobalConfig::DEFAULT_JSON_CONFIG_PATH;
    std::string qos_file_path = GlobalConfig::DEFAULT_QOS_XML_PATH;
    std::string logDir = GlobalConfig::LOG_DIRECTORY;
    std::string logPrefix = GlobalConfig::LOG_FILE_PREFIX;
    std::string logSuffix = GlobalConfig::LOG_FILE_SUFFIX;
    std::string resultDir = GlobalConfig::DEFAULT_RESULT_PATH;
    bool loggingEnabled = true;

    constexpr std::string_view TP_PREFIX = "tp::";
    constexpr std::string_view DELAY_PREFIX = "delay::";
}

int main() {
    try {
        // ================= 初始化全局内存池 =================
        if (!GloMemPool::initialize()) {
            std::cerr << "[Error] GloMemPool 初始化失败！" << std::endl;
            std::cin.get();
            return EXIT_FAILURE;
        }
        Logger::getInstance().logAndPrint("[Memory] 使用 GloMemPool 管理全局内存");

        // ================= 初始化日志系统 =================
        Logger::setupLogger(logDir, logPrefix, logSuffix);

        // ================= 加载并选择配置 =================
        Config config(json_file_path);
        if (!config.promptAndSelectConfig(&Logger::getInstance())) {
            Logger::getInstance().logAndPrint("用户取消选择或配置加载失败");
            std::cin.get();
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
            std::cin.get();
            return EXIT_FAILURE;
        }

        // ==================== 在此统一决定测试类型 ====================
        const std::string& test_name = base_config.name;
        bool is_throughput_test = (test_name.rfind(TP_PREFIX, 0) == 0);
        bool is_delay_test = (test_name.rfind(DELAY_PREFIX, 0) == 0);

        if (!is_throughput_test && !is_delay_test) {
            Logger::getInstance().logAndPrint("[Error] 配置名称必须以 'tp::' 或 'delay::' 开头");
            std::cin.get();
            return EXIT_FAILURE;
        }

        Logger::getInstance().logAndPrint("测试类型: " + std::string(is_throughput_test ? "THROUGHPUT" : "LATENCY"));

        // ==================== 初始化资源监控 ====================
        if (!ResourceUtilization::instance().initialize()) {
            Logger::getInstance().logAndPrint("[Warning] ResourceUtilization 初始化失败！CPU 监控可能无效。");
        }
        else {
            Logger::getInstance().logAndPrint("[Resource] ResourceUtilization 初始化成功");
        }

        MetricsReport metricsReport;

        // ========== 根据测试类型创建对应模块（仅一次）==========
        bool is_zero_copy_mode = false;
        std::unique_ptr<DDSManager_Bytes> bytes_manager;
        std::unique_ptr<DDSManager_ZeroCopyBytes> zc_manager;
        std::unique_ptr<Throughput_Bytes> throughput_bytes;
        std::unique_ptr<Throughput_ZeroCopyBytes> throughput_zc;
        std::unique_ptr<LatencyTest_Bytes> latency_test;  // ✅ 新增：时延测试对象

        if (is_throughput_test) {
            is_zero_copy_mode = (base_config.m_typeName == "DDS::ZeroCopyBytes");

            if (is_zero_copy_mode) {
                Logger::getInstance().logAndPrint("启动 ZeroCopyBytes 模式");
                zc_manager = std::make_unique<DDSManager_ZeroCopyBytes>(base_config, qos_file_path);
                throughput_zc = std::make_unique<Throughput_ZeroCopyBytes>(*zc_manager,
                    [&metricsReport](const TestRoundResult& result) {
                        metricsReport.addResult(result);
                    });
            }
            else {
                Logger::getInstance().logAndPrint("启动 Bytes 模式");
                bytes_manager = std::make_unique<DDSManager_Bytes>(base_config, qos_file_path);
                throughput_bytes = std::make_unique<Throughput_Bytes>(*bytes_manager,
                    [&metricsReport](const TestRoundResult& result) {
                        metricsReport.addResult(result);
                    });
            }
        }
        else if (is_delay_test) {
            // 时延测试仅支持 Bytes
            Logger::getInstance().logAndPrint("启动 Latency 模式 (Bytes only)");
            bytes_manager = std::make_unique<DDSManager_Bytes>(base_config, qos_file_path);
            latency_test = std::make_unique<LatencyTest_Bytes>(*bytes_manager,
                [&metricsReport](const TestRoundResult& result) {
                    metricsReport.addResult(result);  // ✅ 统一结果上报
                });
        }

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

            bool init_success = false;

            // ------------------- 重新初始化 DDSManager -------------------
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
                        init_success = bytes_manager->initialize(DDSManager_Bytes::TestMode::THROUGHPUT);
                    }
                    else {
                        auto end_callback = [&]() { throughput_bytes->onEndOfRound(); };
                        init_success = bytes_manager->initialize(
                            DDSManager_Bytes::TestMode::THROUGHPUT,
                            [&](const DDS::Bytes& sample, const DDS::SampleInfo& info) {
                                throughput_bytes->onDataReceived(sample, info);
                            },
                            nullptr,
                            end_callback
                        );
                    }
                }
            }
            else if (is_delay_test) {
                if (current_cfg.m_isPositive) {
                    // Publisher 角色：发送 Ping，接收 Pong
                    LatencyTest_Bytes* test_ptr = latency_test.get();  // 获取裸指针
                    init_success = bytes_manager->initialize(
                        DDSManager_Bytes::TestMode::LATENCY,
                        nullptr,
                        [test_ptr](const DDS::Bytes& s, const DDS::SampleInfo& i) {
                            if (test_ptr) {
                                test_ptr->onDataReceived(s, i);  // 通过指针调用
                            }
                        },
                        nullptr
                    );
                }
                else {
                    // Subscriber 角色：接收 Ping，回复 Pong
                    LatencyTest_Bytes* test_ptr = latency_test.get();
                    init_success = bytes_manager->initialize(
                        DDSManager_Bytes::TestMode::LATENCY,
                        [test_ptr](const DDS::Bytes& s, const DDS::SampleInfo& i) {
                            if (test_ptr) {
                                test_ptr->onDataReceived(s, i);
                            }
                        },
                        nullptr,
                        nullptr
                    );
                }
            }

            if (!init_success) {
                Logger::getInstance().logAndPrint("[Error] DDSManager 初始化失败（第 " + std::to_string(round + 1) + " 轮）");
                total_result = EXIT_FAILURE;
                break;
            }

            // ------------------- Publisher: 等待 Subscriber 重连（从第二轮开始）-------------------
            if (current_cfg.m_isPositive && round > 0) {
                Logger::getInstance().logAndPrint("等待订阅者重新上线...");

                bool connected = false;
                if (is_throughput_test) {
                    if (is_zero_copy_mode) {
                        connected = throughput_zc->waitForSubscriberReconnect(std::chrono::seconds(10));
                    }
                    else {
                        connected = throughput_bytes->waitForSubscriberReconnect(std::chrono::seconds(10));
                    }
                }
                // 时延测试暂不实现等待逻辑（可选）

                if (!connected) {
                    Logger::getInstance().logAndPrint("警告：未检测到订阅者重连，超时继续...");
                }
            }

            // ------------------- 运行单轮测试 -------------------
            int result = 0;
            if (current_cfg.m_isPositive) {
                if (is_throughput_test) {
                    result = is_zero_copy_mode ?
                        throughput_zc->runPublisher(current_cfg) :
                        throughput_bytes->runPublisher(current_cfg);
                }
                else if (is_delay_test) {
                    // ✅ 调用新模块
                    result = latency_test->runPublisher(current_cfg);
                }
            }
            else {
                if (is_throughput_test) {
                    result = is_zero_copy_mode ?
                        throughput_zc->runSubscriber(current_cfg) :
                        throughput_bytes->runSubscriber(current_cfg);
                }
                else if (is_delay_test) {
                    // ✅ 调用新模块
                    result = latency_test->runSubscriber(current_cfg);
                }
            }

            if (result != 0) {
                total_result = EXIT_FAILURE;
            }

            // ------------------- 清理本轮回合资源 -------------------
            if (is_throughput_test) {
                if (is_zero_copy_mode) {
                    zc_manager->shutdown();
                }
                else {
                    bytes_manager->shutdown();
                }
            }
            else if (is_delay_test) {
                bytes_manager->shutdown();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // ==================== 测试结束，生成报告 ====================
        Logger::getInstance().logAndPrint("\n--- 开始生成系统资源使用报告 ---");
        metricsReport.generateSummary();

        ResourceUtilization::instance().shutdown();
        GloMemPool::finalize();

        std::cout << "\n程序执行完毕，按任意键退出..." << std::endl;
        std::cin.get();

        return total_result == EXIT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;

    }
    catch (const std::exception& e) {
        std::string errorMsg = "[Error] 异常: " + std::string(e.what());
        Logger::getInstance().logAndPrint(errorMsg);
        std::cout << "\n程序因异常终止，按任意键退出..." << std::endl;
        std::cin.get();
        return EXIT_FAILURE;
    }
    catch (...) {
        std::string errorMsg = "[Error] 未捕获的异常";
        Logger::getInstance().logAndPrint(errorMsg);
        std::cout << "\n程序因未捕获异常终止，按任意键退出..." << std::endl;
        std::cin.get();
        return EXIT_FAILURE;
    }
}