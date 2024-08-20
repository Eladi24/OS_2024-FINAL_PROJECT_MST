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


class Handle
{
private:
    int _fd;

public:
    Handle(int fd) : _fd(fd) {}
    int getFd() { return _fd; }
};

class EventHandler
{
protected:
    int _handle;
    function<void()> _callback;
    
    

public:
    EventHandler(int fd, function<void()> callback) : _handle(fd), _callback(callback) {}
    virtual ~EventHandler() = default;
    virtual void handleEvent() = 0;
    virtual int getHandle() = 0;
};

class ConcreteEventHandler : public EventHandler
{
    public:
    ConcreteEventHandler(int fd, function<void()> callback) : EventHandler(fd, callback) {}
    void handleEvent() override { _callback(); }
    int getHandle() override { return _handle; }
    
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
        queue<function<void()>> _events;
        thread _thread;
        mutex _mx;
        atomic<bool> _isAwake;
    public:
        ThreadContext() : _clientFd(-1), _isAwake(false) {}
        ThreadContext(thread loop): _thread(move(loop)), _isAwake(false) {}
        ThreadContext(ThreadContext&& other) noexcept :_clientFd(other._clientFd),_events(move(other._events)), _thread(move(other._thread)) {}
        ThreadContext& operator=(ThreadContext&& other) noexcept;
        ThreadContext(const ThreadContext& other) = delete;
        ThreadContext& operator=(const ThreadContext& other) = delete;
        ~ThreadContext() { if (_thread.joinable()) _thread.join(); }
        void addEvent(function<void()> event) { _events.push(event); }
        void addHandle(int fd) { _clientFd = fd; }
        void executeEvents();
        void join() { _thread.join(); }
        thread::id getId() { return _thread.get_id(); }
        bool operator==(const ThreadContext& other) { return _thread.get_id() == other._thread.get_id(); }
        bool operator!=(const ThreadContext& other) { return _thread.get_id() != other._thread.get_id(); }
        void wakeUp() { _isAwake.store(true); }
        void sleep() { _isAwake.store(false); }
        bool isAwake() { return _isAwake.load(); }
        int getClientFd() const { return _clientFd; }
        bool joinable() { return _thread.joinable(); }
};

class LFThreadPool
{
private:
    
    map<thread::id, ThreadContext> _followers;
    mutex _mx;
    condition_variable _condition;
    bool _stop; 
    shared_ptr<Reactor> _reactor;
    ThreadContext* _leader;
    void followerLoop();
    
public:
    LFThreadPool(size_t numThreads, shared_ptr<Reactor> reactor);
    ~LFThreadPool();
    void promoteNewLeader();
    void join();
    void addFd(int fd, function<void()> event);
    void addEvent(function<void()> event, int fd);
};

#endif