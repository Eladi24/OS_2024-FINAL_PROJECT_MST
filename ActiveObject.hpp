#ifndef _ACTIVEOBJECT_HPP
#define _ACTIVEOBJECT_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
using namespace std;
class ActiveObject
{
private:
    queue<function<void()>> _tasks;
    mutex _mx;
    condition_variable _cv;
    thread _worker;
    bool _done;
    void run();

public:
    ActiveObject() : _worker(&ActiveObject::run, this), _done(false) {}
    ~ActiveObject();

    template <class F>
    void enqueue(F task)
    {
        unique_lock<mutex> lock(_mx);
        _tasks.emplace(task);
        lock.unlock();
        _cv.notify_one();
    }
    template <class F>
    function<F()> dequeue();
};

#endif