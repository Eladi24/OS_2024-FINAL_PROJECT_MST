#ifndef _ACTIVEOBJECT_HPP
#define _ACTIVEOBJECT_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
using namespace std;

/**
 * @class ActiveObject
 * 
 * This class is a simple implementation of the Active Object pattern.
 * It allows tasks to be enqueued and executed by a separate thread.
 * 
 * The class is templated to allow for any type of function to be enqueued.
 * 
 * The class is not thread-safe for the enqueuing of tasks, but is thread-safe
 * for the execution of tasks.
 * 
 * The components of this pattern are:
 * Proxy: The interface that clients use to interact with the Active Object (a.k.a. Pipeline server)
 * MethodRequest: The task that is enqueued and executed by the Active Object (a.k.a. function object Task)
 * Scheduler: The component that has a list of tasks to be executed by the Active Object (a.k.a. Task queue)
 * Servant: The component that actually executes the tasks (a.k.a. Worker thread)
 * Future: The result of the task execution (a.k.a. response to the client)
 */
class ActiveObject
{
private:
    queue<function<void()>> _tasks; // Task queue
    mutex _mx; // Mutex to protect the task queue
    condition_variable _cv; // Condition variable to wake up the worker thread
    atomic<bool> _done; // Flag to signal the worker thread to stop
    thread _worker; // Worker thread
    static mutex _outputMx; // Mutex to protect the output stream

   /*
    * @brief 
    * The worker thread function
    * This function is the main loop of the worker thread.
    * It waits for tasks to be enqueued and executes them.
    * @return void
   */
    void run();

public:
    ActiveObject() :_tasks(), _done(false), _worker(&ActiveObject::run, this) {}

    ~ActiveObject();
    
    void start();
    /*
    * @brief
    * This function enqueues a task to be executed by the worker thread.
    * It is templated to allow for any type of function to be enqueued.
    * @param task The task to be enqueued
    * @return void
    */
    template <class F>
    void enqueue(F task)
    {   
        // Lock the mutex to protect the task queue
        unique_lock<mutex> lock(_mx);
        // Move the task into the task queue
        _tasks.emplace(task);
        // Wake up the worker thread
        _cv.notify_one();
    }

    static mutex& getOutputMutex() { return _outputMx; }
    
};

#endif