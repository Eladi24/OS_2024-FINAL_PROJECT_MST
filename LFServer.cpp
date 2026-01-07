#include "Graph.hpp"
#include "LFThreadPool.hpp"
#include "MSTFactory.hpp"
#include "ServerConnection.hpp"
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
#include <tuple>
#include <unistd.h>
#include <vector>

// Constants
const int port = ServerConnection::DEFAULT_PORT; 
const int BUFFER_SIZE = 1024;                     
const int THREAD_POOL_SIZE = 10;                  
const int SHUTDOWN_CLEANUP_DELAY_MS = 300;      
const int MAIN_LOOP_SLEEP_MS = 100;              

// Global variables
function<void(int)>
    signalHandlerLambda;     
atomic<int> clientNumber(0); 
mutex graphMutex;            
mutex &coutLock =LFThreadPool::getOutputMx();  
set<int> connectedClients;        
mutex clientsMutex;               
atomic<bool> shutdownFlag(false); 


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

  shutdownFlag.store(true, memory_order_release);

  // Notify and close all client connections
  string shutdownMsg =
      "\n🛑 [SERVER] Server is shutting down. Connection will be closed.\n";
  {
    unique_lock<mutex> guard(clientsMutex);
    ServerLogger::logCleanup("Closing " + to_string(connectedClients.size()) + " client connection(s)", coutLock);
    for (int clientSock : connectedClients) {
      if (clientSock >= 0) {
        send(clientSock, shutdownMsg.c_str(), shutdownMsg.size(), 0);
        close(clientSock);
        ServerLogger::logClientClosed(clientSock, coutLock);
      }
    }
    connectedClients.clear();
    clientNumber.store(0, memory_order_release);
  }

  // Wait for threads to finish current operations
  ServerLogger::logCleanup("Waiting for threads to finish operations...", coutLock);
  this_thread::sleep_for(chrono::milliseconds(SHUTDOWN_CLEANUP_DELAY_MS));

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
                                "✅ [SHUTDOWN] LF Server stopped successfully.",
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
    ServerLogger::logError("send() failed for client " + to_string(clientSock), coutLock);
  }
}

/**
 * @brief Validates and parses Newgraph command input (SERVER responsibility)
 
 * @param clientSock Client socket (for reading additional packets)
 * @param ss Stringstream with initial command data
 * @param buffer Buffer for receiving data
 * @param n Output: Number of vertices
 * @param m Output: Number of edges
 * @param edges Output: Vector of edges (u, v, w)
 * @param errorMsg Output: Error message if validation fails
 * @return true if all validation passes, false otherwise
 */
