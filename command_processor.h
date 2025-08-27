#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include "unix_socket_server.h"
#include "netlink_manager.h"
#include "network_manager.h"
#include "command_serializer.h"
#include <functional>
#include <memory>
#include <string>

class CommandProcessor {
public:
    using ClientHandler = std::function<void(int, const std::string&)>;

    CommandProcessor(UnixSocketServer& server, NetlinkManager& netlink_mgr, NetworkManager& network_mgr,
                     std::unique_ptr<CommandSerializer> serializer);

    void handleCommand(int client_fd, const std::string& command);

private:
    UnixSocketServer& server_;
    NetlinkManager& netlink_mgr_;
    NetworkManager& network_mgr_;
    std::unique_ptr<CommandSerializer> serializer_;

    std::string getTimestamp() const;
    std::string handleEnumerate();
    std::string handleOn(const std::string& ifname);
    std::string handleOff(const std::string& ifname);
    std::string handleDhcpOn(const std::string& ifname);
    std::string handleDhcpOff(const std::string& ifname);
    std::string handleSetStatic(const std::string& ifname, const std::string& ip, 
                               const std::string& prefix, const std::string& gateway);
};

#endif