#include "netlink_manager.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <net/if.h>


NetlinkManager::NetlinkManager() 
    : nl_sock_(nullptr), link_cache_(nullptr), addr_cache_(nullptr), route_cache_(nullptr) {}

NetlinkManager::~NetlinkManager() {
    if (route_cache_) nl_cache_free(route_cache_);
    if (addr_cache_) nl_cache_free(addr_cache_);
    if (link_cache_) nl_cache_free(link_cache_);
    if (nl_sock_) nl_socket_free(nl_sock_);
}

void NetlinkManager::init() {
    nl_sock_ = nl_socket_alloc();
    if (!nl_sock_) {
        throw std::runtime_error("Не удалось создать netlink сокет");
    }

    // Отключаем проверку последовательности
    nl_socket_disable_seq_check(nl_sock_);
    
    // Устанавливаем callback для обработки сообщений
    nl_socket_modify_cb(nl_sock_, NL_CB_MSG_IN, NL_CB_CUSTOM, &NetlinkManager::netlinkCallback, this);
    nl_socket_set_nonblocking(nl_sock_);

    if (nl_connect(nl_sock_, NETLINK_ROUTE) < 0) {
        nl_socket_free(nl_sock_);
        throw std::runtime_error("Не удалось подключиться к netlink");
    }

    if (nl_socket_add_memberships(nl_sock_, RTNLGRP_LINK, RTNLGRP_IPV4_IFADDR, RTNLGRP_IPV4_ROUTE, 0) < 0) {
        nl_close(nl_sock_);
        nl_socket_free(nl_sock_);
        throw std::runtime_error("Не удалось подписаться на netlink группы");
    }

    if (rtnl_link_alloc_cache(nl_sock_, AF_UNSPEC, &link_cache_) < 0) {
        throw std::runtime_error("Не удалось загрузить кэш ссылок");
    }

    if (rtnl_addr_alloc_cache(nl_sock_, &addr_cache_) < 0) {
        throw std::runtime_error("Не удалось загрузить кэш адресов");
    }

    if (rtnl_route_alloc_cache(nl_sock_, AF_INET, 0, &route_cache_) < 0) {
        throw std::runtime_error("Не удалось загрузить кэш маршрутов");
    }
}

int NetlinkManager::getSocketFd() const {
    return nl_socket_get_fd(nl_sock_);
}

struct nl_sock* NetlinkManager::getSocket() const {
    return nl_sock_;
}

void NetlinkManager::processEvents() {
    int err = nl_recvmsgs_default(nl_sock_);
    if (err < 0) {
        // Игнорируем ошибки EAGAIN и временные ошибки
        if (err != -NLE_AGAIN && err != -NLE_INTR) {
            std::cerr << "Ошибка получения netlink сообщений: " << nl_geterror(err) << std::endl;
        }
    }
}

int NetlinkManager::netlinkCallback(struct nl_msg* msg, void* arg) {
    NetlinkManager* self = static_cast<NetlinkManager*>(arg);
    struct nlmsghdr* nlh = nlmsg_hdr(msg);
    
    // Игнорируем сообщения типа NLMSG_DONE (тип 3)
    if (nlh->nlmsg_type == NLMSG_DONE) {
        return NL_OK;
    }
    
    switch (nlh->nlmsg_type) {
        case RTM_NEWLINK:
        case RTM_DELLINK:
            self->processLinkMessage(msg);
            break;
        case RTM_NEWADDR:
        case RTM_DELADDR:
            self->processAddrMessage(msg);
            break;
        case RTM_NEWROUTE:
        case RTM_DELROUTE:
            self->processRouteMessage(msg);
            break;
        default:
            std::cout << "Необработанное netlink сообщение, тип=" << nlh->nlmsg_type << std::endl;
            break;
    }
    
    return NL_OK;
}

void NetlinkManager::processLinkMessage(struct nl_msg* msg) {
    if (link_callback_) {
        link_callback_(msg);
    }
}

void NetlinkManager::processAddrMessage(struct nl_msg* msg) {
    if (addr_callback_) {
        addr_callback_(msg);
    }
}

void NetlinkManager::processRouteMessage(struct nl_msg* msg) {
    if (route_callback_) {
        route_callback_(msg);
    }
}

void NetlinkManager::setLinkCallback(LinkCallback callback) {
    link_callback_ = callback;
}

void NetlinkManager::setAddrCallback(AddrCallback callback) {
    addr_callback_ = callback;
}

void NetlinkManager::setRouteCallback(RouteCallback callback) {
    route_callback_ = callback;
}

struct nl_cache* NetlinkManager::getLinkCache() const {
    return link_cache_;
}

struct nl_cache* NetlinkManager::getAddrCache() const {
    return addr_cache_;
}

struct nl_cache* NetlinkManager::getRouteCache() const {
    return route_cache_;
}

std::string NetlinkManager::getInterfaceName(int ifindex) const {
    char ifname[IF_NAMESIZE];
    const char* name = rtnl_link_i2name(link_cache_, ifindex, ifname, sizeof(ifname));
    if (!name) {
        snprintf(ifname, sizeof(ifname), "unknown-%d", ifindex);
    }
    return std::string(ifname);
}

int NetlinkManager::getInterfaceIndex(const std::string& ifname) const {
    return rtnl_link_name2i(link_cache_, ifname.c_str());
}