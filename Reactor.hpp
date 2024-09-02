#ifndef _REACTOR_HPP
#define _REACTOR_HPP

#include <iostream>
#include <functional>
#include <map>
#include <sys/select.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

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
    void addHandle(int fd, function<void()> event);
    void removeHandle(int fd);
    int handleEvents();
};

#endif