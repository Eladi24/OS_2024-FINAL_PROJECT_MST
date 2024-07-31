#include "LFThreadPool.hpp"

void ConcreteEventHandler::handleEvent()
{
    _callback();
}

Reactor::~Reactor()
{
    for (auto &handler : _handlers)
    {
        delete handler.second;
    }
}

int Reactor::registerHandler(EventHandler *handler, EventType type)
{
    Handle handle = handler->getHandle();
    int fd = handle.getFd();
    if (type == EventType::READ)
    {
        FD_SET(fd, &_readFds);
    }
    if (fd > _maxFd)
    {
        _maxFd = fd;
    }
    _handlers[fd] = handler;
    return 0;
}

int Reactor::removeHandler(EventHandler *handler, EventType type)
{
    Handle handle = handler->getHandle();
    int fd = handle.getFd();
    if (type == EventType::READ)
    {
        FD_CLR(fd, &_readFds);
    }
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
    Handle handle = handler->getHandle();
    int fd = handle.getFd();
    if (type == EventType::READ)
    {
        if (FD_ISSET(fd, &_readFds))
        {
            FD_CLR(fd, &_readFds);
            FD_SET(fd, &_deactivatedReadFds);
        }
    }
    return 0;
}

int Reactor::reactivateHandle(EventHandler *handler, EventType type)
{
    Handle handle = handler->getHandle();
    int fd = handle.getFd();
    if (type == EventType::READ)
    {
        if (FD_ISSET(fd, &_deactivatedReadFds))
        {
            FD_CLR(fd, &_deactivatedReadFds);
            FD_SET(fd, &_readFds);
        }
    }
    return 0;
}

LFThreadPool::LFThreadPool(size_t numThreads, Reactor *reactor) : _stop(false), _reactor(reactor)
{
    for (size_t i = 0; i < numThreads; i++)
    {
        _workers.emplace_back([this]
                              { 
            for(;;) {
                unique_lock<mutex> lock(_queueMutex);
                _condition.wait(lock, [this] { return _stop || !_tasks.empty(); });
                if (_stop && _tasks.empty()) return;
                auto task = move(_tasks.front());
                _tasks.pop();
                lock.unlock();
                task();
            } });
    }
}

LFThreadPool::~LFThreadPool()
{
    unique_lock<mutex> lock(_queueMutex);
    _stop = true;
    lock.unlock();
    _condition.notify_all();
    for (auto &worker : _workers)
    {
        worker.join();
    }
}


void LFThreadPool::workerThread()
{
    while (true)
    {
        function<void()> task;
        {
            unique_lock<mutex> lock(_queueMutex);
            _condition.wait(lock, [this]
                            { return _stop || !_tasks.empty(); });
            if (_stop && _tasks.empty())
                return;
            task = move(_tasks.front());
            _tasks.pop();
        }
        task();
    }
}

int LFThreadPool::promoteNewLeader(void)
{
    unique_lock<mutex> lock(_queueMutex);
    if (_workers.empty())
    {
        return -1;
    }
    _leaderId = _workers.back().get_id();
    return 0;
}

void LFThreadPool::leaderLoop()
{
    while (!_stop)
    {
        _reactor->handleEvents();
        
        // Promote a new leader
        {
            unique_lock<mutex> lock(_queueMutex);
            _leaderId = thread::id();
            _condition.notify_all();
        }

        // Yield to allow other threads to acquire the leadership
        this_thread::yield();
    }
}

void LFThreadPool::followerLoop()
{
    while (!_stop)
    {
        {
            unique_lock<mutex> lock(_queueMutex);
            _condition.wait(lock, [this] { return _leaderId == thread::id() || _stop; });
            if (_stop)
                return;

            // Become the leader
            _leaderId = this_thread::get_id();
        }
        
        leaderLoop();
    }
}
