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

ThreadContext& ThreadContext::operator=(ThreadContext&& other) noexcept
{
    if (this != &other)
    {
        _clientFd = other._clientFd;
        _events = move(other._events);
        _thread = move(other._thread);
    }

    return *this;
}

void ThreadContext::executeEvents()
{
    while (!_events.empty())
    {
        function<void()> event;
        {
            unique_lock<mutex> lock(_mx);
            event = _events.front();
            _events.pop();
        }
        event();     
    }
}

LFThreadPool::LFThreadPool(size_t numThreads, shared_ptr<Reactor> reactor)
    : _stop(false), _reactor(reactor)
{
    // Start the follower threads
    for (size_t i = 0; i < numThreads; ++i)
    {
        ThreadContext tc(thread(&LFThreadPool::followerLoop, this));
        cout << "Starting follower thread: " << tc.getId() << endl; 
        _followers[tc.getId()] = move(tc);
        
    }
    cout << "Number of threads: " << _followers.size() << endl;
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
    if (_leader == nullptr)
    {
        cout << "Promoting new leader: " << _followers.begin()->first << endl;
        _leader = &_followers.begin()->second;
        _leader->wakeUp();
        _condition.notify_one();
        return;
    }
    // Find the next thread to be the leader
    for (auto &[id, follower] : _followers)
    {
        cout << "This is test " << id << endl;
        if (&follower != _leader && !follower.isAwake())
        {
            cout << "Promoting new leader: " << follower.getId() << endl;
            _leader = &follower;
            // Notify the new leader
            _leader->wakeUp();
            _condition.notify_one();
            return;
        }
    }
}

void LFThreadPool::join()
{
    for (auto &[id, follower] : _followers)
    {
        if (follower.joinable())
        {
            cout << "Joining thread: " << id << endl;
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
        _condition.wait(lock, [this] { return (_leader != nullptr && _leader->isAwake()) || _stop; });

        cout << "Follower thread " << this_thread::get_id() << " has woken up" << endl;
        // If stop then the program is shutting down
        if (_stop)
            break;

        // Handle events in the reactor
        _reactor->handleEvents();
        // Promote a new leader
        ThreadContext* currThread = _leader;
        promoteNewLeader();
        cout << "New leader: " << _leader->getId() << endl;
        // Release the lock
        lock.unlock();
        // Execute events in the thread context
        currThread->executeEvents();
        // Put the thread to sleep
        currThread->sleep();
    }
}

void LFThreadPool::addFd(int fd, function<void()> event)
{
    // Add the file descriptor to the leader
    _leader->addHandle(fd);
    // Add the event to the leader
    _leader->addEvent(event);
}

void LFThreadPool::addEvent(function<void()> event, int fd)
{
    for (auto &[id, follower] : _followers)
    {
        if (follower.getClientFd() == fd)
        {
            cout << "Found the follower with fd: " << fd << endl;
            follower.addEvent(event);
            return;
        } 
    }
    cout << "Could not find the follower with fd: " << fd << endl;
}
