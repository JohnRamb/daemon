#pragma once
#include <string>
#include <format>

class NetworkEvent {
public:
    NetworkEvent(std::string iface, std::string addr, std::string mac,
                 std::string gateway, std::string mask, std::string flags);
    
    std::string toString() const;

private:
    std::string iface_;
    std::string addr_;
    std::string mac_;
    std::string gateway_;
    std::string mask_;
    std::string flags_;
};