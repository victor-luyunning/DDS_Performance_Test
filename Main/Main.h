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
 * @brief ȫ�����úͳ������塣
 *
 * ��ͷ�ļ��������������ܲ��Թ�����ʹ�õ�ȫ��·����Ĭ��ֵ�ͳ�����
 * ͨ���޸Ĵ˴��Ķ��壬�������ɵص�����������в������������ڶ��Դ�ļ��в��Һ��滻��
 */

namespace GlobalConfig {

    // =========================
    // ·������
    // =========================

    /**
     * @brief Ĭ�ϵ� ZRDDS QoS �����ļ� (XML) ��·����
     * ����ļ����������п��õ� QoS ���ԣ���ɿ����䡢��ʷ��ȵȡ�
     * ·�������Ǿ���·��������ڿ�ִ���ļ�����Ŀ¼�����·����
     */
    constexpr const char* DEFAULT_QOS_XML_PATH = "E:\\25-26-1\\project\\data\\zrdds_perf_test_qos.xml";

    /**
     * @brief Ĭ�ϵ� RapidIO ������� (ZRDDSRIOProxy) �����ļ�·����
     * ����ļ��������� RapidIO ͨ�ŵĽ��մ��ڡ�
     * �������Ҫ RapidIO ͨ�ţ������ÿɺ��ԡ�
     */
    constexpr const char* DEFAULT_RIO_PROXY_CONFIG_PATH = "E:\\25-26-1\\ZRDDS\\ZRDDS-2.4.4\\bin\\ZRDDSPerf\\rio_config.txt";

    /**
     * @brief �����־�ļ���Ĭ��Ŀ¼��
     * ����᳢���ڴ�Ŀ¼�´�����־�ļ���
     */
    constexpr const char* LOG_DIRECTORY = "E:\\25-26-1\\project\\log";

    /**
     * @brief Ĭ�ϵ����ܲ��������ļ� (JSON) ��·����
     * ����ļ������˾���Ĳ���������������������ɫ��QoS���Ƶȡ�
     */
    constexpr const char* DEFAULT_JSON_CONFIG_PATH = "E:\\25-26-1\\project\\data\\zrdds_perf_config.json";


    // =========================
    // ��������
    // =========================

    /**
     * @brief ��־�ļ�����ǰ׺��
     */
    constexpr const char* LOG_FILE_PREFIX = "log_zrdds_perf_bench_config_";

    /**
     * @brief ��־�ļ��ĺ�׺��
     */
    constexpr const char* LOG_FILE_SUFFIX = ".log";

    /**
     * @brief RapidIO ����������Ŀ�ִ���ļ�����
     * ����������ǰ��������Ҫ���˳����Ƿ���ڻ��Զ���������
     */
    constexpr const char* RIO_PROXY_EXECUTABLE = "ZRDDSRIOProxy.exe";

} // namespace GlobalConfig
