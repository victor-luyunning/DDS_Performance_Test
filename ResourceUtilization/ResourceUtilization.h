// ResourceUtilization.h
#pragma once
#include "SysMetrics.h"
#include <memory>

// ��Դ�����ʼ���� (����)
class ResourceUtilization {
public:
    // ��ȡ����ʵ��
    static ResourceUtilization& instance();

    // ��ʼ����Դ��� (������ʹ��ǰ����)
    bool initialize();

    // �ر���Դ��� (�������ǰ����)
    void shutdown();

    // �����Ľӿڡ��ɼ���ǰϵͳָ��
    // ����ֵ�������ϴε��ô˷���������¼���� CPU ʹ���ʷ�ֵ
    SysMetrics collectCurrentMetrics() const;

private:
    // ˽�й���/������������ֹ�ⲿʵ����
    ResourceUtilization();
    ~ResourceUtilization();

    // Pimpl (Pointer to Implementation) ģʽ
    // ����ƽ̨��ص�ʵ��ϸ��
    class Impl;
    std::unique_ptr<Impl> pimpl_;

    // ����Ƿ��ѳ�ʼ��
    bool is_initialized_ = false;
};