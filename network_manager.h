#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <string>
#include <map>
#include <netlink/route/addr.h>
#include <netlink/route/route.h>

class NetlinkManager;

class NetworkManager {
public:
    NetworkManager(NetlinkManager& netlink_mgr);
    
    void setStaticIP(const std::string& ifname, const std::string& ip_mask, const std::string& gateway);
    std::string setDynamicIP(const std::string& ifname);
    void stopDhcpcd(const std::string& ifname);
    std::string getStatus() const;
    std::string getDNS() const;

private:
    NetlinkManager& netlink_mgr_;
    std::map<int, std::string> interface_ips_;
    std::map<int, uint32_t> interface_masks_;
    std::map<std::string, pid_t> dhcpcd_pids_;

    std::string prefixToMask(uint32_t prefix) const;
    std::string getGateway(int if_index) const;
    void removeIP(const std::string& ifname);
    void addGateway(const std::string& gateway, int ifindex);
    bool waitForDhcpcd(const std::string& ifname, int timeout_seconds);
    void closeAllFileDescriptors();
};

#endif // NETWORK_MANAGER_H