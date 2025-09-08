// Config.cpp
// 注意：配置模块使用标准 std::string / std::vector
// 不接入 GloMemPool，因为：
// 1. 只在启动时加载一次，不影响性能测试
// 2. 使用标准库更安全、易维护
// 3. 避免过度复杂化非关键路径
#include "Config.h"
#include "ConfigData.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "Logger.h"

using json = nlohmann::ordered_json;

namespace {
    void printArrayField(std::ostream& out, const std::string& name, const std::vector<int>& vec, bool hasData) {
        out << "\t" << name << ":\t";
        if (hasData && !vec.empty()) {
            bool first = true;
            for (const auto& val : vec) {
                if (!first) out << ", ";
                out << val;
                first = false;
            }
        }
        out << std::endl;
    }
}

class Config::Impl {
public:
    explicit Impl(const std::string& json_file_path) : json_file_path_(json_file_path) {
        loadConfig();
    }

    void loadConfig() {
        Logger::getInstance().logAndPrint("正在加载配置文件: " + json_file_path_);

        if (!std::filesystem::exists(json_file_path_)) {
            Logger::getInstance().logAndPrint("[Error] 配置文件不存在: " + json_file_path_);
            Logger::getInstance().logAndPrint("当前工作目录: " + std::filesystem::current_path().string());
            throw std::runtime_error("配置文件不存在");
        }

        std::ifstream file(json_file_path_);
        if (!file.is_open()) {
            throw std::runtime_error("[Error] 无法打开 JSON 文件: " + json_file_path_);
        }

        json json_data;
        file >> json_data;

        for (auto it = json_data.begin(); it != json_data.end(); ++it) {
            configs_.push_back(parseConfigItem(it.key(), it.value()));
        }

        if (configs_.empty()) {
            throw std::runtime_error("JSON 文件中没有任何配置");
        }

        current_ = configs_[0];
    }

    ConfigData parseConfigItem(const std::string& name, const nlohmann::json& item) {
        ConfigData cfg{};
        cfg.name = name;
        cfg.m_dpfQosName = item.value("m_dpfQosName", "");
        cfg.m_dpQosName = item.value("m_dpQosName", "");
        cfg.m_pubQosName = item.value("m_pubQosName", "");
        cfg.m_subQosName = item.value("m_subQosName", "");
        cfg.m_writerQosName = item.value("m_writerQosName", "");
        cfg.m_readerQosName = item.value("m_readerQosName", "");
        cfg.m_typeName = item.value("m_typeName", "");
        cfg.m_topicName = item.value("m_topicName", "");
        cfg.m_domainId = item.value("m_domainId", 0);
        cfg.m_isPositive = item.value("m_isPositive", false);
        cfg.m_useTaskNextSample = item.value("m_useTaskNextSample", false);
        cfg.m_useDataArrived = item.value("m_useDataArrived", false);
        cfg.m_useSyncDelay = item.value("m_useSyncDelay", false);
        cfg.m_remoteNum = item.value("m_remoteNum", 0);
        cfg.m_userAction = item.value("m_userAction", 0);
        cfg.m_latencyMode = item.value("m_latencyMode", DEFAULT_LATENCY_MODE);
        cfg.m_clockDevName = item.value("m_clockDevName", DEFAULT_CLOCK_DEV_NAME);
        cfg.m_logTimeStamp = item.value("m_logTimeStamp", true);
        cfg.m_checkSample = item.value("m_checkSample", false);
        cfg.m_delayMode = item.value("m_delayMode", 0);
        cfg.m_activeLoop = item.value("m_activeLoop", 0);

        auto load_vector = [&](const std::string& key, std::vector<int>& vec, bool& has) {
            auto it = item.find(key);
            if (it != item.end() && it->is_array()) {
                vec = it->get<std::vector<int>>();
                has = true;
            }
            };

        load_vector("m_minSize", cfg.m_minSize, cfg.has_m_minSize);
        load_vector("m_maxSize", cfg.m_maxSize, cfg.has_m_maxSize);
        load_vector("m_sendCount", cfg.m_sendCount, cfg.has_m_sendCount);
        load_vector("m_sendDelayCount", cfg.m_sendDelayCount, cfg.has_m_sendDelayCount);
        load_vector("m_sendDelay", cfg.m_sendDelay, cfg.has_m_sendDelay);
        load_vector("m_sendPrintGap", cfg.m_sendPrintGap, cfg.has_m_sendPrintGap);
        load_vector("m_recvPrintGap", cfg.m_recvPrintGap, cfg.has_m_recvPrintGap);
        load_vector("m_domainIds", cfg.m_domainIds, cfg.has_m_domainIds);
        load_vector("m_dpNum", cfg.m_dpNum, cfg.has_m_dpNum);
        load_vector("m_readerNum", cfg.m_readerNum, cfg.has_m_readerNum);
        load_vector("m_writerNum", cfg.m_writerNum, cfg.has_m_writerNum);
        load_vector("m_readerTopicRange", cfg.m_readerTopicRange, cfg.has_m_readerTopicRange);
        load_vector("m_writerTopicRange", cfg.m_writerTopicRange, cfg.has_m_writerTopicRange);

        if (item.contains("configs") && item["configs"].is_array()) {
            cfg.configs = item["configs"].get<std::vector<std::string>>();
            cfg.has_configs = true;
        }

        if (cfg.has_m_minSize) {
            cfg.m_loopNum = static_cast<int>(cfg.m_minSize.size());
        }
        else {
            cfg.m_loopNum = item.value("m_loopNum", 0);
        }

        cfg.m_resultPath = item.value("m_resultPath", "");

        if (name.rfind("tp::", 0) == 0 ||
            name.rfind("delay::", 0) == 0 ||
            name.rfind("scale::", 0) == 0 ||
            name.rfind("concurrence_delay::", 0) == 0) {
            cfg.m_resultPath = generateResultPath(cfg);
        }

        return cfg;
    }

