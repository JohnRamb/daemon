#pragma once

#include <string>

class DHCPClient {
public:
    virtual ~DHCPClient() = default;
    virtual bool start(const std::string& ifname) = 0;
    virtual bool stop(const std::string& ifname) = 0;
    virtual bool isRunning(const std::string& interface) = 0;
};