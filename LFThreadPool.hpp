#ifndef LFTHREADPOOL_HPP
#define LFTHREADPOOL_HPP

#include <thread>
#include <vector>
#include <map>
#include <memory>
#include <condition_variable>
#include "Reactor.hpp"

class LFThreadPool {
public:
    LFThreadPool(size_t numThreads, std::shared_ptr<Reactor> reactor);
    ~LFThreadPool();

    void run();  // Start the pool
    void join(); // Wait for threads to finish

private:
    void followerLoop();       // The loop each follower runs
    void promoteNewLeader();   // Promote a new leader

    std::vector<std::thread> _followers;             // The follower threads
    std::map<std::thread::id, bool> _followersState; // To keep track of the leader/followers

    std::shared_ptr<Reactor> _reactor;
    std::thread::id _leader;  // The current leader
    bool _stop;               // Flag to stop the pool
    std::mutex _mx;           // Mutex for synchronizing access
    std::condition_variable _condition;  // Condition variable for signaling followers
};

#endif // LFTHREADPOOL_HPP