    std::string generateResultPath(const ConfigData& c) const {
        std::string typeName = c.m_typeName;
        std::replace(typeName.begin(), typeName.end(), ':', '-');

        std::string base = c.m_dpfQosName + "-" +
            c.m_dpQosName + "-" +
            c.m_writerQosName + "-" +
            c.m_readerQosName + "-" +
            typeName + "-" +
            (c.m_isPositive ? "positive" : "negative");

        if (!c.m_resultPath.empty()) {
            return base + "-" + c.m_resultPath;
        }

        std::string simplifiedType = "DDS--Bytes";
        std::string opposite = c.m_isPositive ? "negative" : "positive";

        return base + "-" +
            c.m_pubQosName + "-" +
            c.m_subQosName + "-" +
            "default" + "-" +
            simplifiedType + "-" +
            opposite + "-" +
            "default.csv";
    }

    const ConfigData* findPairedConfig() const {
        for (size_t i = 0; i < configs_.size(); ++i) {
            if (configs_[i].name == current_.name) {
                size_t currentIndex = i;
                size_t pairedIndex = (currentIndex % 2 == 0) ? currentIndex + 1 : currentIndex - 1;
                if (pairedIndex < configs_.size()) {
                    return &configs_[pairedIndex];
                }
                break;
            }
        }
        return nullptr;
    }

    std::pair<std::vector<int>, bool> getArrayFallback(
        bool hasCurrent,
        const std::vector<int>& currentArray,
        const std::function<std::vector<int>(const ConfigData*)>& getter,
        const std::function<bool(const ConfigData*)>& hasGetter
    ) const {
        if (hasCurrent) {
            return { currentArray, true };
        }

        const ConfigData* pairedConfig = findPairedConfig();
        if (pairedConfig && hasGetter(pairedConfig)) {
            return { getter(pairedConfig), true };
        }

        return { currentArray, false };
    }

