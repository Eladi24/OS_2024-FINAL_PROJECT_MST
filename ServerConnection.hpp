#ifndef _SERVER_CONNECTION_HPP
#define _SERVER_CONNECTION_HPP

#include <atomic>
#include <mutex>
#include <string>


class ActiveObject;

/**
 * @brief Server connection management utilities
 * 
 * Handles socket creation, binding, listening, and accepting connections
 */
namespace ServerConnection {
  // Constants
  const int DEFAULT_PORT = 4050;
  const int BACKLOG = 10;

  /**
   * @brief Creates and configures a server socket
   * 
   * @param port Port number to bind to (default: 4050)
   * @return int Server socket file descriptor, or -1 on error
   */
  int createServerSocket(int port = DEFAULT_PORT);

  /**
   * @brief Accepts a new client connection
   * 
   * @param serverSock Server socket file descriptor
   * @param clientNumber Atomic counter for tracking connected clients
   * @param coutLock Mutex for thread-safe console output
   * @return int Client socket file descriptor, or -1 on error
   */
  int acceptClient(int serverSock, std::atomic<int>& clientNumber, 
                   std::mutex& coutLock);

  /**
   * @brief Sends a response to a client
   * 
   * @param clientSock Client socket file descriptor
   * @param response Response string to send
   * @param coutLock Mutex for thread-safe console output
   */
  void sendResponse(int clientSock, const std::string& response, 
                    std::mutex& coutLock);
}

#endif // _SERVER_CONNECTION_HPP

