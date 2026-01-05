#include "Graph.hpp"
#include "LFThreadPool.hpp"
#include "MSTFactory.hpp"
#include "ServerLogger.hpp"
#include "Tree.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <netinet/in.h>
#include <set>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

// Constants
const int port = 4050; ///< Server port number

// Global variables
function<void(int)>
    signalHandlerLambda;     ///< Lambda function for handling signals
atomic<int> clientNumber(0); ///< Tracks the number of connected clients
mutex graphMutex;            ///< Mutex for synchronizing access to the graph
mutex &coutLock =
    LFThreadPool::getOutputMx();  ///< Mutex for synchronizing console output
set<int> connectedClients;        ///< Set of all connected client sockets
mutex clientsMutex;               ///< Mutex to protect connectedClients set
atomic<bool> shutdownFlag(false); ///< Flag to signal graceful shutdown

// Forward declarations
void broadcastToAll(const string &message, int excludeSock);

/**
 * @brief Signal handler function.
 *
 * This function handles interrupt signals (e.g., SIGINT) and performs
 * cleanup operations before exiting the program.
 *
 * @param signum The signal number.
 */
void signalHandler(int signum) {
  ServerLogger::logShutdown(
      ServerLogger::RED + ServerLogger::BOLD + "\n🛑 [SHUTDOWN] " +
          ServerLogger::RESET + ServerLogger::YELLOW +
          "SIGINT received. Initiating graceful shutdown...",
      coutLock);

  // Set shutdown flag first
  shutdownFlag.store(true, memory_order_release);

  // Notify all connected clients that server is shutting down
  ServerLogger::logShutdown("📢 [SHUTDOWN] Notifying all clients...", coutLock);
  string shutdownMsg =
      "\n🛑 [SERVER] Server is shutting down. Connection will be closed.\n";
  // Send shutdown message directly (bypass shutdownFlag check)
  {
    unique_lock<mutex> guard(clientsMutex);
    for (int clientSock : connectedClients) {
      send(clientSock, shutdownMsg.c_str(), shutdownMsg.size(), 0);
    }
  }

  // Wait a moment for notifications to be sent
  this_thread::sleep_for(chrono::milliseconds(200));

  // Close all client connections
  {
    unique_lock<mutex> guard(clientsMutex);
    size_t numClients = connectedClients.size();
    ServerLogger::logShutdown(ServerLogger::YELLOW + "🔌 [SHUTDOWN] Closing " +
                                  to_string(numClients) +
                                  " client connection(s)...",
                              coutLock);

    // Close each client socket
    for (int clientSock : connectedClients) {
      if (clientSock >= 0) {
        string msg = "🛑 Server shutting down. Goodbye!\n";
        send(clientSock, msg.c_str(), msg.size(), 0);
        close(clientSock);
        ServerLogger::logClientClosed(clientSock, coutLock);
      }
    }
    connectedClients.clear();
    clientNumber.store(0, memory_order_release);
  }

  // Wait a bit more for threads to finish current operations
  this_thread::sleep_for(chrono::milliseconds(300));

  // Call cleanup lambda if it exists
  if (signalHandlerLambda) {
    try {
      signalHandlerLambda(signum);
    } catch (const exception &e) {
      ServerLogger::logError("Exception during shutdown: " + string(e.what()),
                             coutLock);
    }
  }

  ServerLogger::logShutdown(ServerLogger::GREEN +
                                "✅ [SHUTDOWN] Server stopped successfully.",
                            coutLock);

  exit(0);
}

/**
 * @brief Sends a response to the client.
 *
 * @param clientSock The client's socket descriptor.
 * @param response The response string to be sent.
 */
void sendResponse(int clientSock, const string &response) {
  if (send(clientSock, response.c_str(), response.size(), 0) < 0) {
    cerr << "send error" << endl;
  }
}

/**
 * @brief Broadcasts a message to all connected clients.
 *
 * @param message The message to broadcast.
 * @param excludeSock Optional socket to exclude from broadcast (e.g., the
 * sender).
 */
