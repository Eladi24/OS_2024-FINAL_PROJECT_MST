/**
 * @file Reactor.hpp
 * @brief Header file for the Reactor class.
 */

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

/**
 * @class Reactor
 * @brief Implements the Reactor pattern for event-driven programming.
 *
 * The Reactor class provides a mechanism for handling multiple I/O events using a single thread.
 * It uses the Reactor pattern, which is a design pattern for handling service requests delivered
 * concurrently to a service handler by one or more inputs. The Reactor class maintains a set of
 * file descriptors and associated event handlers. It allows adding and removing file descriptors
 * along with their corresponding event handlers. The handleEvents() method is used to wait for
 * events on the registered file descriptors and invoke the corresponding event handlers when an
 * event occurs.
 */
class Reactor
{
private:
    fd_set _master; /**< The master file descriptor set. */
    fd_set _readFds; /**< The file descriptor set for reading. */
    int _maxFd; /**< The maximum file descriptor value. */
    map<int, function<void()>> _handlers; /**< The map of file descriptors and their event handlers. */

public:
    /**
     * @brief Default constructor for the Reactor class.
     */
    Reactor() : _maxFd(0)
    {
        FD_ZERO(&_readFds);
        FD_ZERO(&_master);
    }

    /**
     * @brief Adds a file descriptor and its corresponding event handler to the Reactor.
     * @param fd The file descriptor to add.
     * @param event The event handler function to associate with the file descriptor.
     */
    void addHandle(int fd, function<void()> event);

    /**
     * @brief Removes a file descriptor and its corresponding event handler from the Reactor.
     * @param fd The file descriptor to remove.
     */
    void removeHandle(int fd);

    /**
     * @brief Waits for events on the registered file descriptors and invokes the corresponding event handlers.
     * @return The number of events handled.
     */
    int handleEvents();
};

#endif