bool validateAndReadNewgraph(int clientSock, stringstream& ss, char* buffer,
                              int& n, int& m, 
                              vector<tuple<int, int, int>>& edges,
                              string& errorMsg) {
  if (!(ss >> n >> m)) {
    errorMsg = "Invalid graph input. Please enter 2 integers for n and m.\n";
    return false;
  }
  
  if (n <= 0 || m < 0) {
    errorMsg = "Invalid graph input. n must be > 0 and m must be >= 0.\n";
    return false;
  }

  int maxEdges = (n * (n - 1)) / 2;
  if (m > maxEdges) {
    errorMsg = "Invalid graph parameters: A graph with " + to_string(n) +
               " vertices can have at most " + to_string(maxEdges) +
               " edges, but " + to_string(m) + " edges were requested.\n";
    return false;
  }
  

  edges.clear();
  
  for (int i = 0; i < m; i++) {
    int u = 0, v = 0, w = 0;
    
    // Try to read edge from current stringstream first
    ss.clear();  // Clear any error flags
    if (!(ss >> u >> v >> w)) {
      // Need more data - read next packet
      ss.clear();
      ss.str("");
      memset(buffer, 0, BUFFER_SIZE);
      int bytesReceived = recv(clientSock, buffer, BUFFER_SIZE, 0);
      
      if (bytesReceived <= 0) {
        {
          unique_lock<mutex> guard(clientsMutex);
          if (connectedClients.find(clientSock) == connectedClients.end()) {
            errorMsg = "Connection closed while reading edges.\n";
            return false;
          }
        }
        errorMsg = "Error reading edges. Connection closed or error occurred.\n";
        return false;
      }
      
      ss.clear();
      ss.str("");
      ss.write(buffer, bytesReceived);
      ss.seekg(0);
      ss.clear();
      
      if (!(ss >> u >> v >> w)) {
        // Invalid format - didn't read all three values
        errorMsg = "Invalid input format. Expected " + to_string(m) + 
                   " edges (format: u v w), but received invalid data at edge " + 
                   to_string(i + 1) + ". Newgraph command aborted. Please start with a new command.\n";
        return false;
      }
    }
    
    // Validate edge values
    if (u < 0 || u > n || v < 0 || v > n || w < 0 || u == v) {
      errorMsg = "Invalid edge values. Vertices should be in the range [1, n] "
                 "and weight should be non-negative.\n";
      return false;
    }
    
    edges.push_back({u, v, w});
  }
  
  return true; 
}
/**
 * @brief Validates that the graph is initialized.
 *
 * @param g Unique pointer to the graph object.
 * @return true if graph is valid, false otherwise.
 */
 bool validateGraph(const unique_ptr<Graph> &g) {
  return g != nullptr && !g->getAdj().empty();
}


// Forward declarations for command handlers
struct CommandResult {
  bool shouldContinue;
  bool shouldBreak;
  string response;
};

CommandResult handleNewgraph(int clientSock, stringstream &ss,
                             unique_ptr<Graph> &g, unique_ptr<Tree> &mst,
                             char *buffer);
CommandResult handleMST(int clientSock, const string &cmd,
                        unique_ptr<Graph> &g, MSTFactory &factory,
                        unique_ptr<Tree> &mst);
CommandResult handleAddEdge(int clientSock, stringstream &ss,
                            unique_ptr<Graph> &g);
CommandResult handleRemoveEdge(int clientSock, stringstream &ss,
                                unique_ptr<Graph> &g);
CommandResult handleExit(int clientSock);

/**
 * @brief Attempts to acquire the graph lock with error handling.
 *
 * @param clientSock The client's socket descriptor.
 * @param cmdName The command name for logging.
 * @return unique_ptr<unique_lock<mutex>> Lock if acquired, nullptr otherwise.
 */
unique_ptr<unique_lock<mutex>> tryLockGraph(int clientSock,
                                             const string &cmdName) {
  auto guard = make_unique<unique_lock<mutex>>(graphMutex, try_to_lock);
  if (!guard->owns_lock()) {
    string response =
        "Graph is being used by another client. Cannot execute " + cmdName + " command.\n";
    sendResponse(clientSock, response);
    ServerLogger::logLockRejected(clientSock, cmdName, coutLock);
    return nullptr;
  }
  return guard;
}



