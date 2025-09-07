// Logger.h
#pragma once
#include <string>
#include <memory>
#include <thread>

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool initialize(
        const std::string& logDirectory,
        const std::string& filePrefix,
        const std::string& fileSuffix
    );

    static void setupLogger(
        const std::string& logDirectory,
        const std::string& filePrefix,
        const std::string& fileSuffix
    );

    void log(const std::string& message);
    void logConfig(const std::string& configInfo);
    void logResult(const std::string& result);
    void logAndPrint(const std::string& message);
    void info(const std::string& msg);
    void error(const std::string& msg);
    void close();

private:
    Logger();  
    ~Logger();

    class Impl;  
    std::unique_ptr<Impl> pImpl_;
};