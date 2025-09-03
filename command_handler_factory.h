#pragma once
#include "command_handler.h"
#include "network_manager.h"
#include <map>
#include <memory>

class CommandHandlerFactory {
public:
    CommandHandlerFactory(NetworkManager& network_mgr);
    CommandHandlerPtr getHandler(const std::string& command) const;
    
private:
    std::map<std::string, CommandHandlerPtr> handlers_;
};