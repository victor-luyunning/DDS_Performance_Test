// ResourceUtilization.h
#pragma once
#include "SysMetrics.h"
#include <memory>

// 资源利用率监控类 (单例)
class ResourceUtilization {
public:
    // 获取单例实例
    static ResourceUtilization& instance();

    // 初始化资源监控 (必须在使用前调用)
    bool initialize();

    // 关闭资源监控 (程序结束前调用)
    void shutdown();

    // 【核心接口】采集当前系统指标
    // 返回值包含自上次调用此方法以来记录到的 CPU 使用率峰值
    SysMetrics collectCurrentMetrics() const;

private:
    // 私有构造/析构函数，防止外部实例化
    ResourceUtilization();
    ~ResourceUtilization();

    // Pimpl (Pointer to Implementation) 模式
    // 隐藏平台相关的实现细节
    class Impl;
    std::unique_ptr<Impl> pimpl_;

    // 标记是否已初始化
    bool is_initialized_ = false;
};