#ifndef REACTOR_HPP
#define REACTOR_HPP

#include "EventHandler.hpp"
#include <memory>
#include <map>
#include <sys/select.h>
#include <mutex>
#include <condition_variable>
#include <atomic>

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

    void notifyAll();
       void stop(); // Add this method

private:
    std::map<int, std::shared_ptr<EventHandler>> _handlers;
    fd_set _readFds;
    fd_set _writeFds;
    int _maxFd;
    std::mutex _mutex;
        std::condition_variable cv;
        std::atomic_bool running;
};

#endif // REACTOR_HPP
