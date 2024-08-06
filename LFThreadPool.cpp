#include "LFThreadPool.hpp"

Reactor::~Reactor()
{
    for (auto &handler : _handlers)
    {
        FD_CLR(handler.first, &_readFds);
        handler.second.reset();
    }
}

int Reactor::registerHandler(shared_ptr<EventHandler> handler, EventType type)
{
    int fd = handler->getHandle();
    FD_SET(fd, &_readFds);
    _handlers[fd] = move(handler);
    _maxFd = max(_maxFd, fd);
    return 0;
}

int Reactor::removeHandler(shared_ptr<EventHandler> handler, EventType type)
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
            if (_handlers.find(fd) != _handlers.end())
            {
                _handlers[fd]->handleEvent();
            }
        }
    }

    cout << "Finished handling events" << endl;
    return 0;
}

int Reactor::deactivateHandle(int fd, EventType type)
{

    FD_CLR(fd, &_readFds);
    return 0;
}

int Reactor::reactivateHandle(int fd, EventType type)
{
    FD_SET(fd, &_readFds);
    _maxFd = max(_maxFd, fd);
    return 0;
}

LFThreadPool::LFThreadPool(size_t numThreads, shared_ptr<Reactor> reactor)
    : _stop(false), _reactor(reactor)
{
    // Start the follower threads
    for (size_t i = 1; i < numThreads; ++i)
    {
        thread t(&LFThreadPool::followerLoop, this);
        _followersState[t.get_id()] = false;
        _followers.push_back(move(t));
        
    }
    
}

LFThreadPool::~LFThreadPool()
{
    cout << "LFThreadPool destructor" << endl;
    // Stop all worker threads
    {
        unique_lock<mutex> lock(_mx);
        _stop = true;
    }

    _condition.notify_all();
    join();
}

void LFThreadPool::promoteNewLeader()
{
    
    // Find the next thread to be the leader
    for (auto &follower : _followers)

    {
        if (follower.get_id() != _leader && !_followersState[follower.get_id()])
        {
            cout << "Promoting new leader: " << follower.get_id() << endl;
            _leader = follower.get_id();
            // Notify the new leader
            _followersState[_leader] = true;
            _condition.notify_one();
            return;
        }
    }
}

void LFThreadPool::join()
{
    for (auto &follower : _followers)
    {
        if (follower.joinable())
        {
            follower.join();
        }
    }
}

void LFThreadPool::followerLoop()
{
   cout << "Follower thread " << this_thread::get_id() << " started" << endl;
    while (true)
    {
        unique_lock<mutex> lock(_mx);

        // Wait until this thread becomes the leader or stop is signaled
        _condition.wait(lock, [this]
                        { return _followersState[this_thread::get_id()] || _stop; });

        if (_stop)
            break;
        
        // Handle events
        cout << "Thread " << this_thread::get_id() << " handling events" << endl;
        promoteNewLeader();
        _reactor->handleEvents();
        _followersState[this_thread::get_id()] = false;
        
    }
}


