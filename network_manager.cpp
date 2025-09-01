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

NetworkManager::NetworkManager(NetlinkManager& netlink_mgr) : netlink_mgr_(netlink_mgr) {}

std::string NetworkManager::setDynamicIP(const std::string& ifname) {
    std::cout << "DEBUG: setDynamicIP called for interface: " << ifname << std::endl;

    if (ifname.empty()) {
        std::cerr << "ERROR: No interface specified in setDynamicIP" << std::endl;
        return "error(no interface specified)";
    }

    //std::string name = "eno1"; // test
    std::cout << "INFO: Setting DHCP for interface: " << ifname << std::endl;
    //std::cout << "DEBUG: Using interface name: " << name << " for dhcpcd" << std::endl;
    
    stopDhcpcd(ifname);

    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "ERROR: fork() failed in setDynamicIP: " << strerror(errno) << std::endl;
        return "error(fork failed)";
    } else if (pid == 0) {
        std::cout << "DEBUG: Child process executing: dhcpcd -n " << ifname << std::endl;
        execlp("dhcpcd", "dhcpcd", "-n", ifname.c_str(), nullptr);
        std::cerr << "ERROR: execlp failed: " << strerror(errno) << std::endl;
        _exit(EXIT_FAILURE);
    }

    std::cout << "DEBUG: Waiting for dhcpcd process to complete, PID: " << pid << std::endl;
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        std::cout << "INFO: dhcpcd completed successfully for interface: " << ifname << std::endl;
        return getInterfaceInfo(ifname);
    } else {
        std::cerr << "ERROR: dhcpcd failed for interface: " << ifname 
                  << ", exit status: " << WEXITSTATUS(status) << std::endl;
        return "error(dhcpcd failed)";
    }
}

void NetworkManager::stopDhcpcd(const std::string& ifname) {
    std::cout << "DEBUG: stopDhcpcd called for interface: " << ifname << std::endl;
    
    if (!ifname.empty()) {
        std::cout << "INFO: Stopping dhcpcd for interface: " << ifname << std::endl;
        
        pid_t pid = fork();
        if (pid == 0) {
            std::cout << "DEBUG: Child process executing: dhcpcd -k " << ifname << std::endl;
            execlp("dhcpcd", "dhcpcd", "-k", ifname.c_str(), nullptr);
            std::cerr << "ERROR: execlp failed in stopDhcpcd: " << strerror(errno) << std::endl;
            _exit(EXIT_FAILURE);
        }
        
        std::cout << "DEBUG: Waiting for dhcpcd stop process, PID: " << pid << std::endl;
        waitpid(pid, nullptr, 0);
        std::cout << "INFO: dhcpcd stopped for interface: " << ifname << std::endl;
    } else {
        std::cout << "WARNING: stopDhcpcd called with empty interface name" << std::endl;
    }
}

void NetworkManager::setStaticIP(const std::string& ifname, const std::string& ip_mask, const std::string& gateway) {
    struct nl_sock* sock = netlink_mgr_.getSocket();
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();

    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    if (!link) {
        std::cerr << "Interface " << ifname << " not found" << std::endl;
        return;
    }

    int ifindex = rtnl_link_get_ifindex(link);
    struct nl_addr* addr;
    nl_addr_parse(ip_mask.c_str(), AF_INET, &addr);
    struct rtnl_addr* rt_addr = rtnl_addr_alloc();
    rtnl_addr_set_ifindex(rt_addr, ifindex);
    rtnl_addr_set_local(rt_addr, addr);
    rtnl_addr_set_family(rt_addr, AF_INET);

    int err = rtnl_addr_add(sock, rt_addr, 0);
    if (err < 0) {
        std::cerr << "Failed to set IP " << ip_mask << " on " << ifname << ": " << nl_geterror(err) << std::endl;
    } else {
        std::cout << "Статический IP установлен: " << ip_mask << " на интерфейсе " << ifname << std::endl;
    }

    nl_addr_put(addr);
    rtnl_addr_put(rt_addr);
    rtnl_link_put(link);

    if (!gateway.empty() && gateway != "none") {
        struct nl_addr* gw_addr;
        nl_addr_parse(gateway.c_str(), AF_INET, &gw_addr);
        struct rtnl_route* route = rtnl_route_alloc();
        struct nl_addr* dst_addr;
        nl_addr_parse("0.0.0.0/0", AF_INET, &dst_addr);
        rtnl_route_set_dst(route, dst_addr);
        rtnl_route_set_scope(route, RT_SCOPE_UNIVERSE);
        rtnl_route_set_protocol(route, RTPROT_STATIC);
        rtnl_route_set_family(route, AF_INET);
        rtnl_route_set_table(route, RT_TABLE_MAIN);

        struct rtnl_nexthop* nh = rtnl_route_nh_alloc();
        rtnl_route_nh_set_gateway(nh, gw_addr);
        rtnl_route_nh_set_ifindex(nh, ifindex);
        rtnl_route_add_nexthop(route, nh);

        err = rtnl_route_add(sock, route, 0);
        if (err < 0) {
            std::cerr << "Failed to set gateway " << gateway << ": " << nl_geterror(err) << std::endl;
        } else {
            std::cout << "Шлюз установлен: " << gateway << " через интерфейс " << ifindex << std::endl;
        }

        nl_addr_put(dst_addr);
        nl_addr_put(gw_addr);
        rtnl_route_put(route);
    }
}

