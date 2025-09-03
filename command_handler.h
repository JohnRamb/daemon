#pragma once
#include <string>
#include <vector>
#include <memory>

class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;
    virtual std::string handleCommand(const std::vector<std::string>& tokens) = 0;
    virtual bool canHandle(const std::string& command) const = 0;
};

using CommandHandlerPtr = std::shared_ptr<ICommandHandler>;