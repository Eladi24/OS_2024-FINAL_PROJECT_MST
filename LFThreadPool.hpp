#ifndef _LFTHREADPOOL_HPP
#define _LFTHREADPOOL_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <map>
#include <sys/select.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <memory>
#include <pthread.h>
#include <deque>
#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTStrategy.hpp"
#include "MSTFactory.hpp"

using namespace std;

enum class EventType
{
    READ,
    WRITE,
    ACCEPT,
    CONNECT,
    DISCONNECT
};


class Reactor
{
private:
    fd_set _master;
    fd_set _readFds;
    int _maxFd;
    map<int, function<void()>> _handlers;


public:
    Reactor() : _maxFd(0)
    {
        FD_ZERO(&_readFds);
        FD_ZERO(&_master);
        
    }

    ~Reactor();
    void addHandle(int fd, function<void()> event);
    void removeHandle(int fd);
    int handleEvents();
    int deactivateHandle(int fd, EventType type);
    int reactivateHandle(int fd, EventType type);
    
};

class ThreadContext
{
    private:
        int _clientFd;
        function<void()> _event;
        pthread_t _thread;
        mutex _thMx;
        condition_variable _thCv;
        atomic<bool> _isAwake;
        
    public:
        ThreadContext() : _clientFd(-1), _isAwake(false) {}
        ThreadContext(pthread_t loop): _thread(loop), _isAwake(false) {}
        ThreadContext(function<void()> th);
        // ThreadContext(ThreadContext&& other) noexcept :_clientFd(other._clientFd),_event(move(other._event)), _thread(move(other._thread)) {}
        // ThreadContext& operator=(ThreadContext&& other) noexcept;
        // ThreadContext(const ThreadContext& other) = delete;
        // ThreadContext& operator=(const ThreadContext& other) = delete;
        ~ThreadContext() {}
        void createThread(function<void()> func);
        void addHandle(int fd, function<void()> event) { _clientFd = fd; _event = event; }
        void executeEvent() { _event(); }
        pthread_t getId() const { return _thread; }
        bool operator==(const ThreadContext& other) { return _thread == other._thread; }
        bool operator!=(const ThreadContext& other) { return _thread != other._thread; }
        void wakeUp() { _isAwake.store(true, memory_order_release); }
        void sleep() { _isAwake.store(false, memory_order_release); }
        bool isAwake() { return _isAwake.load(memory_order_acquire); }
        int getClientFd() const { return _clientFd; }
        
                
        
};

class LFThreadPool
{
private:
    map<pthread_t, shared_ptr<ThreadContext>> _followers; // map of sleeping threads followers
    mutex _mx;    
    static mutex _outputMx; // Mutex to protect the output
    condition_variable _condition; // Condition variable to wake up the leader
    atomic<bool> _stop; // Flag to stop the pool and wake all the followers
    atomic<bool> _leaderChanged; // Flag to indicate that the leader has changed
    shared_ptr<Reactor> _reactor; // Reactor to handle the events
    shared_ptr<ThreadContext> _leader;
    void followerLoop();
    
public:
    LFThreadPool(size_t numThreads, shared_ptr<Reactor> reactor);
    ~LFThreadPool();
    void promoteNewLeader();
    void join();
    
    void stopPool();
    void addFd(int fd, function<void()> event);
    static mutex& getOutputMx() { return _outputMx; }
};

#endif