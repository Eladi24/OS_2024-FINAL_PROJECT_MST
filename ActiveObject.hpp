#ifndef ACTIVE_OBJECT_HPP
#define ACTIVE_OBJECT_HPP

#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <iostream>

class ActiveObject {
public:
    ActiveObject();  // Constructor
    ~ActiveObject();  // Destructor
    void enqueue(std::function<void()> task);  // Method to add tasks to the queue

private:
    void run();  // The function that the thread runs

    std::thread _worker;  // Thread to run tasks
    std::queue<std::function<void()>> _tasks;  // Task queue
    std::mutex _mx;  // Mutex for synchronizing access to the task queue
    std::condition_variable _cv;  // Condition variable to signal task availability
    bool _done;  // Flag to indicate whether the ActiveObject should stop
};

#endif // ACTIVE_OBJECT_HPP