std::string NetworkManager::getInterfaceInfo(const std::string& ifname) {
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    struct nl_cache* addr_cache = netlink_mgr_.getAddrCache();
    struct nl_cache* route_cache = netlink_mgr_.getRouteCache();
    if (!link_cache || !addr_cache || !route_cache) {
        return "error(no cache available)";
    }

    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    if (!link) {
        return "error(interface not found)";
    }

    int ifindex = rtnl_link_get_ifindex(link);
    unsigned int flags = rtnl_link_get_flags(link);
    std::string flag_str = (flags & IFF_UP) ? "UP" : "DOWN";

    std::string ip_str = "none";
    std::string mask_str = "none";
    struct nl_object* obj = nl_cache_get_first(addr_cache);
    while (obj) {
        struct rtnl_addr* addr = (struct rtnl_addr*)obj;
        if (rtnl_addr_get_ifindex(addr) == ifindex && rtnl_addr_get_family(addr) == AF_INET) {
            struct nl_addr* local = rtnl_addr_get_local(addr);
            if (local) {
                char ip_buf[INET_ADDRSTRLEN] = {0};
                nl_addr2str(local, ip_buf, sizeof(ip_buf));
                ip_str = ip_buf;
                size_t slash_pos = ip_str.find('/');
                if (slash_pos != std::string::npos) {
                    mask_str = ip_str.substr(slash_pos + 1);
                    ip_str = ip_str.substr(0, slash_pos);
                }
                break;
            }
        }
        obj = nl_cache_get_next(obj);
    }

    std::string gateway_str = "none";
    obj = nl_cache_get_first(route_cache);
    while (obj) {
        struct rtnl_route* route = (struct rtnl_route*)obj;
        if (rtnl_route_get_family(route) == AF_INET) {
            struct nl_addr* dst = rtnl_route_get_dst(route);
            if (dst && nl_addr_get_prefixlen(dst) == 0) { // Проверяем маршрут по умолчанию (0.0.0.0/0)
                struct rtnl_nexthop* nh = rtnl_route_nexthop_n(route, 0);
                if (nh && rtnl_route_nh_get_ifindex(nh) == ifindex) {
                    struct nl_addr* gw = rtnl_route_nh_get_gateway(nh);
                    if (gw) {
                        char gw_buf[INET_ADDRSTRLEN] = {0};
                        inet_ntop(AF_INET, nl_addr_get_binary_addr(gw), gw_buf, sizeof(gw_buf));
                        gateway_str = gw_buf;
                        break;
                    }
                }
            }
        }
        obj = nl_cache_get_next(obj);
    }

    rtnl_link_put(link);

    std::stringstream ss;
    ss << ifname << ":" << ip_str << ":" << mask_str << ":" << flag_str << ":" << gateway_str;
    return ss.str();
}

bool NetworkManager::bringInterfaceUp(const std::string& ifname) {
    struct nl_sock* sock = netlink_mgr_.getSocket();
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    
    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    if (!link) {
        std::cerr << "Interface " << ifname << " not found" << std::endl;
        return false;
    }
    
    // Создаем новый объект link для изменения
    struct rtnl_link* new_link = rtnl_link_alloc();
    if (!new_link) {
        std::cerr << "Failed to allocate link object" << std::endl;
        rtnl_link_put(link);
        return false;
    }
    
    // Устанавливаем флаг UP
    rtnl_link_set_flags(new_link, IFF_UP);
    
    int err = rtnl_link_change(sock, link, new_link, 0);
    if (err < 0) {
        std::cerr << "Failed to bring interface " << ifname << " up: " << nl_geterror(err) << std::endl;
        rtnl_link_put(link);
        rtnl_link_put(new_link);
        return false;
    }
    
    std::cout << "Interface " << ifname << " brought up successfully" << std::endl;
    
    rtnl_link_put(link);
    rtnl_link_put(new_link);
    return true;
}

bool NetworkManager::bringInterfaceDown(const std::string& ifname) {
    struct nl_sock* sock = netlink_mgr_.getSocket();
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    
    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    if (!link) {
        std::cerr << "Interface " << ifname << " not found" << std::endl;
        return false;
    }
    
    // Создаем новый объект link для изменения
    struct rtnl_link* new_link = rtnl_link_alloc();
    if (!new_link) {
        std::cerr << "Failed to allocate link object" << std::endl;
        rtnl_link_put(link);
        return false;
    }
    
    // Снимаем флаг UP
    rtnl_link_unset_flags(new_link, IFF_UP);
    
    int err = rtnl_link_change(sock, link, new_link, 0);
    if (err < 0) {
        std::cerr << "Failed to bring interface " << ifname << " down: " << nl_geterror(err) << std::endl;
        rtnl_link_put(link);
        rtnl_link_put(new_link);
        return false;
    }
    
    std::cout << "Interface " << ifname << " brought down successfully" << std::endl;
    
    rtnl_link_put(link);
    rtnl_link_put(new_link);
    return true;
}