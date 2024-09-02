#ifndef _THREADCONTEXT_HPP
#define _THREADCONTEXT_HPP

#include <iostream>
#include <functional>
#include <pthread.h>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
using namespace std;

/**
 * @class ThreadContext
 * @brief Manages the lifecycle and synchronization of a single thread within a thread pool.
 *
 * The ThreadContext class is responsible for creating, managing, and synchronizing a thread.
 * It provides methods to create a thread with a specific function, wake up the thread, put the thread to sleep,
 * and wait for a condition to be met. This class is typically used within a thread pool to manage individual threads.
 * unlike std::thread, this class provides a way to cancel a thread even if it is in a system blocking call.
 * Those goals are achieved by using pthreads.
 */
class ThreadContext
{
    private:
        int _clientFd; // The client file descriptor associated with the thread
        function<void()> _event; // The event to execute when the thread is woken up
        pthread_t _thread; // The thread identifier
        mutex _thMx; // Mutex to protect the thread context
        condition_variable _thCv; // Condition variable to synchronize the thread
        atomic<bool> _isAwake; // Flag to indicate if the thread is awake
        unique_ptr<function<void()>> _functUniquePtr; // Unique pointer to the function to execute when creating the thread
        
    public:

        /**
         * @brief Default constructor for the ThreadContext class.
         */
        ThreadContext() : _clientFd(-1), _isAwake(false) {}

        /**
         * @brief Default destructor for the ThreadContext class.
         */
        ~ThreadContext() { _functUniquePtr.reset(); }

        /**
         * @brief Creates a new thread with the specified function.
         * @param func The function to execute in the new thread.
         * @return The thread identifier of the new thread.
         */
        pthread_t createThread(function<void()> func);

        /**
         * @brief Adds a file descriptor and its corresponding event handler to the thread context.
         * @param fd The file descriptor to add.
         * @param event The event handler function to associate with the file descriptor.
         */
        void addHandle(int fd, function<void()> event) { _clientFd = fd; _event = event; }

        /**
         * @brief Executes the event associated with the thread context.
         * This method is typically called by the thread pool to execute the event associated with the thread context.
         */
        void executeEvent() { _event(); }

        pthread_t getId() const { return _thread; } // Returns the thread identifier

        // Overloaded operators
        bool operator==(const ThreadContext& other) { return _thread == other._thread; }

        bool operator!=(const ThreadContext& other) { return _thread != other._thread; }

        /**
         * @brief Wakes up the thread.
         * Changes the state of the thread to awake and notifies the condition variable.
         */
        void wakeUp();

        /**
         * @brief Puts the thread to sleep.
         * Changes the state of the thread to asleep.
         */ 
        void sleep();

        bool isAwake() { return _isAwake.load(memory_order_acquire); } // Returns true if the thread is awake

        int getClientFd() const { return _clientFd; } // Returns the client file descriptor associated with the thread

        /**
         * @brief Waits for a condition to be met.
         * @param stopFlag The flag to check for the condition.
         * This method waits for the condition variable to be notified or the stop flag to be set.
         */
        void conditionWait(const atomic<bool>& stopFlag);

        /**
         * @brief Notifies the condition variable.
         * This method notifies the condition variable to wake up the thread.
         */
        void notify() { unique_lock<mutex> lock(_thMx); _thCv.notify_one(); }

        // Thread management methods
        void join() { pthread_join(_thread, nullptr); }
        void cancel() { pthread_cancel(_thread); }
        
};


#endif