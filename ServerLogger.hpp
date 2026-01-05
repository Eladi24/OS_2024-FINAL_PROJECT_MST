#ifndef SERVERLOGGER_HPP
#define SERVERLOGGER_HPP

#include <iostream>
#include <mutex>
#include <string>

using namespace std;

/**
 * @class ServerLogger
 * @brief Handles all formatted output and logging for the server
 * 
 * This class encapsulates all the formatting logic (colors, emojis, boxes)
 * to keep the main server code clean and focused on core logic.
 */
class ServerLogger {
public:
  // Color constants
  static const string RESET;
  static const string GREEN;
  static const string YELLOW;
  static const string RED;
  static const string CYAN;
  static const string BLUE;
  static const string MAGENTA;
  static const string BOLD;

  // Logging functions (thread-safe)
  static void logConnect(int clientNum, const string& ip, int socket, mutex& lock);
  static void logDisconnect(int socket, mutex& lock);
  static void logStatus(int activeClients, mutex& lock);
  static void logGraphCreated(int socket, int vertices, int edges, mutex& lock);
  static void logEdgeAdded(int socket, int u, int v, int w, mutex& lock);
  static void logEdgeRemoved(int socket, int u, int v, mutex& lock);
  static void logMSTComputed(int socket, const string& algorithm, mutex& lock);
  static void logLockRejected(int socket, const string& command, mutex& lock);
  static void logShutdown(const string& message, mutex& lock);
  static void logError(const string& message, mutex& lock);
  static void logCleanup(const string& message, mutex& lock);
  static void logThreads(const string& message, mutex& lock);
  static void logClientClosed(int socket, mutex& lock);

  // Response formatting functions
  static string formatWelcomeMessage();
  static string formatMSTStats(int totalWeight, int diameter, double avgDist, const string& shortestPath);
  
  // Server startup banner
  static void printServerBanner(int port, int socket, mutex& lock);
  
  // Client communication (takes send callback to avoid socket dependency)
  static void sendWelcomeMessage(int clientSock, void (*sendFunc)(int, const string&));
};

#endif

