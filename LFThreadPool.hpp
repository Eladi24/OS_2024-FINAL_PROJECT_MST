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
    CONNECT
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
    fd_set _readFds;
    int _maxFd;
    unordered_map<int, EventHandler *> _handlers;


public:
    Reactor() : _maxFd(0)
    {
        FD_ZERO(&_readFds);
    }
    ~Reactor();
    int registerHandler(EventHandler *handler, EventType type);
    int removeHandler(EventHandler *handler, EventType type);
    int handleEvents();
    int deactivateHandle(EventHandler *handler, EventType type);
    int reactivateHandle(EventHandler *handler, EventType type);
};

class LFThreadPool
{
private:
    vector<thread> _workers;
    mutex _mx;
    condition_variable _condition;
    thread::id _leader;
    bool _stop; 
    Reactor *_reactor;
    void threadLoop();

public:
    LFThreadPool(size_t numThreads, Reactor *reactor);
    ~LFThreadPool();
    void promoteNewLeader(void);
    void join();
};

#endif