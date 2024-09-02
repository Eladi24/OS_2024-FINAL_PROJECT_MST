#ifndef _LFTHREADPOOL_HPP
#define _LFTHREADPOOL_HPP

#include <vector>
#include <string>
#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTStrategy.hpp"
#include "MSTFactory.hpp"
#include "Reactor.hpp"
#include "ThreadContext.hpp"

using namespace std;

class LFThreadPool
{
private:
    
    vector<shared_ptr<ThreadContext>> _followers;
    mutex _mx;    
    static mutex _outputMx; // Mutex to protect the output
    condition_variable _condition; // Condition variable to wake up the leader
    atomic<bool> _stop; // Flag to stop the pool and wake all the followers
    atomic<bool> _leaderChanged; // Flag to indicate that the leader has changed
    Reactor& _reactor; // Reactor to handle the events
    shared_ptr<ThreadContext> _leader;
    void followerLoop(int id);
    
public:
    LFThreadPool(size_t numThreads, Reactor& reactor);
    ~LFThreadPool();
    void promoteNewLeader();
    void join();
    void stopPool();
    void addFd(int fd, function<void()> event);
    static mutex& getOutputMx() { return _outputMx; }
};

#endif