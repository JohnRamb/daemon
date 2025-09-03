#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "netlink_manager.h"
#include <string>
#include <optional>
#include "dhcp_client.h"
#include <memory>

class NetworkManager {
public:
    using LogCallback = std::function<void(const std::string&)>;
    
    NetworkManager(NetlinkManager& netlink_mgr, 
                  std::unique_ptr<DHCPClient> dhcp_client = nullptr,
                  LogCallback log_callback = nullptr);
    
    bool setDynamicIP(const std::string& ifname);
    bool stopDynamicIP(const std::string& ifname);
    bool setStaticIP(const std::string& ifname, const std::string& ip, 
                    uint8_t prefix_len, const std::string& gateway = "");
    
    struct InterfaceInfo {
        std::string name;
        std::string ip;
        std::string netmask;
        std::string gateway;
        bool is_up;
    };
    
    std::optional<InterfaceInfo> getInterfaceInfo(const std::string& ifname);
    bool bringInterfaceUp(const std::string& ifname);
    bool bringInterfaceDown(const std::string& ifname);
    
    void setLogCallback(LogCallback callback);

private:
    NetlinkManager& netlink_mgr_;
    std::unique_ptr<DHCPClient> dhcp_client_;
    LogCallback log_callback_;
    
    void log(const std::string& message) const;
    bool validateInterface(const std::string& ifname) const;
    bool addRoute(const std::string& gateway, int ifindex);
};

#endif // NETWORK_MANAGER_H