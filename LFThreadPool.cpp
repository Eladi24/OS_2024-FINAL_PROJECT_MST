#include "LFThreadPool.hpp"
#include <iomanip>  // For setw function
#include <iostream> // For console output

mutex LFThreadPool::_outputMx;

LFThreadPool::LFThreadPool(size_t numThreads, Reactor& reactor)
    : _followers(numThreads), _stop(false), _leaderChanged(false), _reactor(reactor)
{
    for (size_t i = 0; i < numThreads; ++i)
    {
        _followers[i] = make_shared<ThreadContext>();
        _followers[i]->createThread(bind(&LFThreadPool::followerLoop, this, i));
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "\n[INFO] 🟢 Following thread created: " << setw(10) << _followers[i]->getId() << "\n";
        }
    }
    promoteNewLeader();
}

LFThreadPool::~LFThreadPool()
{
    {
        unique_lock<mutex> guard(_outputMx);
        cout << "\n[INFO] 🔴 LFThreadPool Destructor\n";
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
            cout << "\n[INFO] 👑 Promoting new leader: " << setw(10) << _followers.begin()->get()->getId() << "\n";
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
                cout << "\n[INFO] 👑 Promoting new leader: " << setw(10) << follower->getId() << "\n";
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
            cout << "\n[INFO] 🔔 Thread " << setw(10) << _followers[id]->getId() << " woke up\n";
        }

        if (_stop.load(memory_order_acquire))
            break;

        _reactor.handleEvents();

        shared_ptr<ThreadContext> currThread = _leader;
        promoteNewLeader();

        currThread->executeEvent();
        {
            unique_lock<mutex> guard(_outputMx);
            cout << "\n[INFO] 😴 Thread " << setw(10) << currThread->getId() << " going to sleep\n";
        }
        currThread->sleep();
    }
}

void LFThreadPool::addFd(int fd, function<void()> event)
{
    _leader->addHandle(fd, event);
}

void LFThreadPool::stopPool()
{
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
        // pthread_t id = follower->getId();
        // {
        //     unique_lock<mutex> guard(_outputMx);
        //     cout << "\n[INFO] ⚙️ Joining thread: " << setw(10) << id << "\n";
        // }
        follower->cancel();
        follower->join();
        follower.reset();
    }
}