    void printCurrentConfig(const ConfigData& c, std::ostream& out) const {
        out << "ZRDDS-PerfBench-Config:" << c.name << std::endl;

        out << "\tm_dpfQosName:\t" << c.m_dpfQosName << std::endl;
        out << "\tm_dpQosName:\t" << c.m_dpQosName << std::endl;
        out << "\tm_pubQosName:\t" << c.m_pubQosName << std::endl;
        out << "\tm_subQosName:\t" << c.m_subQosName << std::endl;
        out << "\tm_writerQosName:\t" << c.m_writerQosName << std::endl;
        out << "\tm_readerQosName:\t" << c.m_readerQosName << std::endl;
        out << "\tm_typeName:\t" << c.m_typeName << std::endl;
        out << "\tm_topicName:\t" << c.m_topicName << std::endl;
        out << "\tm_domainId:\t" << c.m_domainId << std::endl;
        out << "\tm_isPositive:\t" << c.m_isPositive << std::endl;
        out << "\tm_useTaskNextSample:\t" << c.m_useTaskNextSample << std::endl;
        out << "\tm_useDataArrived:\t" << c.m_useDataArrived << std::endl;
        out << "\tm_remoteNum:\t" << c.m_remoteNum << std::endl;
        out << "\tm_userAction:\t" << c.m_userAction << std::endl;
        out << "\tm_latencyMode:\t" << c.m_latencyMode << std::endl;
        out << "\tm_useSyncDelay:\t" << c.m_useSyncDelay << std::endl;
        out << "\tm_clockDevName:\t" << c.m_clockDevName << std::endl;
        out << "\tm_logTimeStamp:\t" << c.m_logTimeStamp << std::endl;
        out << "\tm_checkSample:\t" << c.m_checkSample << std::endl;
        out << "\tm_delayMode:\t" << c.m_delayMode << std::endl;
        out << "\tm_activeLoop:\t" << c.m_activeLoop << std::endl;
        out << "\tm_loopNum:\t" << c.m_loopNum << std::endl;

        std::vector<std::tuple<
            const char*,
            const std::vector<int>&,
            bool,
            std::function<std::vector<int>(const ConfigData*)>,
            std::function<bool(const ConfigData*)>
            >> fallbackArrayFields = {
                { "m_minSize", c.m_minSize, c.has_m_minSize,
                  [](const ConfigData* config) { return config->m_minSize; },
                  [](const ConfigData* config) { return config->has_m_minSize; } },
                { "m_maxSize", c.m_maxSize, c.has_m_maxSize,
                  [](const ConfigData* config) { return config->m_maxSize; },
                  [](const ConfigData* config) { return config->has_m_maxSize; } },
                { "m_sendCount", c.m_sendCount, c.has_m_sendCount,
                  [](const ConfigData* config) { return config->m_sendCount; },
                  [](const ConfigData* config) { return config->has_m_sendCount; } },
                { "m_sendDelayCount", c.m_sendDelayCount, c.has_m_sendDelayCount,
                  [](const ConfigData* config) { return config->m_sendDelayCount; },
                  [](const ConfigData* config) { return config->has_m_sendDelayCount; } },
                { "m_sendDelay", c.m_sendDelay, c.has_m_sendDelay,
                  [](const ConfigData* config) { return config->m_sendDelay; },
                  [](const ConfigData* config) { return config->has_m_sendDelay; } },
                { "m_sendPrintGap", c.m_sendPrintGap, c.has_m_sendPrintGap,
                  [](const ConfigData* config) { return config->m_sendPrintGap; },
                  [](const ConfigData* config) { return config->has_m_sendPrintGap; } },
                { "m_recvPrintGap", c.m_recvPrintGap, c.has_m_recvPrintGap,
                  [](const ConfigData* config) { return config->m_recvPrintGap; },
                  [](const ConfigData* config) { return config->has_m_recvPrintGap; } }
        };

        for (const auto& [name, currentVec, hasCurrent, getter, hasGetter] : fallbackArrayFields) {
            auto [finalVec, hasData] = getArrayFallback(hasCurrent, currentVec, getter, hasGetter);
            printArrayField(out, name, finalVec, hasData);
        }

        out << "\tm_resultPath:\t" << c.m_resultPath << std::endl;
    }

