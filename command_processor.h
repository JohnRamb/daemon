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
    CommandProcessor(NetworkManager& network_mgr, std::unique_ptr<CommandSerializer> serializer);
    
    std::string processCommand(const std::string& command);
    std::string getTimestamp() const;

private:
    NetworkManager& network_mgr_;
    std::unique_ptr<CommandSerializer> serializer_;
    
    std::string handleEnumerate();
    std::string handleOn(const std::vector<std::string>& tokens);
    std::string handleOff(const std::vector<std::string>& tokens);
    std::string handleDhcpOn(const std::vector<std::string>& tokens);
    std::string handleDhcpOff(const std::vector<std::string>& tokens);
    std::string handleSetStatic(const std::vector<std::string>& tokens);
};

#endif