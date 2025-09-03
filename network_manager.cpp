#include "network_manager.h"
#include <netlink/netlink.h>
#include <netlink/cache.h>
#include <netlink/utils.h>
#include <netlink/data.h>
#include <netlink/route/rtnl.h>
#include <netlink/route/route.h>
#include <netlink/route/link.h>
#include <netlink/route/nexthop.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sstream>
#include <iostream>
#include <dhcpcd_client.h>

NetworkManager::NetworkManager(NetlinkManager& netlink_mgr,
                             std::unique_ptr<DHCPClient> dhcp_client,
                             LogCallback log_callback)
    : netlink_mgr_(netlink_mgr),
      dhcp_client_(std::move(dhcp_client ? std::move(dhcp_client) : std::make_unique<DhcpcdClient>())),
      log_callback_(log_callback) {}

void NetworkManager::log(const std::string& message) const {
    if (log_callback_) {
        log_callback_(message);
    } else {
        std::cout << message << std::endl;
    }
}

bool NetworkManager::validateInterface(const std::string& ifname) const {
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    return rtnl_link_get_by_name(link_cache, ifname.c_str()) != nullptr;
}

bool NetworkManager::setDynamicIP(const std::string& ifname) {
    if (!validateInterface(ifname)) {
        log("Error: Interface " + ifname + " not found");
        return false;
    }
    
    if (!stopDynamicIP(ifname)) {
        log("Warning: Failed to stop existing DHCP client");
    }
    
    log("Setting DHCP for interface: " + ifname);
    return dhcp_client_->start(ifname);
}

bool NetworkManager::stopDynamicIP(const std::string& ifname) {
    if (ifname.empty()) {
        log("Warning: Empty interface name");
        return false;
    }
    
    log("Stopping DHCP for interface: " + ifname);
    return dhcp_client_->stop(ifname);
}

bool NetworkManager::setStaticIP(const std::string& ifname, const std::string& ip, 
                               uint8_t prefix_len, const std::string& gateway) {
    if (!validateInterface(ifname)) {
        log("Error: Interface " + ifname + " not found");
        return false;
    }
    
    struct nl_sock* sock = netlink_mgr_.getSocket();
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    
    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    int ifindex = rtnl_link_get_ifindex(link);
    
    // Remove existing addresses
    struct nl_cache* addr_cache = netlink_mgr_.getAddrCache();
    struct nl_object* obj;
    nl_cache_foreach(addr_cache, obj) {
        struct rtnl_addr* addr = (struct rtnl_addr*)obj;
        if (rtnl_addr_get_ifindex(addr) == ifindex) {
            rtnl_addr_delete(sock, addr, 0);
        }
    }
    
    // Add new address
    struct nl_addr* nl_addr;
    std::string ip_with_prefix = ip + "/" + std::to_string(prefix_len);
    int err = nl_addr_parse(ip_with_prefix.c_str(), AF_INET, &nl_addr);
    
    if (err < 0) {
        log("Error: Failed to parse IP address: " + ip_with_prefix);
        return false;
    }
    
    struct rtnl_addr* rt_addr = rtnl_addr_alloc();
    rtnl_addr_set_ifindex(rt_addr, ifindex);
    rtnl_addr_set_local(rt_addr, nl_addr);
    rtnl_addr_set_family(rt_addr, AF_INET);
    
    err = rtnl_addr_add(sock, rt_addr, 0);
    if (err < 0) {
        log("Error: Failed to set static IP: " + std::string(nl_geterror(err)));
        nl_addr_put(nl_addr);
        rtnl_addr_put(rt_addr);
        return false;
    }
    
    nl_addr_put(nl_addr);
    rtnl_addr_put(rt_addr);
    
    // Add gateway if specified
    if (!gateway.empty()) {
        if (!addRoute(gateway, ifindex)) {
            log("Warning: Failed to set gateway");
        }
    }
    
    log("Static IP " + ip_with_prefix + " set on interface " + ifname);
    return true;
}