    void printConcurrenceDelayConfig(const ConfigData& c, std::ostream& out) const {
        out << "ZRDDS-PerfBench-Concurrence-Delay-Config: " << c.name << " contains " << c.configs.size() << " sub-configs" << std::endl;

        for (const auto& subName : c.configs) {
            const ConfigData* subConfig = nullptr;
            for (const auto& config : configs_) {
                if (config.name == subName) {
                    subConfig = &config;
                    break;
                }
            }
            if (!subConfig) {
                out << "Warning: 子配置未找到: " << subName << std::endl;
                continue;
            }
            printCurrentConfig(*subConfig, out);
        }
    }

    std::vector<ConfigData> configs_;
    ConfigData current_;
    std::string json_file_path_;

    static constexpr const char* DEFAULT_LATENCY_MODE = "pp";
    static constexpr const char* DEFAULT_CLOCK_DEV_NAME = "CLOCK_REALTIME";
};

// ============= Config 接口实现 =============

Config::Config(const std::string& json_file_path)
    : pImpl_(std::make_unique<Impl>(json_file_path)) {
}

Config::~Config() noexcept = default;

const std::vector<ConfigData>& Config::getConfigs() const {
    return pImpl_->configs_;
}

const ConfigData& Config::getCurrentConfig() const {
    return pImpl_->current_;
}

void Config::selectConfig(size_t index) {
    if (index >= pImpl_->configs_.size()) {
        throw std::out_of_range("配置索引超出范围");
    }
    pImpl_->current_ = pImpl_->configs_[index];
}

void Config::selectConfig(const std::string& name) {
    for (const auto& cfg : pImpl_->configs_) {
        if (cfg.name == name) {
            pImpl_->current_ = cfg;
            return;
        }
    }
    throw std::runtime_error("未找到配置: " + name);
}

size_t Config::getConfigCount() const {
    return pImpl_->configs_.size();
}

void Config::listAvailableConfigs() const {
    std::cout << "Usage: test config_name or index, availables:" << std::endl;
    for (size_t i = 0; i < pImpl_->configs_.size(); ++i) {
        std::cout << "\t" << i << ". " << pImpl_->configs_[i].name << std::endl;
    }
}

void Config::printCurrentConfig(std::ostream& out) const {
    const std::string& name = pImpl_->current_.name;

    if (name.rfind("concurrence_delay::", 0) == 0) {
        pImpl_->printConcurrenceDelayConfig(pImpl_->current_, out);
    }
    else if (name.rfind("tp::", 0) == 0 ||
        name.rfind("delay::", 0) == 0 ||
        name.rfind("scale::", 0) == 0) {
        pImpl_->printCurrentConfig(pImpl_->current_, out);
    }
    else {
        std::cerr << "未知配置类型: " << name << std::endl;
    }
}

bool Config::promptAndSelectConfig(void* logger) {
    auto log = [logger](const std::string& msg) {
        if (logger) {
            static_cast<Logger*>(logger)->logAndPrint(msg);
        }
        else {
            std::cout << msg << std::endl;
        }
        };

    log("Usage: test config_name or index, availables:");
    for (size_t i = 0; i < pImpl_->configs_.size(); ++i) {
        std::string configInfo = "\t" + std::to_string(i) + ". " + pImpl_->configs_[i].name;
        log(configInfo);
    }

    std::string input;
    while (true) {
        std::string prompt = "\nInput index (0-" + std::to_string(pImpl_->configs_.size() - 1) + ") or config name: ";
        log(prompt);

        if (!std::getline(std::cin, input)) {
            log("Error: 读取输入失败");
            continue;
        }

        input.erase(0, input.find_first_not_of(" \t\r\n"));
        input.erase(input.find_last_not_of(" \t\r\n") + 1);

        if (input.empty()) {
            log("Error: 输入不能为空");
            continue;
        }

        try {
            size_t index = std::stoull(input);
            selectConfig(index);
            return true;
        }
        catch (const std::invalid_argument&) {
        }
        catch (const std::out_of_range&) {
            log("Error: 索引超出范围，请输入 0-" + std::to_string(pImpl_->configs_.size() - 1) + " 之间的数字");
            continue;
        }

        try {
            selectConfig(input);
            return true;
        }
        catch (const std::exception& e) {
            log("Error: " + std::string(e.what()));
            continue;
        }
    }
}