void broadcastToAll(const string &message, int excludeSock = -1) {
  // Don't broadcast during shutdown (clients are being closed)
  if (shutdownFlag.load(memory_order_acquire)) {
    return;
  }

  unique_lock<mutex> guard(clientsMutex);
  for (auto it = connectedClients.begin(); it != connectedClients.end();) {
    int clientSock = *it;

    // Skip the sender (they already got the direct response)
    if (clientSock == excludeSock) {
      ++it;
      continue;
    }

    if (send(clientSock, message.c_str(), message.size(), 0) < 0) {
      // Client disconnected, remove from set
      it = connectedClients.erase(it);
      clientNumber.store(clientNumber.load(memory_order_acquire) - 1,
                         memory_order_release);
    } else {
      ++it;
    }
  }
}

/**
 * @brief Scans the graph input from the client.
 *
 * @param clientSock The client's socket descriptor.
 * @param n Number of vertices.
 * @param m Number of edges.
 * @param ss The stringstream containing the client's input.
 * @param g Unique pointer to the graph object.
 * @return int 0 if successful, -1 otherwise.
 */
int scanGraph(int clientSock, int &n, int &m, stringstream &ss,
              unique_ptr<Graph> &g) {
  if (!(ss >> n >> m) || n <= 0 || m < 0) {
    cerr << "Invalid graph input" << endl;
    return -1;
  }

  g = make_unique<Graph>(n, m);
  return 0;
}

/**
 * @brief Handles commands sent by the client.
 *
 * This function processes various commands related to graph operations and MST
 * calculations.
 *
 * @param clientSock The client's socket descriptor.
 * @param g Unique pointer to the graph object.
 * @param factory The MSTFactory object for creating MSTs.
 * @param mst Unique pointer to the MST (Tree) object.
 */
