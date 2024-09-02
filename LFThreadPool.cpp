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

pthread_t  ThreadContext::createThread(function<void()> func)
{
    // Allocate a new function on the heap so it can be passed to the thread
    _functUniquePtr = make_unique<function<void()>>(func);
    // Create the thread
    int result = pthread_create(&_thread, nullptr, [](void* arg) -> void* {
        // Convert the argument back to function pointer
        function<void()>* funcPtr = static_cast<function<void()>*>(arg);
        
        // Execute the function
        (*funcPtr)();
          
        return nullptr;
    }, _functUniquePtr.get());

    // Check for thread creation errors
    if (result != 0) {
        throw runtime_error("Failed to create thread");
    }

    return _thread;
}

void ThreadContext::wakeUp()
{
    unique_lock<mutex> lock(_thMx);
    _isAwake.store(true, memory_order_release);
    _thCv.notify_one();
}

void ThreadContext::sleep()
{
    unique_lock<mutex> lock(_thMx);
    _isAwake.store(false, memory_order_release);
}

void ThreadContext::conditionWait(const atomic<bool>& stopFlag)
{
    unique_lock<mutex> lock(_thMx);
    _thCv.wait(lock, [&stopFlag, this] { return _isAwake.load(memory_order_acquire) || stopFlag.load(memory_order_acquire); });
}

mutex LFThreadPool::_outputMx;
LFThreadPool::LFThreadPool(size_t numThreads, Reactor& reactor)
    : _followers(numThreads), _stop(false), _leaderChanged(false), _reactor(reactor)
{
    // Start the follower threads
    for (size_t i = 0; i < numThreads; ++i)
    {
        
        _followers[i] = make_shared<ThreadContext>();
        _followers[i]->createThread(bind(&LFThreadPool::followerLoop, this, i));
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "Following thread created: " << _followers[i]->getId() << endl;
        }
    }
    
    promoteNewLeader();
}

LFThreadPool::~LFThreadPool()
{
    {
        unique_lock<mutex> guard(_outputMx);
        cout << "LFThreadPool destructor" << endl;
    }
    
    stopPool();
    _followers.clear();
    _leader.reset();
}

void LFThreadPool::promoteNewLeader()
{
    if (_leader == nullptr)
    {
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "Promoting new leader: " << _followers.begin()->get()->getId() << endl;
        }
        _leader = *_followers.begin();
        _leader->wakeUp();
        return;
    }
    
    for (auto &follower : _followers)
    {
        if (*follower != *_leader && !follower->isAwake())
        {
            {
                unique_lock<mutex> guard(_outputMx);
                cout << "Promoting new leader: " << follower->getId() << endl;
            }
            _leader = follower;
            _leader->wakeUp();
            return;
        }
    }
}


void LFThreadPool::followerLoop(int id)
{
    while (true)
    {
        
        _followers[id]->conditionWait(_stop);
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "This thread: " << pthread_self() << " vs leader: " << _leader->getId() << endl;
        }
        
        // If stop then the program is shutting down
        if (_stop.load(memory_order_acquire))
            break;

        // Handle events in the reactor
        _reactor.handleEvents();
        
        // Promote a new leader without holding _mx lock
        shared_ptr<ThreadContext> currThread = _leader;
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
    for (auto & follower : _followers)
    {
        follower->notify();
    }
    join();
}

void LFThreadPool::join()
{   
    for (auto & follower : _followers)
    {
        pthread_t id = follower->getId();
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
