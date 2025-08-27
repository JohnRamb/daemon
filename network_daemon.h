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
};

#endif // NETWORK_DAEMON_H