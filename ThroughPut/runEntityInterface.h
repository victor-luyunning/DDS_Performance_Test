#pragma once
// runEntityInterface.h
#pragma once
#include "ConfigData.h"

class runEntityInterface {
public:
    virtual ~runEntityInterface() = default;

    // �����������߼�
    virtual int runPublisher(const ConfigData& config) = 0;

    // �����������߼�
    virtual int runSubscriber(const ConfigData& config) = 0;
};