#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "netlink_manager.h"
#include <string>

class NetworkManager {
public:
    NetworkManager(NetlinkManager& netlink_mgr);
    
    std::string setDynamicIP(const std::string& ifname);
    void stopDhcpcd(const std::string& ifname);
    void setStaticIP(const std::string& ifname, const std::string& ip_mask, const std::string& gateway);
    std::string getInterfaceInfo(const std::string& ifname);

private:
    NetlinkManager& netlink_mgr_;
};

#endif // NETWORK_MANAGER_H