bool NetworkManager::addRoute(const std::string& gateway, int ifindex) {
    struct nl_sock* sock = netlink_mgr_.getSocket();
    
    struct nl_addr* gw_addr;
    int err = nl_addr_parse(gateway.c_str(), AF_INET, &gw_addr);
    if (err < 0) {
        log("Error: Failed to parse gateway address: " + gateway);
        return false;
    }
    
    struct rtnl_route* route = rtnl_route_alloc();
    struct nl_addr* dst_addr;
    nl_addr_parse("0.0.0.0/0", AF_INET, &dst_addr);
    
    rtnl_route_set_dst(route, dst_addr);
    rtnl_route_set_scope(route, RT_SCOPE_UNIVERSE);
    rtnl_route_set_table(route, RT_TABLE_MAIN);
    
    struct rtnl_nexthop* nh = rtnl_route_nh_alloc();
    rtnl_route_nh_set_gateway(nh, gw_addr);
    rtnl_route_nh_set_ifindex(nh, ifindex);
    rtnl_route_add_nexthop(route, nh);
    
    err = rtnl_route_add(sock, route, 0);
    if (err < 0) {
        log("Error: Failed to add route: " + std::string(nl_geterror(err)));
        nl_addr_put(gw_addr);
        nl_addr_put(dst_addr);
        rtnl_route_put(route);
        return false;
    }
    
    nl_addr_put(gw_addr);
    nl_addr_put(dst_addr);
    rtnl_route_put(route);
    return true;
}

std::optional<NetworkManager::InterfaceInfo> NetworkManager::getInterfaceInfo(const std::string& ifname) {
    if (!validateInterface(ifname)) {
        log("Error: Interface " + ifname + " not found");
        return std::nullopt;
    }
    
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    struct nl_cache* addr_cache = netlink_mgr_.getAddrCache();
    struct nl_cache* route_cache = netlink_mgr_.getRouteCache();
    
    InterfaceInfo info;
    info.name = ifname;
    
    // Get interface status
    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    info.is_up = (rtnl_link_get_flags(link) & IFF_UP) != 0;
    rtnl_link_put(link);
    
    // Get IP address
    struct nl_object* obj;
    nl_cache_foreach(addr_cache, obj) {
        struct rtnl_addr* addr = (struct rtnl_addr*)obj;
        if (rtnl_addr_get_ifindex(addr) == rtnl_link_get_ifindex(link) && 
            rtnl_addr_get_family(addr) == AF_INET) {
            struct nl_addr* local = rtnl_addr_get_local(addr);
            char ip_buf[INET_ADDRSTRLEN];
            nl_addr2str(local, ip_buf, sizeof(ip_buf));
            info.ip = ip_buf;
            break;
        }
    }
    
    // Get gateway
    nl_cache_foreach(route_cache, obj) {
        struct rtnl_route* route = (struct rtnl_route*)obj;
        if (rtnl_route_get_family(route) == AF_INET) {
            struct nl_addr* dst = rtnl_route_get_dst(route);
            if (dst && nl_addr_get_prefixlen(dst) == 0) {
                struct rtnl_nexthop* nh = rtnl_route_nexthop_n(route, 0);
                if (nh && rtnl_route_nh_get_ifindex(nh) == rtnl_link_get_ifindex(link)) {
                    struct nl_addr* gw = rtnl_route_nh_get_gateway(nh);
                    if (gw) {
                        char gw_buf[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, nl_addr_get_binary_addr(gw), gw_buf, sizeof(gw_buf));
                        info.gateway = gw_buf;
                        break;
                    }
                }
            }
        }
    }
    
    return info;
}

bool NetworkManager::bringInterfaceUp(const std::string& ifname) {
    struct nl_sock* sock = netlink_mgr_.getSocket();
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    
    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    if (!link) {
        log("Error: Interface " + ifname + " not found");
        return false;
    }
    
    struct rtnl_link* new_link = rtnl_link_alloc();
    rtnl_link_set_flags(new_link, IFF_UP);
    
    int err = rtnl_link_change(sock, link, new_link, 0);
    rtnl_link_put(link);
    rtnl_link_put(new_link);
    
    if (err < 0) {
        log("Error: Failed to bring interface up: " + std::string(nl_geterror(err)));
        return false;
    }
    
    log("Interface " + ifname + " brought up successfully");
    return true;
}

bool NetworkManager::bringInterfaceDown(const std::string& ifname) {
    struct nl_sock* sock = netlink_mgr_.getSocket();
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    
    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    if (!link) {
        log("Error: Interface " + ifname + " not found");
        return false;
    }
    
    struct rtnl_link* new_link = rtnl_link_alloc();
    rtnl_link_unset_flags(new_link, IFF_UP);
    
    int err = rtnl_link_change(sock, link, new_link, 0);
    rtnl_link_put(link);
    rtnl_link_put(new_link);
    
    if (err < 0) {
        log("Error: Failed to bring interface down: " + std::string(nl_geterror(err)));
        return false;
    }
    
    log("Interface " + ifname + " brought down successfully");
    return true;
}

void NetworkManager::setLogCallback(LogCallback callback) {
    log_callback_ = callback;
}