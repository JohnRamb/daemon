#pragma once
#include <string>
#include <memory>
#include "network_event.h"

class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    virtual void handleLinkEvent(const NetworkEvent& event, bool isNew) = 0;
    virtual void handleAddrEvent(const NetworkEvent& event, bool isNew) = 0;
    virtual void handleRouteEvent(const NetworkEvent& event, bool isNew) = 0;
};

using EventHandlerPtr = std::shared_ptr<IEventHandler>;