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
        ActiveObject(): _done(false), _worker(&ActiveObject::run, this) {}
        ~ActiveObject();
        
        template<class F>
        void enqueue(F task);
        

};





#endif