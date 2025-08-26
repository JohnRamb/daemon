#ifndef NETLINK_MANAGER_H
#define NETLINK_MANAGER_H

#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/route/link.h>
#include <netlink/route/addr.h>
#include <netlink/route/route.h>
#include <netlink/cache.h>
#include <functional>
#include <map>
#include <string>

class NetlinkManager {
public:
    using LinkCallback = std::function<void(struct nl_msg*)>;
    using AddrCallback = std::function<void(struct nl_msg*)>;
    using RouteCallback = std::function<void(struct nl_msg*)>;

    NetlinkManager();
    ~NetlinkManager();

    void init();
    int getSocketFd() const;
    struct nl_sock* getSocket() const; // Добавлен новый метод
    void processEvents();

    void setLinkCallback(LinkCallback callback);
    void setAddrCallback(AddrCallback callback);
    void setRouteCallback(RouteCallback callback);

    struct nl_cache* getLinkCache() const;
    struct nl_cache* getAddrCache() const;
    struct nl_cache* getRouteCache() const;

    std::string getInterfaceName(int ifindex) const;
    int getInterfaceIndex(const std::string& ifname) const;

private:
    struct nl_sock* nl_sock_;
    struct nl_cache* link_cache_;
    struct nl_cache* addr_cache_;
    struct nl_cache* route_cache_;

    LinkCallback link_callback_;
    AddrCallback addr_callback_;
    RouteCallback route_callback_;

    static int netlinkCallback(struct nl_msg* msg, void* arg);
    void processLinkMessage(struct nl_msg* msg);
    void processAddrMessage(struct nl_msg* msg);
    void processRouteMessage(struct nl_msg* msg);
};

#endif // NETLINK_MANAGER_H