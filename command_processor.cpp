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
#include <regex>

CommandProcessor::CommandProcessor(NetworkManager& network_mgr,
                                 std::unique_ptr<CommandSerializer> serializer)
    : network_mgr_(network_mgr), serializer_(std::move(serializer)) {}

std::string CommandProcessor::processCommand(const std::string& command) {
    std::cout << "[" << getTimestamp() << "] CommandProcessor: Received command: " << command << std::endl;

    std::vector<std::string> tokens = serializer_->parseCommand(command);
    if (tokens.empty()) {
        return serializer_->serializeResponse("error", "invalid S-expression format");
    }

    std::string cmd = tokens[0];
    std::string response;

    if (cmd == "enumerate") {
        response = handleEnumerate();
    } else if (cmd == "on") {
        response = handleOn(tokens);
    } else if (cmd == "off") {
        response = handleOff(tokens);
    } else if (cmd == "dhcpOn") {
        response = handleDhcpOn(tokens);
    } else if (cmd == "dhcpOff") {
        response = handleDhcpOff(tokens);
    } else if (cmd == "setIface") {
        response = handleSetStatic(tokens);
    } else {
        response = "error(unknown command or invalid arguments)";
    }

    std::string full_response = serializer_->serializeResponse(cmd, response);
    std::cout << "[" << getTimestamp() << "] CommandProcessor: Prepared response: " << full_response << std::endl;
    
    return full_response;
}

std::string CommandProcessor::handleEnumerate() {
  //  return network_mgr_.enumerateInterfaces();
}

std::string CommandProcessor::handleOn(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        return "error(missing parameters)";
    }
   // return network_mgr_.enableInterface(tokens[1]);
}

std::string CommandProcessor::handleOff(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        return "error(missing parameters)";
    }
  //  return network_mgr_.disableInterface(tokens[1]);
}

// Аналогично для других handle-методов...

std::string CommandProcessor::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
