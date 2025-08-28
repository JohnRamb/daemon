#ifndef NETWORK_DAEMON_H
#define NETWORK_DAEMON_H

#include "netlink_manager.h"
#include "unix_socket_server.h"
#include "network_manager.h"
#include "command_processor.h"
#include "event_loop.h"
#include <memory>

class NetworkDaemon {
public:
    NetworkDaemon();
    ~NetworkDaemon();

    void run();
    void stop();
    std::string getTimestamp() const;

private:
    NetlinkManager netlink_mgr_;
    UnixSocketServer unix_server_;
    NetworkManager network_mgr_;
    std::unique_ptr<CommandProcessor> command_processor_;
    EventLoop loop_;

    void setupSignalHandlers();
    void handleNetlinkEvent(int fd, uint32_t events);
    
    // Методы-колбэки для netlink событий
    void handleLinkEvent(struct nl_msg* msg);
    void handleAddrEvent(struct nl_msg* msg);
    void handleRouteEvent(struct nl_msg* msg);
    
    // Вспомогательные методы для форматирования событий
    std::string formatLinkEvent(struct nlmsghdr* nlh, struct ifinfomsg* ifi, 
                               struct nlattr* tb[], const char* ifname, const char* mac_str);
    std::string formatAddrEvent(struct nlmsghdr* nlh, struct ifaddrmsg* ifa,
                               struct nlattr* tb[], const char* ifname, 
                               const char* ip_str, const char* mask_str, int mask_len);
    std::string formatRouteEvent(struct nlmsghdr* nlh, struct rtmsg* rtm,
                                struct nlattr* tb[], const char* ifname,
                                const char* dst_str, const char* gw_str);
};

#endif // NETWORK_DAEMON_H