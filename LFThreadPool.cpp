#include "LFThreadPool.hpp"

mutex LFThreadPool::_outputMx;
LFThreadPool::LFThreadPool(size_t numThreads, Reactor& reactor)
    : _followers(numThreads), _stop(false), _leaderChanged(false), _reactor(reactor)
{
    // Start the follower threads
    for (size_t i = 0; i < numThreads; ++i)
    {
        // Create a new thread context object
        _followers[i] = make_shared<ThreadContext>();
        // Create a new thread and bind the follower loop function
        _followers[i]->createThread(bind(&LFThreadPool::followerLoop, this, i));
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "Following thread created: " << _followers[i]->getId() << endl;
        }
    }
    // Promote the initial leader
    promoteNewLeader();
}

LFThreadPool::~LFThreadPool()
{
    {
        unique_lock<mutex> guard(_outputMx);
        cout << "LFThreadPool destructor" << endl;
    }
    
    stopPool();
    // Clean the allocated resources
    _followers.clear();
    _leader.reset();
}

void LFThreadPool::promoteNewLeader()
{
    // If there is no leader, promote the first follower in the list
    if (_leader == nullptr)
    {
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "Promoting new leader: " << _followers.begin()->get()->getId() << endl;
        }
        _leader = *_followers.begin();
        // Wake up the new leader to handle events
        _leader->wakeUp();
        return;
    }
    
    for (auto &follower : _followers)
    {
        // If the follower is not the current leader and is not awake, promote it
        if (*follower != *_leader && !follower->isAwake())
        {
            {
                unique_lock<mutex> guard(_outputMx);
                cout << "Promoting new leader: " << follower->getId() << endl;
            }
            _leader = follower;
            // Wake up the new leader to handle events
            _leader->wakeUp();
            return;
        }
    }
}


void LFThreadPool::followerLoop(int id)
{
    while (true)
    {
        // Wait until the follower is promoted to be the leader or the thread pool is stopped
        _followers[id]->conditionWait(_stop);
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "Thread: " << _followers[id]->getId() << " woke up" << endl;
        }
        
        // If stop then the program is shutting down
        if (_stop.load(memory_order_acquire))
            break;

        // Handle events in the reactor
        _reactor.handleEvents();
        
        // Promote a new leader and execute the event
        shared_ptr<ThreadContext> currThread = _leader;
        promoteNewLeader();

        // Execute events in the thread context
        currThread->executeEvent();
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "Thread: " << currThread->getId() << " is returning to sleep" << endl;
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
            cout << "Joining thread: " << id << endl;
        }
        // Cancel the thread and join it
        follower->cancel();
        follower->join();
        follower.reset();
    }
}