void handleCommands(int clientSock, unique_ptr<Graph> &g, MSTFactory &factory,
                    unique_ptr<Tree> &mst) {
  // Send welcome message when client first connects
  ServerLogger::sendWelcomeMessage(clientSock, sendResponse);

  char buffer[1024];
  int bytesReceived;

  while (!shutdownFlag.load(memory_order_acquire)) {
    bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
    if (bytesReceived <= 0) {
      // Remove client from connected set
      {
        unique_lock<mutex> guard(clientsMutex);
        connectedClients.erase(clientSock);
      }
      clientNumber.store(clientNumber.load(memory_order_acquire) - 1,
                         memory_order_release);
      if (bytesReceived == 0) {
        ServerLogger::logDisconnect(clientSock, coutLock);
        break;
      } else {
        if (shutdownFlag.load(memory_order_acquire)) {
          break;
        }
        ServerLogger::logError(
            "recv() failed for client " + to_string(clientSock), coutLock);
        continue;
      }
    }

    // Check shutdown flag after recv (might have been set while blocking)
    if (shutdownFlag.load(memory_order_acquire)) {
      ServerLogger::logShutdown(
          ServerLogger::YELLOW + "⚠️  [SHUTDOWN] Client " +
              to_string(clientSock) +
              " - Server shutting down, closing connection",
          coutLock);
      break;
    }
    buffer[bytesReceived] = '\0';
    string command(buffer);
    if (command.empty())
      continue;

    stringstream ss(command);
    string cmd;
    string response;
    ss >> cmd;

    if (cmd == "Newgraph") {
      unique_lock<mutex> guard(graphMutex, try_to_lock);
      if (!guard.owns_lock()) {
        response =
            "⚠️  Graph is being used by another client. Please retry...\n";
        sendResponse(clientSock, response);
        ServerLogger::logLockRejected(clientSock, "Newgraph", coutLock);
        continue;
      }

      // Notify other clients that graph is being modified
      string startNotification =
          "\n📢 [NOTIFICATION] Graph is being reset and recreated. "
          "Graph modifications are temporarily locked.\n";
      broadcastToAll(startNotification, clientSock);

      if (g != nullptr) {
        g.reset();
        g = nullptr;
      }
      if (mst != nullptr) {
        mst.reset();
        mst = nullptr;
      }
      int n, m;
      int res = scanGraph(clientSock, n, m, ss, g);
      if (res == -1) {
        sendResponse(
            clientSock,
            "Invalid graph input. Please enter 2 integers for n and m.\n");
        continue;
      }

      // Wait for the graph to be created
      for (int i = 0; i < m; i++) {
        int u = 0, v = 0, w = 0;

        // Attempt to read and parse the edge data
        if (!(ss >> u >> v >> w)) {
          ss.clear();
          ss.str("");
          memset(buffer, 0, sizeof(buffer));
          bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);

          if (bytesReceived <= 0) {
            clientNumber.store(clientNumber.load(memory_order_acquire) - 1,
                               memory_order_release);
            if (bytesReceived == 0) {
              ServerLogger::logDisconnect(clientSock, coutLock);
              break;
            } else {
              ServerLogger::logError("recv error: " + string(strerror(errno)),
                                     coutLock);
              break;
            }
          }

          // Check if we received a notification or other non-edge data
          // Notifications start with "[NOTIFICATION]" or new commands
          string receivedData(buffer, bytesReceived);
          if (receivedData.find("[NOTIFICATION]") != string::npos ||
              receivedData.find("Newgraph") != string::npos ||
              receivedData.find("AddEdge") != string::npos ||
              receivedData.find("RemoveEdge") != string::npos ||
              receivedData.find("Prim") != string::npos ||
              receivedData.find("Kruskal") != string::npos) {
            // This is not edge data, skip it and try again
            i--;
            continue;
          }

          ss.write(buffer, bytesReceived);

          if (!(ss >> u >> v >> w)) {
            sendResponse(clientSock, "Invalid input format. Please enter 3 "
                                     "integers for u, v, and w.\n");
            i--;
            continue;
          }
        }

        if (u < 0 || u > n || v < 0 || v > n || w < 0 || u == v) {
          sendResponse(clientSock,
                       "Invalid edge values. Vertices should be in the range "
                       "[1, n] and weight should be non-negative.\n");
          i--;
          continue;
        }

        g->addEdge(u, v, w);
      }
      response = "✅ Graph created with " + to_string(n) + " vertices and " +
                 to_string(m) + " edges.\n";

      ServerLogger::logGraphCreated(clientSock, n, m, coutLock);

      // Broadcast notification to all OTHER clients (exclude sender)
      string notification =
          "\n📢 [NOTIFICATION] Graph was reset and recreated with " +
          to_string(n) + " vertices and " + to_string(m) + " edges.\n";
      broadcastToAll(notification, clientSock);
    } else if (cmd == "Prim" || cmd == "Kruskal") {
      unique_lock<mutex> guard(graphMutex, try_to_lock);
      if (!guard.owns_lock()) {
        response =
            "⚠️  Graph is being used by another client. Please retry...\n";
        sendResponse(clientSock, response);
        ServerLogger::logLockRejected(clientSock, cmd, coutLock);
        continue;
      }

      if (g == nullptr || g->getAdj().empty()) {
        cerr << "Graph not initialized" << endl;
        continue;
      }

      if (mst != nullptr) {
        mst.reset();
        mst = nullptr;
      }

      if (cmd == "Prim") {
        factory.setStrategy(new PrimStrategy());
      } else if (cmd == "Kruskal") {
        factory.setStrategy(new KruskalStrategy());
      }

      else {
        sendResponse(clientSock, "Invalid command: " + cmd + "\n");
        continue;
      }

      mst = factory.createMST(g);
      ServerLogger::logMSTComputed(clientSock, cmd, coutLock);

      response = "✅ MST created using " + cmd + " algorithm.\n\n";
      response += mst->printMST();
      response += ServerLogger::formatMSTStats(
          mst->totalWeight(), mst->diameter(), mst->averageDistanceEdges(),
          mst->shortestPath());

    } else if (cmd == "AddEdge") {
      unique_lock<mutex> guard(graphMutex, try_to_lock);
      if (!guard.owns_lock()) {
        response =
            "⚠️  Graph is being used by another client. Please retry...\n";
        sendResponse(clientSock, response);
        ServerLogger::logLockRejected(clientSock, "AddEdge", coutLock);
        continue;
      }

      if (g == nullptr || g->getAdj().empty()) {
        response = "Graph not initialized.\n";
        sendResponse(clientSock, response);
        continue;
      }

      int u = 0, v = 0, w = 0;
      ss >> u >> v >> w;
      if (u < 0 || u > g->getVerticesNumber() || v < 0 ||
          v > g->getVerticesNumber() || w < 0) {
        response = "Invalid edge values. Vertices should be in the range [1, "
                   "n] and weight should be non-negative.\n";
      } else {
        bool res = g->addEdge(u, v, w);

        if (!res) {
          response = "Invalid edge. Vertices should range from 1 to " +
                     to_string(g->getVerticesNumber()) +
                     ". or edge already exists.\n";
        } else {
          response = "Edge added between " + to_string(u) + " and " +
                     to_string(v) + " with weight " + to_string(w) + "\n";

          ServerLogger::logEdgeAdded(clientSock, u, v, w, coutLock);

          // Broadcast notification to all OTHER clients (exclude sender)
          string notification =
              "\n📢 [NOTIFICATION] Edge added: " + to_string(u) + " → " +
              to_string(v) + " (weight: " + to_string(w) + ")\n";
          broadcastToAll(notification, clientSock);
        }
      }
    }

    else if (cmd == "RemoveEdge") {
      unique_lock<mutex> guard(graphMutex, try_to_lock);
      if (!guard.owns_lock()) {
        response =
            "⚠️  Graph is being used by another client. Please retry...\n";
        sendResponse(clientSock, response);
        ServerLogger::logLockRejected(clientSock, "RemoveEdge", coutLock);
        continue;
      }

      if (g == nullptr || g->getAdj().empty()) {
        response = "Graph not initialized.\n";
        sendResponse(clientSock, response);
        continue;
      }

      int u = 0, v = 0;
      ss >> u >> v;
      if (u < 0 || u > g->getVerticesNumber() || v < 0 ||
          v > g->getVerticesNumber()) {
        response =
            "Invalid edge values. Vertices should be in the range [1, n].\n";
      } else {
        if (!g->removeEdge(u, v)) {
          response = "Edge not found between " + to_string(u) + " and " +
                     to_string(v) + "\n";
        } else {
          response = "Edge removed between " + to_string(u) + " and " +
                     to_string(v) + "\n";

          ServerLogger::logEdgeRemoved(clientSock, u, v, coutLock);

          // Broadcast notification to all OTHER clients (exclude sender)
          string notification =
              "\n📢 [NOTIFICATION] Edge removed: " + to_string(u) + " → " +
              to_string(v) + "\n";
          broadcastToAll(notification, clientSock);
        }
      }
    }

    else if (cmd == "Exit") {
      sendResponse(clientSock, "Goodbye\n");
      ServerLogger::logDisconnect(clientSock, coutLock);
      clientNumber.store(clientNumber.load(memory_order_acquire) - 1,
                         memory_order_release);
      break;
    } else {
      response = "Invalid command: " + cmd + "\n";
    }
    // Send the response to the client
    sendResponse(clientSock, response);
  }

  // Cleanup: remove from connected set and close socket
  {
    unique_lock<mutex> guard(clientsMutex);
    connectedClients.erase(clientSock);
  }

  if (clientSock >= 0) {
    close(clientSock);
  }
}

