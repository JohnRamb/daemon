#include "command_processor.h"
#include <sstream>
#include <chrono>
#include <iomanip>
#include <netlink/cache.h>
#include <netlink/route/link.h>
#include <net/if.h>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

CommandProcessor::CommandProcessor(UnixSocketServer& server, NetlinkManager& netlink_mgr, 
                                   NetworkManager& network_mgr, std::unique_ptr<CommandSerializer> serializer)
    : server_(server), netlink_mgr_(netlink_mgr), network_mgr_(network_mgr), serializer_(std::move(serializer)) {
    server_.setClientHandler(std::bind(&CommandProcessor::handleCommand, this,
                                      std::placeholders::_1, std::placeholders::_2));
}

std::string CommandProcessor::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void CommandProcessor::handleCommand(int client_fd, const std::string& command) {
    std::cout << "[" << getTimestamp() << "] CommandProcessor: Received command: " << command << std::endl;

    // Парсим команду с помощью сериализатора
    std::vector<std::string> tokens = serializer_->parseCommand(command);
    if (tokens.empty()) {
        std::string response = serializer_->serializeResponse("error", "invalid S-expression format");
        server_.sendResponse(client_fd, response);
        std::cout << "[" << getTimestamp() << "] CommandProcessor: Sent response: " << response << std::endl;
        return;
    }

    std::string cmd = tokens[0];
    std::string response;

    if (cmd == "enumerate" && tokens.size() == 1) {
        response = handleEnumerate();
    } else if (cmd == "on" && tokens.size() == 2) {
        response = handleOn(tokens[1]);
        return;
    } else if (cmd == "off" && tokens.size() == 2) {
        response = handleOff(tokens[1]);
    } else if (cmd == "dhcpOn" && tokens.size() == 2) {
        response = handleDhcpOn(tokens[1]);
        return;
    } else if (cmd == "dhcpOff" && tokens.size() == 2) {
        response = handleDhcpOff(tokens[1]);
        return;
    } else if (cmd == "setStatic" && tokens.size() == 5) {
        response = handleSetStatic(tokens[1], tokens[2], tokens[3], tokens[4]);
    } else {
        response = "error(unknown command or invalid arguments)";
    }

    std::string full_response = serializer_->serializeResponse(cmd, response);
    server_.sendResponse(client_fd, full_response);
    std::cout << "[" << getTimestamp() << "] CommandProcessor: Sent response: " << full_response << std::endl;
}

