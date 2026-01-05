#include "ServerLogger.hpp"
#include <cstdio>
#include <iomanip>
#include <sstream>

// Color constants
const string ServerLogger::RESET = "\033[0m";
const string ServerLogger::GREEN = "\033[32m";
const string ServerLogger::YELLOW = "\033[33m";
const string ServerLogger::RED = "\033[31m";
const string ServerLogger::CYAN = "\033[36m";
const string ServerLogger::BLUE = "\033[34m";
const string ServerLogger::MAGENTA = "\033[35m";
const string ServerLogger::BOLD = "\033[1m";

void ServerLogger::logConnect(int clientNum, const string& ip, int socket, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << GREEN << "🔌 [CONNECT] " << RESET << "Client #" << clientNum
       << " connected from " << CYAN << ip << RESET << " (socket: " << socket << ")" << RESET << endl;
}

void ServerLogger::logDisconnect(int socket, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << RED << "❌ [DISCONNECT] " << RESET << "Client " << socket
       << " disconnected" << RESET << endl;
}

void ServerLogger::logStatus(int activeClients, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << BLUE << "📊 [STATUS] " << RESET << "Active clients: " << BOLD
       << activeClients << RESET << endl;
}

void ServerLogger::logGraphCreated(int socket, int vertices, int edges, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << GREEN << "📊 [GRAPH] " << RESET << "Client " << socket
       << " created graph: " << vertices << " vertices, " << edges << " edges" << RESET << endl;
}

void ServerLogger::logEdgeAdded(int socket, int u, int v, int w, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << GREEN << "➕ [EDGE] " << RESET << "Client " << socket
       << " added edge: " << u << " → " << v << " (weight: " << w << ")" << RESET << endl;
}

void ServerLogger::logEdgeRemoved(int socket, int u, int v, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << RED << "➖ [EDGE] " << RESET << "Client " << socket
       << " removed edge: " << u << " → " << v << RESET << endl;
}

void ServerLogger::logMSTComputed(int socket, const string& algorithm, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << MAGENTA << "🔍 [MST] " << RESET << "Client " << socket
       << " computed MST using " << algorithm << " algorithm" << RESET << endl;
}

void ServerLogger::logLockRejected(int socket, const string& command, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << YELLOW << "🔒 [LOCK] " << RESET << "Client " << socket
       << " - " << command << " rejected (graph locked)" << RESET << endl;
}

void ServerLogger::logShutdown(const string& message, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << message << RESET << endl;
}

void ServerLogger::logError(const string& message, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cerr << RED << "❌ [ERROR] " << RESET << message << RESET << endl;
}

void ServerLogger::logCleanup(const string& message, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << YELLOW << "🧹 [CLEANUP] " << RESET << message << RESET << endl;
}

void ServerLogger::logThreads(const string& message, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << CYAN << "🛑 [THREADS] " << RESET << message << RESET << endl;
}

void ServerLogger::logClientClosed(int socket, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << CYAN << "  ✓ Closed client socket: " << socket << RESET << endl;
}

string ServerLogger::formatWelcomeMessage() {
  return "╔═══════════════════════════════════════════════════════╗\n"
         "║     🚀 Welcome to the Graph Computation Server! 🚀     ║\n"
         "╚═══════════════════════════════════════════════════════╝\n\n"
         "📋 Available Commands:\n"
         "  📊 Newgraph n m     - Create a new graph with n vertices and m edges\n"
         "                        (followed by m lines of: u v w)\n"
         "  ➕ AddEdge u v w    - Add an edge from u to v with weight w\n"
         "  ➖ RemoveEdge u v   - Remove the edge from u to v\n"
         "  🔍 Prim             - Compute MST using Prim's algorithm\n"
         "  🔍 Kruskal          - Compute MST using Kruskal's algorithm\n"
         "  👋 Exit             - Disconnect from server\n\n"
         "ℹ️  Note: The graph is shared among all clients.\n"
         "📢 You will be notified when other clients modify the graph.\n\n"
         "═══════════════════════════════════════════════════════\n\n";
}

string ServerLogger::formatMSTStats(int totalWeight, int diameter, double avgDist, const string& shortestPath) {
  string result = "\n╔═══════════════════════════════════════════════════════╗\n";
  result += "║              📊 MST Statistics                        ║\n";
  result += "╠═══════════════════════════════════════════════════════╣\n";
  
  char buffer[200];
  snprintf(buffer, sizeof(buffer), "║  ⚖️  Total Weight:     %30s ║\n", to_string(totalWeight).c_str());
  result += buffer;
  snprintf(buffer, sizeof(buffer), "║  📏 Longest Path:      %30s ║\n", to_string(diameter).c_str());
  result += buffer;
  snprintf(buffer, sizeof(buffer), "║  📈 Average Distance:  %30.6f ║\n", avgDist);
  result += buffer;
  snprintf(buffer, sizeof(buffer), "║  🎯 Shortest Path:     %30s ║\n", shortestPath.c_str());
  result += buffer;
  result += "╚═══════════════════════════════════════════════════════╝\n";
  
  return result;
}

void ServerLogger::printServerBanner(int port, int socket, mutex& lock) {
  unique_lock<mutex> guard(lock);
  cout << "\n" << BOLD << BLUE
       << "╔═══════════════════════════════════════════════════════╗" << RESET
       << endl;
  cout << BOLD << BLUE << "║" << RESET << "  " << GREEN << BOLD
       << "🚀 Graph Computation Server (Leader-Follower)" << RESET << "  "
       << BOLD << BLUE << "║" << RESET << endl;
  cout << BOLD << BLUE
       << "╠═══════════════════════════════════════════════════════╣" << RESET
       << endl;
  cout << BOLD << BLUE << "║" << RESET << "  " << CYAN << "📍 Port: " << BOLD
       << port << RESET << "                                    " << BOLD
       << BLUE << "║" << RESET << endl;
  cout << BOLD << BLUE << "║" << RESET << "  " << CYAN
       << "🔌 Socket: " << socket << RESET
       << "                                  " << BOLD << BLUE << "║" << RESET
       << endl;
  cout << BOLD << BLUE << "║" << RESET << "  " << MAGENTA
       << "👥 Thread Pool: 10 threads" << RESET << "                        "
       << BOLD << BLUE << "║" << RESET << endl;
  cout << BOLD << BLUE << "║" << RESET << "  " << YELLOW
       << "⏳ Waiting for connections..." << RESET << "                    "
       << BOLD << BLUE << "║" << RESET << endl;
  cout << BOLD << BLUE
       << "╚═══════════════════════════════════════════════════════╝" << RESET
       << "\n"
       << endl;
}

void ServerLogger::sendWelcomeMessage(int clientSock, void (*sendFunc)(int, const string&)) {
  sendFunc(clientSock, formatWelcomeMessage());
}

