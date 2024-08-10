#ifndef REACTOR_HPP
#define REACTOR_HPP

#include "EventHandler.hpp"
#include <memory>
#include <map>
#include <sys/select.h>
#include <mutex>

enum class EventType {
    READ,
    WRITE,
    ACCEPT,
    DISCONNECT
};

class Reactor {
public:
    Reactor();
    ~Reactor();

    int registerHandler(std::shared_ptr<EventHandler> handler, EventType type);
    int removeHandler(std::shared_ptr<EventHandler> handler, EventType type);
    int handleEvents();
    int deactivateHandle(int fd, EventType type);
    int reactivateHandle(int fd, EventType type);

private:
    std::map<int, std::shared_ptr<EventHandler>> _handlers;
    fd_set _readFds;
    fd_set _writeFds;
    int _maxFd;
    std::mutex _mutex;
};

#endif // REACTOR_HPP
