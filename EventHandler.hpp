#ifndef EVENTHANDLER_HPP
#define EVENTHANDLER_HPP

#include <functional>

class EventHandler {
public:
    virtual ~EventHandler() = default;
    virtual int getHandle() const = 0;
    virtual void handleEvent() = 0;
};

#endif // EVENTHANDLER_HPP
