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

class ThreadContext
{
    private:
        int _clientFd;
        function<void()> _event;
        pthread_t _thread;
        mutex _thMx;
        condition_variable _thCv;
        atomic<bool> _isAwake;
        unique_ptr<function<void()>> _functUniquePtr;
        
    public:
        ThreadContext() : _clientFd(-1), _isAwake(false) {}
        ThreadContext(pthread_t loop): _thread(loop), _isAwake(false) {}
        ~ThreadContext() { _functUniquePtr.reset(); }
        pthread_t createThread(function<void()> func);
        void addHandle(int fd, function<void()> event) { _clientFd = fd; _event = event; }
        void executeEvent() { _event(); }
        pthread_t getId() const { return _thread; }
        bool operator==(const ThreadContext& other) { return _thread == other._thread; }
        bool operator!=(const ThreadContext& other) { return _thread != other._thread; }
        void wakeUp(); 
        void sleep(); 
        bool isAwake() { return _isAwake.load(memory_order_acquire); }
        int getClientFd() const { return _clientFd; }
        void conditionWait(const atomic<bool>& stopFlag);
        void notify() { unique_lock<mutex> lock(_thMx); _thCv.notify_one(); }
        void join() { pthread_join(_thread, nullptr); }
        void cancel() { pthread_cancel(_thread); }
        
};


#endif