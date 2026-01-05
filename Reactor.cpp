#include "Reactor.hpp"
#include <cstring>
#include <cerrno>

void Reactor::addHandle(int fd, function<void()> event)
{
    FD_SET(fd, &_master);
    _maxFd = max(_maxFd, fd);
    _handlers[fd] = event;
}

int Reactor::handleEvents()
{
    // Copy the master set to prevent modification during select
    fd_set readFds = _master;
    // Wait for events on the registered file descriptors
    int nready = select(_maxFd + 1, &readFds, nullptr, nullptr, nullptr);

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

    // Check each file descriptor for events
    for (int fd = 0; fd <= _maxFd; ++fd)
    {
        // If the file descriptor is ready to read, call the event handler
        if (FD_ISSET(fd, &readFds))
        {
            if (_handlers.find(fd) != _handlers.end() && _handlers[fd])
            {
                _handlers[fd]();
            }
        }
    }

    return 0;
}