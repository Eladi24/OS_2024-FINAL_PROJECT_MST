#include "Reactor.hpp"

void Reactor::addHandle(int fd, function<void()> event)
{
    FD_SET(fd, &_master);
    _maxFd = max(_maxFd, fd);
    _handlers[fd] = event;
}

void Reactor::removeHandle(int fd)
{
    FD_CLR(fd, &_master);
    _handlers.erase(fd);
}

int Reactor::handleEvents()
{
    fd_set readFds = _master;
    int nready = select(_maxFd + 1, &readFds, nullptr, nullptr, nullptr);

    if (nready == -1 && errno != EINTR)
    {
        cerr << "Error in select" << endl;
        return -1;
    }

    for (int fd = 0; fd <= _maxFd; ++fd)
    {
        if (FD_ISSET(fd, &readFds))
        {
            if (_handlers.find(fd) != _handlers.end())
            {
                _handlers[fd]();
            }
        }
    }

    return 0;
}