/**
 * @brief Accepts incoming connections and assigns them to a thread in the pool.
 *
 * This function handles the process of accepting new client connections,
 * creating a handler for the connection, and assigning the handler to a thread
 * in the thread pool.
 *
 * @param server_sock The server's socket descriptor.
 * @param g Unique pointer to the graph object.
 * @param factory The MSTFactory object for creating MSTs.
 * @param mst Unique pointer to the MST (Tree) object.
 * @param pool Unique pointer to the thread pool.
 */
void acceptConnection(int server_sock, unique_ptr<Graph> &g,
                      MSTFactory &factory, unique_ptr<Tree> &mst,
                      unique_ptr<LFThreadPool> &pool) {
  if (shutdownFlag.load(memory_order_acquire)) {
    return; // Don't accept new connections during shutdown
  }

  struct sockaddr_in client_addr;
  socklen_t sin_size = sizeof(client_addr);
  int client_sock =
      accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
  if (client_sock == -1) {
    perror("accept");
    return;
  }
  clientNumber.store(clientNumber.load(memory_order_acquire) + 1,
                     memory_order_release);
  char s[INET6_ADDRSTRLEN];
  inet_ntop(client_addr.sin_family, &client_addr.sin_addr, s, sizeof s);
  ServerLogger::logConnect(clientNumber.load(), string(s), client_sock,
                           coutLock);
  ServerLogger::logStatus(clientNumber.load(), coutLock);
  // Add client to connected set
  {
    unique_lock<mutex> guard(clientsMutex);
    connectedClients.insert(client_sock);
  }

  function<void()> commandHandler = [client_sock, &g, &factory, &mst]() {
    handleCommands(client_sock, g, factory, mst);
  };
  pool->addFd(client_sock, commandHandler);
}

