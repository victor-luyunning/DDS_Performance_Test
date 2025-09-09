// Main.h
#pragma once
#include <string>

#ifndef DDS_TRUE
#define DDS_TRUE  true
#endif

#ifndef DDS_FALSE
#define DDS_FALSE false
#endif

#include "DDSManager.h"
#include "Config.h"
#include "Logger.h"
#include "GloMemPool.h"
#include "ThroughPut.h"

#include "TestData.h"
#include "TestDataDataWriter.h"
#include "TestDataTypeSupport.h"
       
#include "ZRSequence.h" 

/**
 * @file Main.h
 * @brief 全局配置和常量定义。
 *
 * 该头文件定义了整个性能测试工具中使用的全局路径、默认值和常量。
 * 通过修改此处的定义，可以轻松地调整程序的运行参数，而无需在多个源文件中查找和替换。
 */

namespace GlobalConfig {

    // =========================
    // 路径配置
    // =========================

    /**
     * @brief 默认的 ZRDDS QoS 配置文件 (XML) 的路径。
     * 这个文件定义了所有可用的 QoS 策略，如可靠传输、历史深度等。
     * 路径可以是绝对路径或相对于可执行文件运行目录的相对路径。
     */
    constexpr const char* DEFAULT_QOS_XML_PATH = "E:\\25-26-1\\project\\data\\zrdds_perf_test_qos.xml";

    /**
     * @brief 默认的 RapidIO 门铃代理 (ZRDDSRIOProxy) 配置文件路径。
     * 这个文件用于配置 RapidIO 通信的接收窗口。
     * 如果不需要 RapidIO 通信，此配置可忽略。
     */
    constexpr const char* DEFAULT_RIO_PROXY_CONFIG_PATH = "E:\\25-26-1\\ZRDDS\\ZRDDS-2.4.4\\bin\\ZRDDSPerf\\rio_config.txt";

    /**
     * @brief 存放日志文件的默认目录。
     * 程序会尝试在此目录下创建日志文件。
     */
    constexpr const char* LOG_DIRECTORY = "E:\\25-26-1\\project\\log";

    /**
     * @brief 默认的性能测试配置文件 (JSON) 的路径。
     * 这个文件包含了具体的测试用例，如主题名、角色、QoS名称等。
     */
    constexpr const char* DEFAULT_JSON_CONFIG_PATH = "E:\\25-26-1\\project\\data\\zrdds_perf_config.json";


    // =========================
    // 其他常量
    // =========================

    /**
     * @brief 日志文件名的前缀。
     */
    constexpr const char* LOG_FILE_PREFIX = "log_zrdds_perf_bench_config_";

    /**
     * @brief 日志文件的后缀。
     */
    constexpr const char* LOG_FILE_SUFFIX = ".log";

    /**
     * @brief RapidIO 门铃代理程序的可执行文件名。
     * 在启动程序前，可能需要检查此程序是否存在或自动启动它。
     */
    constexpr const char* RIO_PROXY_EXECUTABLE = "ZRDDSRIOProxy.exe";

} // namespace GlobalConfig
