// ResourceUtilization.h
#pragma once
#include "SysMetrics.h"
#include <memory>

class ResourceUtilization {
public:
    // ��������
    static ResourceUtilization& instance();

    // ��ʼ��/�رգ�������ã�
    bool initialize();
    void shutdown();

    // �����ġ��ɼ���ǰϵͳָ��
    SysMetrics collectCurrentMetrics() const;

private:
    ResourceUtilization(); // ˽�й���
    ~ResourceUtilization();

    class Impl; // Pimpl
    std::unique_ptr<Impl> pimpl_;
    bool is_initialized_ = false;
};