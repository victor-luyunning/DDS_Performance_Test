// GloMemPool.h
#pragma once
#include <cstddef>
#include <string>

#include "ZRMemPool.h"  

class GloMemPool {
public:
    // ��ʼ��/����
    static bool initialize();
    static void finalize();

    // �������ͷţ��Զ�ʹ��ȫ�ֳأ�
    static void* allocate(size_t size, const char* file = nullptr, int line = 0);
    static void deallocate(void* ptr);

    // ͳ�ƹ��ܣ��ؼ����������ܲ��ԣ�
    struct Stats {
        size_t total_allocated = 0;   // ��ǰ�ѷ�������
        size_t peak_usage = 0;        // ��ֵ�ڴ�ʹ��
        size_t alloc_count = 0;       // �������
        size_t dealloc_count = 0;     // �ͷŴ���
    };
    static Stats getStats();
    static void logStats();  // ��ӡ��־

private:
    static ZRMemPool* s_pool;     // ȫ�ֳ�ָ�루�� ZR ��ʼ����
    static Stats s_stats;         // ͳ������

    // ��ֹʵ����
    GloMemPool() = delete;
}; 