CommandResult handleNewgraph(int clientSock, stringstream &ss,
                             unique_ptr<Graph> &g, unique_ptr<Tree> &mst,
                             char *buffer) {
  // Step 1: Validate and collect all edges BEFORE locking
  int n, m;
  vector<tuple<int, int, int>> edges;
  string errorMsg;
  
  if (!validateAndReadNewgraph(clientSock, ss, buffer, n, m, edges, errorMsg)) {
    return {true, false, errorMsg};
  }

  // Step 2: Lock acquisition (only after validation passes)
  auto guard = tryLockGraph(clientSock, "Newgraph");
  if (!guard) {
    return {true, false, ""}; // continue
  }

  // Step 3: Reset existing graph and MST
  g.reset();
  mst.reset();

  // Step 4: Create new graph (all validation passed)
  g = make_unique<Graph>(n, m);
  
  // Step 5: Add all pre-validated edges
  for (const auto& [u, v, w] : edges) {
    g->addEdge(u, v, w);  
  }

  // Step 6: Log and respond (lock will be released when guard goes out of scope)
  ServerLogger::logGraphCreated(clientSock, n, m, coutLock);

  string response = "\nGraph created with " + to_string(n) + " vertices and " +
                   to_string(m) + " edges.\n";
  return {false, false, response};
}
/**
 * @brief Handles the AddEdge command.
 * 
 * 
 * 1. Format validation (parse input) - NO LOCK
 * 2. Lock acquisition
 * 3. Graph state check
 * 4. Graph operation (Graph validates internally)
 * 5. Handle result and respond
 */
 CommandResult handleAddEdge(int clientSock, stringstream &ss,
  unique_ptr<Graph> &g) {

int u, v, w;
if (!(ss >> u >> v >> w)) {
return {true, false, "Invalid format. Please provide 3 integers: u v w\n"};
}

// Step 2: Lock acquisition
auto guard = tryLockGraph(clientSock, "AddEdge");
if (!guard) {
return {true, false, ""}; // continue
}

// Step 3: Graph state check
if (!validateGraph(g)) {
return {true, false, "Graph not initialized.\n"};
}

// Step 4: Graph operation (Graph validates internally)
bool success = g->addEdge(u, v, w);

// Step 5: Handle result
if (!success) {
return {false, false,
"Invalid edge. Vertices should be in the range [1, n] and "
"weight should be non-negative, or edge already exists.\n"};
}

ServerLogger::logEdgeAdded(clientSock, u, v, w, coutLock);

string response = "Edge added between vertices " + to_string(u) + " and " +
to_string(v) + " with weight " + to_string(w) + ".\n";
return {false, false, response};
}

/**
* @brief Handles the RemoveEdge command.
* 
* Matches PipelineServer's pattern:
* 1. Format validation (parse input) - NO LOCK
* 2. Lock acquisition
* 3. Graph state check
* 4. Graph operation (Graph validates internally)
* 5. Handle result and respond
*/
CommandResult handleRemoveEdge(int clientSock, stringstream &ss,
      unique_ptr<Graph> &g) {
// Step 1: Format validation (NO LOCK)
int u, v;
if (!(ss >> u >> v)) {
return {true, false, "Invalid format. Please provide 2 integers: u v\n"};
}

// Step 2: Lock acquisition
auto guard = tryLockGraph(clientSock, "RemoveEdge");
if (!guard) {
return {true, false, ""}; // continue
}

// Step 3: Graph state check
if (!validateGraph(g)) {
return {true, false, "Graph not initialized.\n"};
}

// Step 4: Graph operation (Graph validates internally)
bool success = g->removeEdge(u, v);

// Step 5: Handle result
if (!success) {
return {false, false,
"Edge between vertices " + to_string(u) + " and " +
to_string(v) + " does not exist.\n"};
}

ServerLogger::logEdgeRemoved(clientSock, u, v, coutLock);

string response =
"Edge removed between vertices " + to_string(u) + " and " +
to_string(v) + ".\n";
return {false, false, response};
}

/**
 * @brief Handles Prim and Kruskal MST commands.
 */
CommandResult handleMST(int clientSock, const string &cmd,
                        unique_ptr<Graph> &g, MSTFactory &factory,
                        unique_ptr<Tree> &mst) {
  auto guard = tryLockGraph(clientSock, cmd);
  if (!guard) {
    return {true, false, ""};
  }

  if (!validateGraph(g)) {
    ServerLogger::logError("Graph not initialized for MST computation", coutLock);
    return {false, false, "Graph not initialized.\n"};
  }

  // Reset existing MST
  mst.reset();

  // Set strategy based on command
  if (cmd == "Prim") {
    factory.setStrategy(new PrimStrategy());
  } else if (cmd == "Kruskal") {
    factory.setStrategy(new KruskalStrategy());
  } else {
    return {true, false, "Invalid command: " + cmd + "\n"};
  }

  mst = factory.createMST(g);
  ServerLogger::logMSTComputed(clientSock, cmd, coutLock);

  string response = "MST created using " + cmd + " algorithm.\n\n";
  response += mst->printMST();
  response += "TOTAL WEIGHT OF THE MST IS: " + to_string(mst->totalWeight()) + "\n\n";
  response += "THE LONGEST PATH (DIAMETER) OF THE MST IS: " + to_string(mst->diameter()) + "\n\n";
  response += "AVERAGE DISTANCE OF THE MST IS: " + to_string(mst->averageDistanceEdges()) + "\n\n";
  response += "SHORTEST PATH IS: " + mst->shortestPath() + "\n";

  return {false, false, response};
}

