// ResourceUtilization.h
#pragma once
#include "SysMetrics.h"
#include <memory>

class ResourceUtilization {
public:
    // 单例访问
    static ResourceUtilization& instance();

    // 初始化/关闭（必须调用）
    bool initialize();
    void shutdown();

    // 【核心】采集当前系统指标
    SysMetrics collectCurrentMetrics() const;

private:
    ResourceUtilization(); // 私有构造
    ~ResourceUtilization();

    class Impl; // Pimpl
    std::unique_ptr<Impl> pimpl_;
    bool is_initialized_ = false;
};