#ifndef CONCRETEEVENTHANDLER_HPP
#define CONCRETEEVENTHANDLER_HPP

#include "EventHandler.hpp"
#include <functional>

class ConcreteEventHandler : public EventHandler {
public:
    ConcreteEventHandler(int fd, std::function<void(int)> callback)
        : _fd(fd), _callback(std::move(callback)) {}

    int getHandle() const override {
        return _fd;
    }

    void handleEvent() override {
        _callback(_fd);  // Pass the file descriptor to the callback
    }

private:
    int _fd;
    std::function<void(int)> _callback;  // Callback that handles the event
};

#endif // CONCRETEEVENTHANDLER_HPP