/**
 * @brief Handles the Exit command.
 */
CommandResult handleExit(int clientSock) {
  sendResponse(clientSock, "Goodbye\n");
  ServerLogger::logDisconnect(clientSock, coutLock);
  clientNumber.store(clientNumber.load(memory_order_acquire) - 1,
                     memory_order_release);
  return {false, true, ""}; // break
}

/**
 * @brief Handles commands sent by the client.
 *
 * This function processes various commands related to graph operations and MST
 * calculations. Refactored to use separate command handler functions for
 * better maintainability.
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

  char buffer[BUFFER_SIZE];
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
    if (command.empty()) {
      continue;
    }

    stringstream ss(command);
    string cmd;
    ss >> cmd;

    // Dispatch command to appropriate handler
    CommandResult result;
    if (cmd == "Newgraph") {
      result = handleNewgraph(clientSock, ss, g, mst, buffer);
    } else if (cmd == "Prim" || cmd == "Kruskal") {
      result = handleMST(clientSock, cmd, g, factory, mst);
    } else if (cmd == "AddEdge") {
      result = handleAddEdge(clientSock, ss, g);
    } else if (cmd == "RemoveEdge") {
      result = handleRemoveEdge(clientSock, ss, g);
    } else if (cmd == "Exit") {
      result = handleExit(clientSock);
    } else {
      result = {false, false, "Invalid command: " + cmd + "\n"};
    }


    if (result.shouldBreak) {
      break;
    }
    // Send response before continuing (if there's an error message)
    if (!result.response.empty()) {
      sendResponse(clientSock, result.response);
    }
    if (result.shouldContinue) {
      continue;
    }
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
    // Stop thread pool FIRST (before closing server socket)
    if (pool) {
      ServerLogger::logCleanup("Stopping thread pool", coutLock);
      pool->stopPool();
    }

    // Now close server socket (threads are stopped, so no select() calls)
    if (serverSock >= 0) {
      ServerLogger::logCleanup("Closing server socket", coutLock);
      close(serverSock);
    }

    // Cleanup resources
    ServerLogger::logCleanup("Destroying MST strategy", coutLock);
    factory.destroyStrategy();
    
    ServerLogger::logCleanup("Resetting Reactor", coutLock);
    reactor.reset();
    
    ServerLogger::logCleanup("Resetting Thread Pool", coutLock);
    pool.reset();
    
    ServerLogger::logCleanup("Resetting Graph", coutLock);
    g.reset();
    
    ServerLogger::logCleanup("Resetting MST", coutLock);
    t.reset();
  };

  // Create server socket using connection utilities
  serverSock = ServerConnection::createServerSocket(port);
  if (serverSock < 0) {
    ServerLogger::logError("Failed to create server socket", coutLock);
    exit(1);
  }

  ServerLogger::printServerBanner(port, serverSock, coutLock);

  pool = make_unique<LFThreadPool>(THREAD_POOL_SIZE, *reactor);
  reactor->addHandle(serverSock, [serverSock, &g, &factory, &t, &pool]() {
    acceptConnection(serverSock, g, factory, t, pool);
  });
  while (!shutdownFlag.load(memory_order_acquire)) {
    this_thread::sleep_for(chrono::milliseconds(MAIN_LOOP_SLEEP_MS));
  }
  return 0;
}
