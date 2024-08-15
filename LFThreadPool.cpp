#include "LFThreadPool.hpp"

Reactor::~Reactor()
{
    
}

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
    while (true)
    {
        fd_set readFds = _master;
        int nready = select(_maxFd + 1, &readFds, nullptr, nullptr, nullptr);
        if (nready == -1)
        {
            if (errno == EINTR)
                continue;
            else
            {
                perror("select");
                return -1;
            }
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
    }

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
    for (size_t i = 0; i < numThreads; ++i)
    {
        thread t(&LFThreadPool::followerLoop, this);
        _followersState[t.get_id()] = false;
        _followers.push_back(move(t));
        
    }
    promoteNewLeader();
    
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
   
    while (true)
    {
        unique_lock<mutex> lock(_mx);

        // Wait until this thread becomes the leader or stop is signaled
        _condition.wait(lock, [this]
                        { return _followersState[this_thread::get_id()] || _stop; });

        cout << "Follower thread " << this_thread::get_id() << "Has woken up" << endl;
        // If stop then the program is shutting down
        if (_stop)
            break;
        
        // Handle events in the reactor
        _reactor->handleEvents();
        // Promote a new leader
        promoteNewLeader();
        
    }
}


