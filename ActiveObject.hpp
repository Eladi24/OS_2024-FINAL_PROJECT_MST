#ifndef ACTIVEOBJECT_HPP
#define ACTIVEOBJECT_HPP

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <iostream>

class ActiveObject {
public:
    ActiveObject();
    ~ActiveObject();
    void enqueue(std::function<void()> task);
    
private:
    void run();

    std::thread _worker;
    std::mutex _mx;
    std::condition_variable _cv;
    std::queue<std::function<void()>> _tasks;
    bool _done = false;
};

#endif // ACTIVEOBJECT_HPP
