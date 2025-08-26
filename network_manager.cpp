#include "network_manager.h"
#include "netlink_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <net/if.h>

NetworkManager::NetworkManager(NetlinkManager& netlink_mgr) : netlink_mgr_(netlink_mgr) {}

void NetworkManager::setStaticIP(const std::string& ifname, const std::string& ip_mask, const std::string& gateway) {
    int ifindex = netlink_mgr_.getInterfaceIndex(ifname);
    if (ifindex == 0) {
        std::cerr << "Интерфейс не найден: " << ifname << std::endl;
        return;
    }

    size_t slash_pos = ip_mask.find('/');
    if (slash_pos == std::string::npos) {
        std::cerr << "Неверный формат IP/маски: " << ip_mask << std::endl;
        return;
    }

    std::string ip_str = ip_mask.substr(0, slash_pos);
    std::string prefix_str = ip_mask.substr(slash_pos + 1);
    int prefixlen = std::stoi(prefix_str);

    removeIP(ifname);

    struct rtnl_addr* addr = rtnl_addr_alloc();
    if (!addr) {
        std::cerr << "Не удалось выделить память для адреса" << std::endl;
        return;
    }

    rtnl_addr_set_ifindex(addr, ifindex);
    rtnl_addr_set_family(addr, AF_INET);
    rtnl_addr_set_prefixlen(addr, prefixlen);

    struct nl_addr* local_addr;
    if (nl_addr_parse(ip_str.c_str(), AF_INET, &local_addr) < 0) {
        std::cerr << "Неверный формат IP адреса: " << ip_str << std::endl;
        rtnl_addr_put(addr);
        return;
    }

    rtnl_addr_set_local(addr, local_addr);

    if (rtnl_addr_add(netlink_mgr_.getSocket(), addr, 0) < 0) {
        std::cerr << "Ошибка добавления адреса" << std::endl;
    } else {
        std::cout << "Статический IP установлен: " << ip_str << "/" << prefixlen 
                  << " на интерфейсе " << ifname << std::endl;
    }

    if (!gateway.empty() && gateway != "0.0.0.0") {
        addGateway(gateway, ifindex);
    }

    nl_addr_put(local_addr);
    rtnl_addr_put(addr);
}

std::string NetworkManager::setDynamicIP(const std::string& ifname) {
    std::cout << "Запуск DHCP для интерфейса: " << ifname << std::endl;
    
    int ifindex = netlink_mgr_.getInterfaceIndex(ifname);
    if (ifindex == 0) {
        return "Интерфейс " + ifname + " не найден";
    }

    struct rtnl_link* link = rtnl_link_get(netlink_mgr_.getLinkCache(), ifindex);
    if (!link) {
        return "Не удалось получить информацию об интерфейсе " + ifname;
    }

    unsigned int flags = rtnl_link_get_flags(link);
    rtnl_link_put(link);

    if (!(flags & IFF_UP)) {
        return "Интерфейс " + ifname + " не активен (DOWN)";
    }

    stopDhcpcd(ifname);

    pid_t pid = fork();
    if (pid == -1) {
        return "Ошибка fork: " + std::string(strerror(errno));
    }

    if (pid == 0) {
        closeAllFileDescriptors();
        
        int log_fd = open("/tmp/dhcpcd.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd != -1) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        execlp("dhcpcd", "dhcpcd", "-4", "-B", ifname.c_str(), nullptr);
        std::cerr << "Ошибка запуска dhcpcd: " << strerror(errno) << std::endl;
        exit(1);
    } else {
        dhcpcd_pids_[ifname] = pid;
        std::cout << "Запущен dhcpcd для " << ifname << " с PID " << pid << std::endl;
        
        if (waitForDhcpcd(ifname, 30)) {
            return "DHCP успешно настроен для " + ifname;
        } else {
            return "Таймаут ожидания DHCP для " + ifname;
        }
    }
}

void NetworkManager::stopDhcpcd(const std::string& ifname) {
    auto it = dhcpcd_pids_.find(ifname);
    if (it != dhcpcd_pids_.end()) {
        pid_t pid = it->second;
        std::cout << "Остановка dhcpcd для " << ifname << " (PID: " << pid << ")" << std::endl;
        
        kill(pid, SIGTERM);
        int status;
        waitpid(pid, &status, 0);
        
        dhcpcd_pids_.erase(it);
        std::cout << "dhcpcd остановлен для " << ifname << std::endl;
    }
}

std::string NetworkManager::getStatus() const {
    std::string status;
    char ifname[IF_NAMESIZE];
    
    // Обновляем кэш адресов
    nl_cache_refill(netlink_mgr_.getSocket(), const_cast<struct nl_cache*>(netlink_mgr_.getAddrCache()));
    
    struct rtnl_addr* addr = nullptr;
    for (addr = reinterpret_cast<rtnl_addr*>(nl_cache_get_first(netlink_mgr_.getAddrCache()));
         addr;
         addr = reinterpret_cast<rtnl_addr*>(nl_cache_get_next(reinterpret_cast<nl_object*>(addr)))) {
        
        if (rtnl_addr_get_family(addr) == AF_INET) {
            int ifindex = rtnl_addr_get_ifindex(addr);
            std::string name = netlink_mgr_.getInterfaceName(ifindex);
            
            struct nl_addr* local = rtnl_addr_get_local(addr);
            if (local) {
                char ip_buf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, nl_addr_get_binary_addr(local), ip_buf, sizeof(ip_buf));
                uint32_t prefixlen = nl_addr_get_prefixlen(local);
                
                std::string mask = prefixToMask(prefixlen);
                std::string gateway = getGateway(ifindex);
                std::string dns = getDNS();
                
                status += name + ": IP=" + ip_buf + ", Mask=" + mask + 
                          ", Gateway=" + gateway + ", DNS=" + dns + "\n";
            }
        }
    }
    
    return status.empty() ? "Нет активных IP" : status;
}

