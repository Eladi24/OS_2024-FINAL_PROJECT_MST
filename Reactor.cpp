#include "Reactor.hpp"
#include <cstring>
#include <cerrno>

void Reactor::addHandle(int fd, function<void()> event)
{
    unique_lock<mutex> lock(_reactorMutex);
    FD_SET(fd, &_master);
    _maxFd = max(_maxFd, fd);
    _handlers[fd] = event;
}

int Reactor::handleEvents()
{
    // Copy the master set and get handlers copy while holding lock
    fd_set readFds;
    int maxFdCopy;
    map<int, function<void()>> handlersCopy;
    
    {
        unique_lock<mutex> lock(_reactorMutex);
        readFds = _master;
        maxFdCopy = _maxFd;
        handlersCopy = _handlers;  // Copy handlers map
    }
    
    // Wait for events on the registered file descriptors (outside lock)
    int nready = select(maxFdCopy + 1, &readFds, nullptr, nullptr, nullptr);

    // If an error occurred in select
    if (nready == -1) {
        if (errno == EINTR) {
            // Interrupted by signal (likely shutdown) - this is normal
            return 0;
        } else if (errno == EBADF) {
            // Bad file descriptor - socket was closed (likely during shutdown)
            // This is expected during shutdown, don't treat as error
            return 0;
        } else {
            cerr << "Error in select: " << strerror(errno) << endl;
            return -1;
        }
    }

    // Check each file descriptor for events (using copied data)
    for (int fd = 0; fd <= maxFdCopy; ++fd)
    {
        // If the file descriptor is ready to read, call the event handler
        if (FD_ISSET(fd, &readFds))
        {
            auto it = handlersCopy.find(fd);
            if (it != handlersCopy.end() && it->second)
            {
                it->second();  // Call handler (using copied handler)
            }
        }
    }

    return 0;
}