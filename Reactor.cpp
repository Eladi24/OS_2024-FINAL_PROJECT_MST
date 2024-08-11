#include "Reactor.hpp"
#include <iostream>
#include <unistd.h>

Reactor::Reactor() {
    FD_ZERO(&_readFds);
    FD_ZERO(&_writeFds);
    _maxFd = 0;
    running = true; // Initialize running flag
}

Reactor::~Reactor() {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto &handler : _handlers) {
        FD_CLR(handler.first, &_readFds);
        FD_CLR(handler.first, &_writeFds);
        handler.second.reset();
    }
}

int Reactor::registerHandler(std::shared_ptr<EventHandler> handler, EventType type) {
    std::lock_guard<std::mutex> lock(_mutex);  // Lock the mutex
    int fd = handler->getHandle();
    if (type == EventType::READ) {
        FD_SET(fd, &_readFds);
    } else if (type == EventType::WRITE) {
        FD_SET(fd, &_writeFds);
    } else if (type == EventType::ACCEPT) {
        FD_SET(fd, &_readFds);
    }
    _handlers[fd] = std::move(handler);
    _maxFd = std::max(_maxFd, fd);  // Update _maxFd inside the mutex
    return 0;
}

int Reactor::handleEvents() {
    fd_set tempReadFds;
    fd_set tempWriteFds;
    int maxFd;

    {
        std::lock_guard<std::mutex> lock(_mutex);  // Lock the mutex
        tempReadFds = _readFds;
        tempWriteFds = _writeFds;
        maxFd = _maxFd;
    }

    struct timeval timeout;
    timeout.tv_sec = 5;  // 5-second timeout
    timeout.tv_usec = 0;

    int activity = select(maxFd + 1, &tempReadFds, &tempWriteFds, nullptr, &timeout);

    if (activity < 0 && errno != EINTR) {
        std::cerr << "select error" << std::endl;
        exit(1);
    }

    if (activity == 0) {
        return 0; // Timeout, no events
    }

    for (int fd = 0; fd <= maxFd; ++fd) {  // Use the local maxFd copy
        if (FD_ISSET(fd, &tempReadFds) || FD_ISSET(fd, &tempWriteFds)) {
            std::shared_ptr<EventHandler> handler;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_handlers.find(fd) != _handlers.end()) {
                    handler = _handlers[fd];
                }
            }
            if (handler) {
                handler->handleEvent();
            }
        }
    }

    return 0;
}

int Reactor::removeHandler(std::shared_ptr<EventHandler> handler, EventType type) {
    std::lock_guard<std::mutex> lock(_mutex);  // Lock the mutex
    int fd = handler->getHandle();
    if (type == EventType::READ) {
        FD_CLR(fd, &_readFds);
    } else if (type == EventType::WRITE) {
        FD_CLR(fd, &_writeFds);
    }
    _handlers.erase(fd);

    // Recalculate _maxFd if necessary
    if (fd == _maxFd) {
        _maxFd = 0; // Reset _maxFd
        for (const auto& h : _handlers) {
            _maxFd = std::max(_maxFd, h.first);
        }
    }

    return 0;
}

int Reactor::deactivateHandle(int fd, EventType type) {
    std::lock_guard<std::mutex> lock(_mutex);  // Lock the mutex
    if (type == EventType::READ) {
        FD_CLR(fd, &_readFds);
    } else if (type == EventType::WRITE) {
        FD_CLR(fd, &_writeFds);
    }
    return 0;
}

int Reactor::reactivateHandle(int fd, EventType type) {
    std::lock_guard<std::mutex> lock(_mutex);  // Lock the mutex
    if (type == EventType::READ) {
        FD_SET(fd, &_readFds);
    } else if (type == EventType::WRITE) {
        FD_SET(fd, &_writeFds);
    }
    _maxFd = std::max(_maxFd, fd);
    return 0;
}

void Reactor::notifyAll() {
    cv.notify_all(); // Notify all threads waiting on the condition variable
}

void Reactor::stop() {
    {
        std::lock_guard<std::mutex> lock(_mutex);  // Lock the mutex
        running = false; // Set the running flag to false
    }
    notifyAll(); // Wake up the event handling loop
}
