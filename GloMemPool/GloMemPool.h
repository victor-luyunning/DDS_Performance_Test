// GloMemPool.h
#pragma once
#include <cstddef>
#include <string>

#include "ZRMemPool.h"

// ��ѡ����������������� new/delete ���أ����������ܲ��Թ����п�����
// #define ENABLE_GLOBAL_NEW_DELETE

class GloMemPool {
public:
    // ��ʼ��/����
    static bool initialize();
    static void finalize();

    // ԭʼ����ӿڣ����ײ�ʹ�ã�
    static void* allocate(size_t size, const char* file = nullptr, int line = 0);
    static void deallocate(void* ptr);

    // C++ ��ȫ����ӿڣ��Ƽ��ϲ�ʹ�ã�
    template<typename T, typename... Args>
    static T* new_object(Args&&... args);

    template<typename T>
    static void delete_object(T* ptr);

    // ͳ�ƹ���
    struct Stats {
        size_t total_allocated = 0;   // ��ǰ�ѷ�������
        size_t peak_usage = 0;        // ��ֵ�ڴ�ʹ��
        size_t alloc_count = 0;       // �������
        size_t dealloc_count = 0;     // �ͷŴ���
    };
    static Stats getStats();
    static void logStats();

private:
    static ZRMemPool* s_pool;
    static Stats s_stats;

#ifdef _MEMORY_USE_TRACK_
    // ����ģʽ��ά�������Сӳ�䣨����׼ȷͳ���ͷţ�
    static std::unordered_map<void*, size_t> s_alloc_map;
#endif

    // ��ֹʵ����
    GloMemPool() = delete;
};

// ȫ�� new/delete ���أ���ѡ���ã�
#ifdef ENABLE_GLOBAL_NEW_DELETE
void* operator new(size_t size);
void operator delete(void* ptr) noexcept;
void* operator new[](size_t size);
void operator delete[](void* ptr) noexcept;
#endif