/**
 * @brief Main function to start the MST server.
 *
 * This function sets up the server, initializes the reactor and thread pool,
 * and handles incoming connections.
 *
 * @return int Returns 0 on successful execution.
 */
int main() {
  signal(SIGINT, signalHandler);
  unique_ptr<Reactor> reactor = make_unique<Reactor>();
  unique_ptr<Graph> g;
  unique_ptr<Tree> t;
  MSTFactory factory;
  unique_ptr<LFThreadPool> pool;
  int serverSock;

  signalHandlerLambda = [&](int signum) {
    ServerLogger::logCleanup("Freeing resources...", coutLock);

    // Stop thread pool FIRST (before closing server socket)
    if (pool) {
      ServerLogger::logThreads(
          "Stopping thread pool (this may take a moment)...", coutLock);
      pool->stopPool();
      ServerLogger::logShutdown(
          ServerLogger::GREEN + "✅ [THREADS] All threads stopped.", coutLock);
    }

    // Now close server socket (threads are stopped, so no select() calls)
    if (serverSock >= 0) {
      ServerLogger::logCleanup("Closing server socket...", coutLock);
      close(serverSock);
    }

    // Cleanup resources
    ServerLogger::logCleanup("Freeing memory...", coutLock);
    factory.destroyStrategy();
    reactor.reset();
    pool.reset();
    g.reset();
    t.reset();

    ServerLogger::logCleanup(
        ServerLogger::GREEN + "✅ [CLEANUP] All resources freed.", coutLock);
  };

  struct sockaddr_in serverAddr;
  int opt = 1;

  if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    cerr << "[Server] Socket creation error" << endl;
    exit(1);
  }

  if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    cerr << "[Server] Setsockopt error" << endl;
    exit(1);
  }

  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(port);
  memset(&(serverAddr.sin_zero), '\0', 8);

  if (::bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) <
      0) {
    ServerLogger::logError("Bind failed", coutLock);
    exit(1);
  }

  if (listen(serverSock, 10) < 0) {
    ServerLogger::logError("Listen failed", coutLock);
    exit(1);
  }

  ServerLogger::printServerBanner(port, serverSock, coutLock);

  pool = make_unique<LFThreadPool>(10, *reactor);
  reactor->addHandle(serverSock, [serverSock, &g, &factory, &t, &pool]() {
    acceptConnection(serverSock, g, factory, t, pool);
  });
  while (!shutdownFlag.load(memory_order_acquire)) {
    this_thread::sleep_for(chrono::milliseconds(100));
  }
  return 0;
}