std::string NetworkManager::getDNS() const {
    std::ifstream resolv_file("/etc/resolv.conf");
    std::vector<std::string> dns_servers;
    std::string line;
    
    while (std::getline(resolv_file, line)) {
        if (line.find("nameserver ") == 0) {
            std::string dns = line.substr(11);
            dns.erase(0, dns.find_first_not_of(" \t\n\r\f\v"));
            dns.erase(dns.find_last_not_of(" \t\n\r\f\v") + 1);
            if (!dns.empty()) {
                dns_servers.push_back(dns);
            }
        }
    }
    
    if (dns_servers.empty()) {
        return "Неизвестно";
    }
    
    std::string result;
    for (size_t i = 0; i < dns_servers.size(); ++i) {
        result += dns_servers[i];
        if (i < dns_servers.size() - 1) {
            result += ",";
        }
    }
    
    return result;
}

std::string NetworkManager::prefixToMask(uint32_t prefix) const {
    uint32_t mask = (0xFFFFFFFF << (32 - prefix)) & 0xFFFFFFFF;
    char mask_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &mask, mask_buf, sizeof(mask_buf));
    return std::string(mask_buf);
}

std::string NetworkManager::getGateway(int if_index) const {
    struct rtnl_route* route = nullptr;
    std::string gateway = "Неизвестно";
    
    for (route = reinterpret_cast<rtnl_route*>(nl_cache_get_first(netlink_mgr_.getRouteCache())); 
         route; 
         route = reinterpret_cast<rtnl_route*>(nl_cache_get_next(reinterpret_cast<nl_object*>(route)))) {
        
        if (rtnl_route_get_table(route) != RT_TABLE_MAIN) {
            continue;
        }
        
        struct rtnl_nexthop* nh = rtnl_route_nexthop_n(route, 0);
        if (nh && rtnl_route_nh_get_ifindex(nh) == if_index) {
            struct nl_addr* gw = rtnl_route_nh_get_gateway(nh);
            if (gw) {
                char addr_buf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, nl_addr_get_binary_addr(gw), addr_buf, sizeof(addr_buf));
                gateway = addr_buf;
                break;
            }
        }
    }
    
    return gateway;
}

