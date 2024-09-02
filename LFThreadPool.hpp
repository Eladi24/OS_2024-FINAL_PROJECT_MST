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

/**
 * @class LFThreadPool
 * @brief Implementation of the Leader-Follower Thread Pool Pattern
 *
 * The Leader-Follower pattern is a concurrency design pattern where multiple worker threads
 * take turns being the leader and followers. The leader is responsible for accepting incoming events (e.g., client connections), 
 * and when it begins processing an event, it promotes a follower to be the next leader. 
 * This design reduces context switching and synchronization overhead, ensuring that only one thread handles events at a time.
 * 
 * Components of the Pattern:
 * - **Leader**: A thread that waits for and handles incoming events.
 * - **Follower**: A thread that waits until promoted to be the leader.
 * - **Reactor**: A mechanism that listens for events on file descriptors and notifies the leader thread to handle them.
 * - **Thread Pool**: A pool of threads that take turns being the leader and followers.
 *
 * The LFThreadPool class encapsulates these components and manages the lifecycle of the threads.
 */
class LFThreadPool
{
private:
    vector<shared_ptr<ThreadContext>> _followers; ///< A list of follower threads (ThreadContext objects)
    mutex _mx; ///< Mutex to synchronize access to shared resources
    static mutex _outputMx; ///< Mutex to protect shared output resources (e.g., logging or console output)
    condition_variable _condition; ///< Condition variable to notify followers to wake up when needed
    atomic<bool> _stop; ///< Atomic flag to signal stopping the thread pool
    atomic<bool> _leaderChanged; ///< Atomic flag to indicate if the leader has changed
    Reactor& _reactor; ///< Reactor object that manages and dispatches events to the leader
    shared_ptr<ThreadContext> _leader; ///< Pointer to the current leader thread context

    /**
     * @brief Follower loop function
     * 
     * This function represents the main loop for follower threads. Each follower waits until it is promoted
     * to be the leader or until the thread pool is stopped. When a follower is promoted to leader, it handles
     * the incoming event and then promotes a new leader.
     * 
     * @param id The ID of the thread running this loop.
     */
    void followerLoop(int id);
    
public:
    /**
     * @brief Constructor
     * 
     * Initializes the LFThreadPool with the specified number of threads and a reference to the Reactor.
     * 
     * @param numThreads The number of threads to create in the thread pool.
     * @param reactor The Reactor that manages events (e.g., incoming client connections).
     */
    LFThreadPool(size_t numThreads, Reactor& reactor);

    /**
     * @brief Destructor
     * 
     * Cleans up the thread pool by stopping all threads and joining them.
     */
    ~LFThreadPool();

    /**
     * @brief Promote a new leader thread
     * 
     * This method is called by the current leader to promote one of the follower threads to become the next leader.
     * It ensures that there is always a leader available to handle events.
     */
    void promoteNewLeader();

    /**
     * @brief Join all threads
     * 
     * Waits for all threads in the thread pool to complete their work. This is typically called during shutdown to
     * ensure a clean exit.
     */
    void join();

    /**
     * @brief Stop the thread pool
     * 
     * Signals all threads in the pool to stop by setting the `_stop` flag. This causes all follower threads to exit
     * their loops, and the pool shuts down gracefully.
     */
    void stopPool();

    /**
     * @brief Add a file descriptor and associated event handler
     * 
     * Registers a file descriptor with the Reactor and associates it with a specific event handler (e.g., a function
     * that handles a client connection or request).
     * 
     * @param fd The file descriptor to monitor (e.g., a socket for a client connection).
     * @param event The function to execute when the event occurs on the file descriptor.
     */
    void addFd(int fd, function<void()> event);

    /**
     * @brief Get the output mutex
     * 
     * Returns a reference to the static mutex used for protecting output operations (e.g., console or logging).
     * This ensures that multiple threads do not produce mixed or garbled output.
     * 
     * @return Reference to the output mutex.
     */
    static mutex& getOutputMx() { return _outputMx; }
};

#endif
