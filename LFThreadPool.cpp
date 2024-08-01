#include "LFThreadPool.hpp"


Reactor::~Reactor()
{
    for (auto &handler : _handlers)
    {
        delete handler.second;
    }
}

int Reactor::registerHandler(EventHandler *handler, EventType type)
{
    int fd = handler->getHandle();
    FD_SET(fd, &_readFds);
    _handlers[fd] = handler;
    _maxFd = max(_maxFd, fd);
    return 0;
}

int Reactor::removeHandler(EventHandler *handler, EventType type)
{
    int fd = handler->getHandle();
    FD_CLR(fd, &_readFds);
    _handlers.erase(fd);
    return 0;
}

int Reactor::handleEvents()
{
    fd_set tempFds = _readFds;
    int activity = select(_maxFd + 1, &tempFds, nullptr, nullptr, nullptr);
    
    if (activity < 0 && errno != EINTR)
    {
        cerr << "select error" << endl;
        exit(1);
    }

    for (int fd = 0; fd <= _maxFd; ++fd)
    {
        if (FD_ISSET(fd, &tempFds))
        {
            _handlers[fd]->handleEvent();
        }
    }
    
    return 0;
}

int Reactor::deactivateHandle(EventHandler *handler, EventType type)
{
    int fd = handler->getHandle();
    FD_CLR(fd, &_readFds);
    return 0;
}

int Reactor::reactivateHandle(EventHandler *handler, EventType type)
{
    int fd = handler->getHandle();
    FD_SET(fd, &_readFds);
    _maxFd = max(_maxFd, fd);
    return 0;
}

LFThreadPool::LFThreadPool(size_t numThreads, Reactor *reactor) : _stop(false), _reactor(reactor)
{
    for (size_t i = 0; i < numThreads; ++i)
    {
        _workers.emplace_back(&LFThreadPool::threadLoop, this);
    }
}

LFThreadPool::~LFThreadPool()
{
    // Stop all worker threads
    {
        unique_lock<mutex> lock(_mx);
        _stop = true;
    }

    _condition.notify_all();
    for (auto &worker : _workers)
    {
        worker.join();
    }
}


void LFThreadPool::promoteNewLeader(void)
{
    // Lock the mutex
    unique_lock<mutex> lock(_mx);

    _leader = this_thread::get_id();
    // Wake up another thread as the new leader
    _condition.notify_one();
    
}

void LFThreadPool::join()
{
    for (auto &worker : _workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

void LFThreadPool::threadLoop()
{
    while (!_stop)
    {
        // Only the leader can process events
        if (this_thread::get_id() == _leader)
        {
            _reactor->handleEvents();
            promoteNewLeader();
        }
        else
        {
            // Lock the mutex
            unique_lock<mutex> lock(_mx);
            // Wait for the leader to change
            _condition.wait(lock, [this] { return this_thread::get_id() == _leader || _stop; });
        }
    }
}