void NetworkManager::addGateway(const std::string& gateway, int ifindex) {
    struct rtnl_route* route = rtnl_route_alloc();
    if (!route) {
        std::cerr << "Не удалось выделить память для маршрута" << std::endl;
        return;
    }

    rtnl_route_set_family(route, AF_INET);
    rtnl_route_set_table(route, RT_TABLE_MAIN);
    rtnl_route_set_scope(route, RT_SCOPE_UNIVERSE);
    rtnl_route_set_type(route, RTN_UNICAST);

    struct nl_addr* gw_addr;
    if (nl_addr_parse(gateway.c_str(), AF_INET, &gw_addr) < 0) {
        std::cerr << "Неверный формат адреса шлюза: " << gateway << std::endl;
        rtnl_route_put(route);
        return;
    }

    struct rtnl_nexthop* nh = rtnl_route_nh_alloc();
    if (nh) {
        rtnl_route_nh_set_ifindex(nh, ifindex);
        rtnl_route_nh_set_gateway(nh, gw_addr);
        rtnl_route_add_nexthop(route, nh);
    }

    if (rtnl_route_add(netlink_mgr_.getSocket(), route, 0) < 0) {
        std::cerr << "Ошибка добавления маршрута к шлюзу" << std::endl;
    } else {
        std::cout << "Шлюз установлен: " << gateway << " через интерфейс " << ifindex << std::endl;
    }

    nl_addr_put(gw_addr);
    if (nh) rtnl_route_nh_free(nh);
    rtnl_route_put(route);
}

void NetworkManager::removeIP(const std::string& ifname) {
    int ifindex = netlink_mgr_.getInterfaceIndex(ifname);
    if (ifindex == 0) {
        std::cerr << "Интерфейс не найден: " << ifname << std::endl;
        return;
    }

    std::cout << "Удаление IP адресов с интерфейса: " << ifname << std::endl;

    struct rtnl_addr* addr = nullptr;
    for (addr = reinterpret_cast<rtnl_addr*>(nl_cache_get_first(netlink_mgr_.getAddrCache()));
         addr;
         addr = reinterpret_cast<rtnl_addr*>(nl_cache_get_next(reinterpret_cast<nl_object*>(addr)))) {
        
        if (rtnl_addr_get_ifindex(addr) == ifindex && 
            rtnl_addr_get_family(addr) == AF_INET) {
            
            if (rtnl_addr_delete(netlink_mgr_.getSocket(), addr, 0) < 0) {
                std::cerr << "Ошибка удаления адреса" << std::endl;
            } else {
                std::cout << "IP адрес удален с интерфейса " << ifname << std::endl;
            }
        }
    }
}

bool NetworkManager::waitForDhcpcd(const std::string& ifname, int timeout_seconds) {
    int ifindex = netlink_mgr_.getInterfaceIndex(ifname);
    if (ifindex == 0) {
        return false;
    }

    auto start = std::chrono::steady_clock::now();
    
    while (true) {
        nl_cache_refill(netlink_mgr_.getSocket(), netlink_mgr_.getAddrCache());
        
        struct rtnl_addr* addr = nullptr;
        for (addr = reinterpret_cast<rtnl_addr*>(nl_cache_get_first(netlink_mgr_.getAddrCache()));
             addr;
             addr = reinterpret_cast<rtnl_addr*>(nl_cache_get_next(reinterpret_cast<nl_object*>(addr)))) {
            
            if (rtnl_addr_get_ifindex(addr) == ifindex && 
                rtnl_addr_get_family(addr) == AF_INET) {
                
                struct nl_addr* local = rtnl_addr_get_local(addr);
                if (local) {
                    char ip_buf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, nl_addr_get_binary_addr(local), ip_buf, sizeof(ip_buf));
                    std::cout << "Получен IP по DHCP: " << ip_buf << " на " << ifname << std::endl;
                    return true;
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        
        if (elapsed >= timeout_seconds) {
            std::cout << "Таймаут ожидания DHCP для " << ifname << std::endl;
            return false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void NetworkManager::closeAllFileDescriptors() {
    int max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd == -1) {
        max_fd = 1024;
    }
    
    for (int fd = 3; fd < max_fd; fd++) {
        close(fd);
    }
}