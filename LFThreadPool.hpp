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

class LFThreadPool
{
private:
    vector<thread> _followers;
    map<thread::id, atomic<bool>> _followersState;
    mutex _mx;
    condition_variable _condition;
    bool _stop; 
    shared_ptr<Reactor> _reactor;
    thread::id _leader;
    void followerLoop();
    
public:
    LFThreadPool(size_t numThreads, shared_ptr<Reactor> reactor);
    ~LFThreadPool();
    void promoteNewLeader();
    void join();
    
    
};

#endif