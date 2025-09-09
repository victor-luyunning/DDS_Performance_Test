#pragma once
// runEntityInterface.h
#pragma once
#include "ConfigData.h"

class runEntityInterface {
public:
    virtual ~runEntityInterface() = default;

    // 启动发布者逻辑
    virtual int runPublisher(const ConfigData& config) = 0;

    // 启动订阅者逻辑
    virtual int runSubscriber(const ConfigData& config) = 0;
};