std::string CommandProcessor::handleEnumerate() {
    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    if (!link_cache) {
        return "error(no link cache)";
    }

    std::stringstream ss;
    struct nl_object* obj = nl_cache_get_first(link_cache);
    bool first_interface = true;
    
    ss << "enumerate(";
    
    while (obj) {
        struct rtnl_link* link = (struct rtnl_link*)obj;
        const char* ifname = rtnl_link_get_name(link);
        
        if (ifname) {
            if (!first_interface) {
                ss << " ";
            }
            
            // Получаем информацию об интерфейсе через существующий метод
            std::string interface_info = network_mgr_.getInterfaceInfo(ifname);
            
            // Парсим информацию (формат: ifname:ip:mask:flag:gateway)
            std::vector<std::string> parts;
            std::istringstream iss(interface_info);
            std::string part;
            
            while (std::getline(iss, part, ':')) {
                parts.push_back(part);
            }
            
            // Получаем MAC-адрес
            struct nl_addr* addr = rtnl_link_get_addr(link);
            std::string mac_str = "none";
            if (addr) {
                unsigned char* mac = (unsigned char*)nl_addr_get_binary_addr(addr);
                char mac_buf[18];
                snprintf(mac_buf, sizeof(mac_buf), "%02x-%02x-%02x-%02x-%02x-%02x",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                mac_str = mac_buf;
            }
            
            // Получаем флаги интерфейса
            unsigned int flags = rtnl_link_get_flags(link);
            char flags_buf[9];
            snprintf(flags_buf, sizeof(flags_buf), "%08x", flags);
            
            // Формируем информацию об интерфейсе
            ss << "iface=" << ifname 
               << " addr=" << (parts.size() >= 2 && parts[1] != "none" ? parts[1] : "none")
               << " mac=" << mac_str
               << " gateway=" << (parts.size() >= 5 && parts[4] != "none" ? parts[4] : "none")
               << " mask=" << (parts.size() >= 3 && parts[2] != "none" ? parts[2] : "none")
               << " flag=" << flags_buf;
            
            first_interface = false;
        }
        obj = nl_cache_get_next(obj);
    }
    
    ss << ")";
    
    return ss.str();
}

std::string CommandProcessor::handleOn(const std::string& ifname) {
    if (ifname.empty()) {
        return "error(no interface specified)";
    }

    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    if (!link_cache) {
        return "error(no link cache)";
    }

    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    if (!link) {
        return "error(interface not found)";
    }

    struct rtnl_link* change = rtnl_link_alloc();
    rtnl_link_set_flags(change, IFF_UP);
    int err = rtnl_link_change(netlink_mgr_.getSocket(), link, change, 0);
    rtnl_link_put(link);
    rtnl_link_put(change);

    if (err < 0) {
        return "error(failed to enable interface: " + std::string(nl_geterror(err)) + ")";
    }
    return "success(interface enabled)";
}

std::string CommandProcessor::handleOff(const std::string& ifname) {
    if (ifname.empty()) {
        return "error(no interface specified)";
    }

    struct nl_cache* link_cache = netlink_mgr_.getLinkCache();
    if (!link_cache) {
        return "error(no link cache)";
    }

    struct rtnl_link* link = rtnl_link_get_by_name(link_cache, ifname.c_str());
    if (!link) {
        return "error(interface not found)";
    }

    struct rtnl_link* change = rtnl_link_alloc();
    rtnl_link_unset_flags(change, IFF_UP);
    int err = rtnl_link_change(netlink_mgr_.getSocket(), link, change, 0);
    rtnl_link_put(link);
    rtnl_link_put(change);

    if (err < 0) {
        return "error(failed to disable interface: " + std::string(nl_geterror(err)) + ")";
    }
    return "success(interface disabled)";
}

std::string CommandProcessor::handleDhcpOn(const std::string& ifname) {
    if (ifname.empty()) {
        return "error(no interface specified)";
    }
    return network_mgr_.setDynamicIP(ifname);
}

std::string CommandProcessor::handleDhcpOff(const std::string& ifname) {
    if (ifname.empty()) {
        return "error(no interface specified)";
    }
    network_mgr_.stopDhcpcd(ifname);
    return "success(DHCP disabled)";
}

std::string CommandProcessor::handleSetStatic(const std::string& ifname, const std::string& ip, 
                                             const std::string& prefix, const std::string& gateway) {
    if (ifname.empty() || ip.empty() || prefix.empty()) {
        return "error(invalid arguments)";
    }

    // Валидация IP-адреса
    struct in_addr addr;
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        return "error(invalid IP address)";
    }

    // Валидация префикса
    int prefix_len;
    try {
        prefix_len = std::stoi(prefix);
        if (prefix_len < 0 || prefix_len > 32) {
            return "error(invalid prefix length)";
        }
    } catch (...) {
        return "error(invalid prefix format)";
    }

    // Валидация шлюза (если указан)
    if (!gateway.empty() && gateway != "none") {
        if (inet_pton(AF_INET, gateway.c_str(), &addr) != 1) {
            return "error(invalid gateway address)";
        }
    }

    std::string ip_mask = ip + "/" + prefix;
    network_mgr_.setStaticIP(ifname, ip_mask, gateway.empty() ? "none" : gateway);
    return "success(static address set)";
}