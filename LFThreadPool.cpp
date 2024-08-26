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

// ThreadContext& ThreadContext::operator=(ThreadContext&& other) noexcept
// {
//     if (this != &other)
//     {
//         _clientFd = other._clientFd;
//         _event = move(other._event);
//         _thread = move(other._thread);
//     }

//     return *this;
// }

void ThreadContext::createThread(function<void()> func)
{
    // Allocate a new function on the heap so it can be passed to the thread
    function<void()>* funcPtr = new function<void()>(func);

    // Create the thread
    int result = pthread_create(&_thread, nullptr, [](void* arg) -> void* {
        // Convert the argument back to function pointer
        function<void()>* funcPtr = static_cast<function<void()>*>(arg);
        
        // Execute the function
        (*funcPtr)();
        
        // Clean up the allocated memory
        delete funcPtr;
        
        return nullptr;
    }, funcPtr);

    // Check for thread creation errors
    if (result != 0) {
        throw runtime_error("Failed to create thread");
    }

}

ThreadContext::ThreadContext(function<void()> th) : _event(th), _isAwake(false)
{
    createThread(th);
}


mutex LFThreadPool::_outputMx;
LFThreadPool::LFThreadPool(size_t numThreads, shared_ptr<Reactor> reactor)
    : _stop(false), _leaderChanged(false), _reactor(reactor)
{
    // Start the follower threads
    for (size_t i = 0; i < numThreads; ++i)
    {
        pthread_t pid;
        auto follwerWrapperFunc = [](void *arg) -> void *
        {
            LFThreadPool *pool = static_cast<LFThreadPool *>(arg);
            pool->followerLoop();
            return nullptr;
        };
        pthread_create(&pid, nullptr, follwerWrapperFunc, this);
        {
            unique_lock<mutex> lock(_outputMx);
            cout << "Following thread created: " << pid << endl;
        }

        _followers[pid] = make_shared<ThreadContext>(pid);
    }
    
    promoteNewLeader();
}

LFThreadPool::~LFThreadPool()
{
    {
        unique_lock<mutex> guard(_outputMx);
        cout << "LFThreadPool destructor" << endl;
    }
    _reactor.reset();
    stopPool();
    _followers.clear();
    _leader.reset();
}

void LFThreadPool::promoteNewLeader()
{
    unique_lock<mutex> lock(_mx);
    if (_leader == nullptr)
    {
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "Promoting new leader: " << _followers.begin()->first << endl;
        }
        _leader = _followers.begin()->second;
        _leader->wakeUp();
        _leaderChanged.store(true, memory_order_release);
        _condition.notify_one();
        return;
    }
    
    for (auto &[id, follower] : _followers)
    {
        if (*follower != *_leader && !follower->isAwake())
        {
            {
                unique_lock<mutex> guard(_outputMx);
                cout << "Promoting new leader: " << id << endl;
            }
            _leader = follower;
            _leader->wakeUp();
            _leaderChanged.store(true, memory_order_release);
            _condition.notify_one();
            return;
        }
    }
}

void LFThreadPool::join()
{   
    for (auto &[id, follower] : _followers)
    {
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "Size of followers: " << _followers.size() << endl;
            cout << "Joining thread: " << id << endl;
        }
        pthread_cancel(id);
        pthread_join(id, nullptr);
        follower.reset();
    }
}

void LFThreadPool::followerLoop()
{
    while (true)
    {
        {
            unique_lock<mutex> lock(_mx);
            // Wait until this thread becomes the leader or stop is signaled
            _condition.wait(lock, [this]
                            { return (_leader != nullptr  && _leaderChanged.load(memory_order_acquire)) || _stop.load(memory_order_acquire); });
            
            _leaderChanged.store(false, memory_order_release);
            {
                unique_lock<mutex> guard(_outputMx);
                cout << "This thread: " << pthread_self() << " vs leader: " << _leader->getId() << endl;
            }
        }

        // If stop then the program is shutting down
        if (_stop.load(memory_order_acquire))
            break;

        // Handle events in the reactor
        _reactor->handleEvents();
        
        // Promote a new leader without holding _mx lock
        ThreadContext* currThread = _leader.get();
        promoteNewLeader();

        // Execute events in the thread context
        currThread->executeEvent();
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "Thread: " << currThread->getId() << " executed event" << endl;
        }
        // Put the thread to sleep
        currThread->sleep();
    }
}


void LFThreadPool::addFd(int fd, function<void()> event)
{
    // Add the file descriptor to the leader
    _leader->addHandle(fd, event);
}

void LFThreadPool::stopPool()
{
    // Stop all worker threads
    _stop.store(true, memory_order_release);
    {
        lock_guard<mutex> lock(_mx);
        _condition.notify_all();
    }
    join();
}
