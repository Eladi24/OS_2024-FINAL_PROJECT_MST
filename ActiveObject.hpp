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
        thread _thread;
        bool _done;
        void run();
    public:
        ActiveObject(): _done(false), _thread(&ActiveObject::run, this) {}
        ~ActiveObject();
        void start();
        void stop();
        template<class F>
        void enqueue(F f);

};





#endif