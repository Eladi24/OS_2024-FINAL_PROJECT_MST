#include "ServerConnection.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <mutex>

namespace ServerConnection {

int createServerSocket(int port) {
  int serverSock = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSock < 0) {
    std::cerr << "Socket creation error" << std::endl;
    return -1;
  }

  // Set socket options
  int opt = 1;
  if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    std::cerr << "Setsockopt error" << std::endl;
    close(serverSock);
    return -1;
  }

  // Configure server address
  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(port);
  memset(&(serverAddr.sin_zero), '\0', 8);

  // Bind socket
  if (::bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
    std::cerr << "Bind error" << std::endl;
    close(serverSock);
    return -1;
  }

  // Listen for connections
  if (listen(serverSock, BACKLOG) < 0) {
    std::cerr << "Listen error" << std::endl;
    close(serverSock);
    return -1;
  }

  return serverSock;
}

int acceptClient(int serverSock, std::atomic<int>& clientNumber, 
                 std::mutex& coutLock) {
  struct sockaddr_in client_addr;
  socklen_t sin_size = sizeof(client_addr);
  
  int client_sock = accept(serverSock, (struct sockaddr *)&client_addr, &sin_size);
  if (client_sock == -1) {
    perror("accept");
    return -1;
  }

  // Increment client counter
  clientNumber.store(clientNumber.load(std::memory_order_acquire) + 1,
                     std::memory_order_release);

  // Connection logged by caller (PipelineServer or LFServer)

  return client_sock;
}

void sendResponse(int clientSock, const std::string& response, 
                  std::mutex& coutLock) {
  if (send(clientSock, response.c_str(), response.size(), 0) < 0) {
    std::unique_lock<std::mutex> guard(coutLock);
    std::cerr << "send error" << std::endl;
  }
}

} 

