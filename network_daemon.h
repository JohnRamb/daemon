#ifndef NETWORK_DAEMON_H
#define NETWORK_DAEMON_H

#include "event_loop.h"
#include "netlink_manager.h"
#include "unix_socket_server.h"
#include "network_manager.h"

class NetworkDaemon {
public:
    NetworkDaemon();
    ~NetworkDaemon();

    void run();
    void stop();

private:
    EventLoop loop_;
    NetlinkManager netlink_mgr_;
    UnixSocketServer unix_server_;
    NetworkManager network_mgr_;

    void setupSignalHandlers();
    void handleNetlinkEvent(int fd, uint32_t events);
    void handleUnixCommand(int client_fd, const std::string& command);
    void processCommand(const std::string& command, int client_fd);
};

#endif // NETWORK_DAEMON_H