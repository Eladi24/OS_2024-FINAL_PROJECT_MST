#include "Reactor.hpp"
#include <iostream>
#include <unistd.h>

Reactor::Reactor() {
    FD_ZERO(&_readFds);
    FD_ZERO(&_writeFds);
    _maxFd = 0;
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
    int fd = handler->getHandle();
    if (type == EventType::READ) {
        FD_SET(fd, &_readFds);
        //std::cout << "Registered fd " << fd << " for READ events." << std::endl;
    } else if (type == EventType::WRITE) {
        FD_SET(fd, &_writeFds);
        //std::cout << "Registered fd " << fd << " for WRITE events." << std::endl;
    } else if (type == EventType::ACCEPT) {
        FD_SET(fd, &_readFds);
        //std::cout << "Registered fd " << fd << " for ACCEPT events." << std::endl;
    }
    _handlers[fd] = std::move(handler);
    _maxFd = std::max(_maxFd, fd);
    //std::cout << "New maxFd is " << _maxFd << std::endl;
    return 0;
}


int Reactor::removeHandler(std::shared_ptr<EventHandler> handler, EventType type) {
    std::lock_guard<std::mutex> lock(_mutex);
    int fd = handler->getHandle();
    if (type == EventType::READ) {
        FD_CLR(fd, &_readFds);
    } else if (type == EventType::WRITE) {
        FD_CLR(fd, &_writeFds);
    }
    _handlers.erase(fd);
    return 0;
}

int Reactor::handleEvents() {
    fd_set tempReadFds;
    fd_set tempWriteFds;

    FD_ZERO(&tempReadFds);
    FD_ZERO(&tempWriteFds);

    {
        std::lock_guard<std::mutex> lock(_mutex);
        tempReadFds = _readFds;
        tempWriteFds = _writeFds;
    }

    struct timeval timeout;
    timeout.tv_sec = 5;  // 5-second timeout
    timeout.tv_usec = 0;

    // Debugging statement
    //std::cout << "Before select, maxFd: " << _maxFd << std::endl;

    int activity = select(_maxFd + 1, &tempReadFds, &tempWriteFds, nullptr, &timeout);

    if (activity < 0 && errno != EINTR) {
        std::cerr << "select error" << std::endl;
        exit(1);
    }

    if (activity == 0) {
        //std::cout << "select() timeout, no events" << std::endl;
        return 0; // Timeout, no events
    }

    for (int fd = 0; fd <= _maxFd; ++fd) {
        if (FD_ISSET(fd, &tempReadFds)) {
            //std::cout << "Event detected on fd: " << fd << std::endl;
            std::lock_guard<std::mutex> lock(_mutex);
            if (_handlers.find(fd) != _handlers.end()) {
                _handlers[fd]->handleEvent();
            }
        }
        if (FD_ISSET(fd, &tempWriteFds)) {
            //std::cout << "Write event detected on fd: " << fd << std::endl;
            std::lock_guard<std::mutex> lock(_mutex);
            if (_handlers.find(fd) != _handlers.end()) {
                _handlers[fd]->handleEvent();
                deactivateHandle(fd, EventType::WRITE);
            }
        }
    }

    return 0;
}



int Reactor::deactivateHandle(int fd, EventType type) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (type == EventType::READ) {
        FD_CLR(fd, &_readFds);
    } else if (type == EventType::WRITE) {
        FD_CLR(fd, &_writeFds);
    }
    return 0;
}

int Reactor::reactivateHandle(int fd, EventType type) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (type == EventType::READ) {
        FD_SET(fd, &_readFds);
    } else if (type == EventType::WRITE) {
        FD_SET(fd, &_writeFds);
    }
    _maxFd = std::max(_maxFd, fd);
    return 0;
}
