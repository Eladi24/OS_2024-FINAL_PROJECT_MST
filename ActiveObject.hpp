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
    ActiveObject();
    ~ActiveObject();
    void enqueue(std::function<void()> task);

private:
    void run();
    std::thread _worker;
    std::queue<std::function<void()>> _tasks;
    std::mutex _mx;
    std::condition_variable _cv;
    bool _done;
};

#endif // ACTIVE_OBJECT_HPP
