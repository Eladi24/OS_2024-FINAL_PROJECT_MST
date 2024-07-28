#ifndef _LFTHREADPOOL_HPP
#define _LFTHREADPOOL_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>

using namespace std;

class LFThreadPool
{
    private:
        vector<thread> _workers;
        queue<function<void()>> _tasks;
        mutex _queueMutex;
        condition_variable _condition;
        bool _stop;
        void workerThread();

    public:
        LFThreadPool(size_t numThreads);
        ~LFThreadPool();
        template<class F>
        void enqueue(F&& f);
        void stop();
        void start();
};




#endif