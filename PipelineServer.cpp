#include "ActiveObject.hpp"
#include "Graph.hpp"
#include "MSTFactory.hpp"
#include "ServerConnection.hpp"
#include "ServerLogger.hpp"
#include "Tree.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <future>
#include <netinet/in.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

// Constants
const int port = 4050; 
const int PIPELINE_SIZE = 7;
const int BUFFER_SIZE = 1024; 

// Global variables
function<void(int)>
    signalHandlerLambda;     
atomic<int> clientNumber(0); 
mutex graphLock;        
mutex futureLock; 
mutex &coutLock =ActiveObject::getOutputMutex();

atomic<bool>terminateFlag(false);
set<int> connectedClients;
mutex clientsMutex; 

/**
 * @struct functArgs
 *
 * @brief Holds arguments for thread functions handling client commands.
 *
 * This struct contains references to the client's socket, the pipeline of
 * ActiveObjects, smart pointers to the graph and MST, and the MST factory.
 */
struct functArgs {
  int clientSock; 
  vector<unique_ptr<ActiveObject>>
      &pipeline;         
  unique_ptr<Graph> &g;  
  MSTFactory &factory;   
  unique_ptr<Tree> &mst; 
};

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
  terminateFlag.store(true);
  unique_lock<mutex> guard(graphLock);
  signalHandlerLambda(signum);
  guard.unlock();
  ServerLogger::logShutdown(ServerLogger::GREEN +
                                "✅ [SHUTDOWN] Pipeline Server stopped successfully.",
                            coutLock);
  exit(signum);
}


/**
 * @brief Validates and parses Newgraph command input (SERVER responsibility)
 * 
 * Handles all input validation in one place:
 * - Parse n, m from stringstream
 * - Validate n > 0, m >= 0
 * - Validate maxEdges constraint
 * - Read all edges with validation
 *
 * @param clientSock Client socket (for reading additional packets)
 * @param ss Stringstream with initial command data
 * @param ssLock Mutex for stringstream
 * @param buffer Buffer for receiving data
 * @param bytesReceived Reference to bytes received
 * @param n Output: Number of vertices
 * @param m Output: Number of edges
 * @param edges Output: Vector of edges (u, v, w)
 * @param errorMsg Output: Error message if validation fails
 * @return true if all validation passes, false otherwise
 */
bool validateAndReadNewgraph(int clientSock, stringstream& ss, mutex& ssLock,
                              char* buffer, int& bytesReceived,
                              int& n, int& m, vector<tuple<int, int, int>>& edges,
                              string& errorMsg) {
  // Step 1: Parse n, m
  {
    unique_lock<mutex> guard(ssLock);
    if (!(ss >> n >> m)) {
      errorMsg = "Invalid graph input. Please enter 2 integers for n and m.\n";
      return false;
    }
  }
  
  // Step 2: Validate n > 0, m >= 0
  if (n <= 0 || m < 0) {
    errorMsg = "Invalid graph input. n must be > 0 and m must be >= 0.\n";
    return false;
  }
  
  // Step 3: Validate maxEdges constraint
  int maxEdges = (n * (n - 1)) / 2;
  if (m > maxEdges) {
    errorMsg = "Invalid graph parameters: A graph with " + to_string(n) +
               " vertices can have at most " + to_string(maxEdges) +
               " edges, but " + to_string(m) + " edges were requested.\n";
    return false;
  }
  
  // Step 4: Read and validate all edges
  edges.clear();
  for (int i = 0; i < m; i++) {
    int u = 0, v = 0, w = 0;
    
    // Parse edge from stringstream (handle multi-packet)
    {
      unique_lock<mutex> guard(ssLock);
      if (!(ss >> u >> v >> w)) {
        // Need more data - read next packet
        ss.clear();
        ss.str("");
        memset(buffer, 0, 1024);
        bytesReceived = recv(clientSock, buffer, BUFFER_SIZE, 0);
        
        if (bytesReceived <= 0) {
          errorMsg = "Error reading edges. Connection closed or error occurred.\n";
          return false;
        }
        
        ss.write(buffer, bytesReceived);
        if (!(ss >> u >> v >> w)) {
          // Invalid format - abort Newgraph command, client must start fresh
          errorMsg = "Invalid input format. Expected " + to_string(m) + 
                     " edges (format: u v w), but received invalid data at edge " + 
                     to_string(i + 1) + ". Newgraph command aborted. Please start with a new command.\n";
          return false;
        }
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
  
  return true;  // All validation passed
}

/**
 * @brief Helper function: Set response and signal completion (for ActiveObject tasks)
 * 
 * Encapsulates the common pattern in ActiveObject lambdas:
 * 1. Lock futureLock
 * 2. Set response string
 * 3. Set done flag
 * 4. Notify waiting thread
 *
 * @param response Response string to send to client
 * @param future Reference to response string (shared with server thread)
 * @param done Reference to completion flag
 * @param cv Condition variable for signaling
 */
inline void setResponseAndSignal(const string& response, string& future,
                                  atomic<bool>& done, condition_variable& cv) {
  unique_lock<mutex> futureGuard(futureLock);
  future = response;
  done.store(true, memory_order_release);
  cv.notify_one();
}

/**
 * @brief Helper function: Append to response string (for pipeline stages)
 * 
 * Thread-safe append to future string. Used when building response incrementally.
 *
 * @param text Text to append
 * @param future Reference to response string (shared with server thread)
 */
inline void appendToResponse(const string& text, string& future) {
  unique_lock<mutex> futureGuard(futureLock);
  future += text;
}

/**
 * @brief Helper function: Signal completion without modifying response (for pipeline stages)
 * 
 * Used when response is built incrementally across pipeline stages.
 * Only signals completion without changing the response string.
 *
 * @param future Reference to response string (shared with server thread)
 * @param done Reference to completion flag
 * @param cv Condition variable for signaling
 */
inline void signalCompletion(string& future, atomic<bool>& done,
                            condition_variable& cv) {
  unique_lock<mutex> futureGuard(futureLock);
  done.store(true, memory_order_release);
  cv.notify_one();
}

/**
 * @brief Helper function: Validate graph is initialized and non-empty
 * 
 * Checks if graph exists and has edges. Used by ActiveObject tasks.
 *
 * @param g Reference to graph pointer
 * @return true if graph is valid, false otherwise
 */
inline bool isGraphValid(const unique_ptr<Graph>& g) {
  return g != nullptr && !g->getAdj().empty();
}

/**
 * @brief Helper function: Reset graph and MST (for Newgraph command)
 * 
 * Safely resets both graph and MST pointers.
 *
 * @param g Reference to graph pointer
 * @param mst Reference to MST pointer
 */
inline void resetGraphAndMST(unique_ptr<Graph>& g, unique_ptr<Tree>& mst) {
  if (g != nullptr) {
    g.reset();
    g = nullptr;
  }
  if (mst != nullptr) {
    mst.reset();
    mst = nullptr;
  }
}

/**
 * @brief Helper function: Wait for ActiveObject task completion and send response
 * 
 * This encapsulates the common pattern:
 * 1. Wait for done flag (set by ActiveObject when operation completes)
 * 2. Send response to client
 * 3. Reset state for next operation
 *
 * @param clientSock Client socket
 * @param future Reference to response string (set by ActiveObject)
 * @param done Reference to completion flag (set by ActiveObject)
 * @param cv Condition variable for waiting
 */
void waitAndSendResponse(int clientSock, string& future, atomic<bool>& done,
                         condition_variable& cv) {
  unique_lock<mutex> guard(futureLock);
  while (!done.load(memory_order_acquire)) {
    cv.wait(guard);
  }
  ServerConnection::sendResponse(clientSock, future, coutLock);
  done.store(false, memory_order_release);
  future.clear();
}

/**
 * @brief Handles commands sent by the client.
 *
 * This function processes various commands related to graph operations and MST
 * calculations. Commands are handled asynchronously using the pipeline of
 * ActiveObjects.
 *
 * @param clientSock The client's socket descriptor.
 * @param pipeline The pipeline of ActiveObjects for task execution.
 * @param g Unique pointer to the graph object.
 * @param factory The MSTFactory object for creating MSTs.
 * @param mst Unique pointer to the MST (Tree) object.
 */
void handleCommands(int clientSock, vector<unique_ptr<ActiveObject>> &pipeline,
                    unique_ptr<Graph> &g, MSTFactory &factory,
                    unique_ptr<Tree> &mst) {
  // Send welcome message when client first connects
  ServerLogger::sendWelcomeMessage(clientSock, [](int sock, const string& msg) {
    ServerConnection::sendResponse(sock, msg, coutLock);
  });
  
  condition_variable cv;
  mutex ssLock;
  atomic<bool> done(false);
  char buffer[BUFFER_SIZE] = {0};

  while (!terminateFlag.load()) {
    memset(buffer, 0, sizeof(buffer));
    int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
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
        ServerLogger::logError("recv() failed for client " + to_string(clientSock), coutLock);
      }
    }
    buffer[bytesReceived] = '\0';
    string command(buffer);
    if (command.empty())
      continue;

    stringstream ss(command);
    string cmd;
    {
      unique_lock<mutex> sguard(ssLock);
      ss >> cmd;
    }

    string future;

    if (cmd == "Newgraph") {

      int n, m;
      vector<tuple<int, int, int>> edges;
      string errorMsg;

      if (!validateAndReadNewgraph(clientSock, ss, ssLock, buffer, bytesReceived,
                                    n, m, edges, errorMsg)) {

        ServerConnection::sendResponse(clientSock, errorMsg, coutLock);
        continue;
      }

      pipeline[0]->enqueue([&g, &mst, edges, n, m, &future, &done, &cv, clientSock]() {

        {
          unique_lock<mutex> graphGuard(graphLock);
          
          resetGraphAndMST(g, mst);

          g = make_unique<Graph>(n, m);

          for (const auto& [u, v, w] : edges) {
            g->addEdge(u, v, w);  // addEdge validates internally
          }

          setResponseAndSignal("\nGraph created with " + to_string(n) + " vertices and " +
                               to_string(m) + " edges.\n", future, done, cv);
        }
        
        ServerLogger::logGraphCreated(clientSock, n, m, coutLock);
      });

    } else if (cmd == "AddEdge") {
      int u = 0, v = 0, w = 0;
      if (!(ss >> u >> v >> w)) {

        ServerConnection::sendResponse(clientSock,
                                       "Invalid ADD_EDGE input. Please provide "
                                       "integers for u, v, and w.\n",
                                       coutLock);
        continue;
      }

      pipeline[0]->enqueue([&g, u, v, w, &future, &done, &cv, clientSock]() {

        bool success;
        {
          unique_lock<mutex> graphGuard(graphLock);

          if (!isGraphValid(g)) {
            setResponseAndSignal("Graph not initialized.\n", future, done, cv);
            return;
          }
          
          success = g->addEdge(u, v, w);
        }
        
        if (!success) {
          setResponseAndSignal("Invalid edge. Vertices should be in the range [1, n] and "
                               "weight should be non-negative, or edge already exists.\n",
                               future, done, cv);
        } else {
          setResponseAndSignal("Edge added between vertices " + to_string(u) + " and " +
                               to_string(v) + " with weight " + to_string(w) + ".\n",
                               future, done, cv);
          // Log after lock is released
          ServerLogger::logEdgeAdded(clientSock, u, v, w, coutLock);
        }
      });
    }

    else if (cmd == "RemoveEdge") {

      int u = 0, v = 0;
      if (!(ss >> u >> v)) {

        ServerConnection::sendResponse(
            clientSock,
            "Invalid REMOVE_EDGE input. Please provide "
            "integers for u and v.\n",
            coutLock);
        continue;
      }

      pipeline[0]->enqueue([&g, u, v, &future, &done, &cv, clientSock]() {

        bool success;
        {
          unique_lock<mutex> graphGuard(graphLock);

          if (!isGraphValid(g)) {
            setResponseAndSignal("Graph not initialized.\n", future, done, cv);
            return;
          }
          
          success = g->removeEdge(u, v);
        }
        
        if (!success) {
          setResponseAndSignal("Edge between vertices " + to_string(u) + " and " +
                               to_string(v) + " does not exist.\n", future, done, cv);
        } else {
          setResponseAndSignal("Edge removed between vertices " + to_string(u) + " and " +
                               to_string(v) + ".\n", future, done, cv);

          // Log after lock is released
          ServerLogger::logEdgeRemoved(clientSock, u, v, coutLock);
        }
      });
    }

    else if (cmd == "Prim" || cmd == "Kruskal") {

      pipeline[1]->enqueue([&g, cmd, &factory, &mst, &future, &done, &cv,
                            &pipeline, clientSock]() {

        unique_lock<mutex> graphGuard(graphLock, try_to_lock);
        if (!graphGuard.owns_lock()) {

          setResponseAndSignal("Graph is being used by another thread. Cannot search for "
                               "MST using " + cmd + ".\n", future, done, cv);
          return;
        }
        
        if (!isGraphValid(g)) {
          setResponseAndSignal("Graph not initialized.\n", future, done, cv);
          return;
        }

        if (mst != nullptr) {
          mst.reset();
          mst = nullptr;
        }

        if (cmd == "Prim") {
          factory.setStrategy(new PrimStrategy);

        } else if (cmd == "Kruskal") {
          factory.setStrategy(new KruskalStrategy);
        } else {
          setResponseAndSignal("Invalid command: " + cmd + "\n", future, done, cv);
          return;
        }
        pipeline[2]->enqueue([&g, &factory, &mst, &future, &done, &cv,
                              &pipeline, cmd, clientSock]() {
          {
            unique_lock<mutex> graphGuard(graphLock);
            mst = factory.createMST(g);
            appendToResponse("MST created using " + cmd + " algorithm.\n", future);
            appendToResponse(mst->printMST(), future);
          }
          // Log after lock is released, at end of task (appears just before SLEEP)
          ServerLogger::logMSTComputed(clientSock, cmd, coutLock);

          pipeline[3]->enqueue([&mst, &future, &done, &cv, &pipeline]() {
            {
              unique_lock<mutex> graphGuard(graphLock);
              appendToResponse("TOTAL WEIGHT OF THE MST IS: ", future);
              appendToResponse(to_string(mst->totalWeight()) + "\n\n", future);
            }
            pipeline[4]->enqueue([&mst, &future, &done, &cv, &pipeline]() {
                {
                  unique_lock<mutex> graphGuard(graphLock);
                  appendToResponse("THE LONGEST PATH (DIAMETER) OF THE MST IS: ", future);
                  appendToResponse(to_string(mst->diameter()) + "\n\n", future);
                  pipeline[5]->enqueue([&mst, &future, &done, &cv,
                                        &pipeline]() {
                    {
                      unique_lock<mutex> graphGuard(graphLock);
                      appendToResponse("AVERAGE DISTANCE OF THE MST IS: ", future);
                      appendToResponse(to_string(mst->averageDistanceEdges()) + "\n\n", future);
                      pipeline[6]->enqueue([&mst, &future, &done, &cv]() {
                        {
                          unique_lock<mutex> graphGuard(graphLock);
                          appendToResponse("SHORTEST PATH IS: ", future);
                          appendToResponse(mst->shortestPath() + "\n", future);
                        }

                        signalCompletion(future, done, cv);
                      });
                    }
                  });
                }
              });
          });
        });
      });
    } else if (cmd == "Exit") {
      ServerConnection::sendResponse(clientSock, "Goodbye\n", coutLock);
      ServerLogger::logDisconnect(clientSock, coutLock);
      
      // Remove client from connected set
      {
        unique_lock<mutex> guard(clientsMutex);
        connectedClients.erase(clientSock);
      }
      
      clientNumber.store(clientNumber.load(memory_order_acquire) - 1,
                         memory_order_release);

      shutdown(clientSock, SHUT_WR);
      break;  // Exit the loop for this client

    } else {

      ServerConnection::sendResponse(
          clientSock, "Invalid command: " + cmd + "\n", coutLock);
      continue;
    }

    waitAndSendResponse(clientSock, future, done, cv);
  }

  // Remove client from connected set before closing
  {
    unique_lock<mutex> guard(clientsMutex);
    connectedClients.erase(clientSock);
  }
  
  shutdown(clientSock, SHUT_RDWR);
  close(clientSock);
}



/**
 * @brief Server main function.
 *
 * This function initializes the server, sets up signal handling,
 * manages connections using a pipeline of ActiveObjects, and
 * handles commands from clients in a multi-threaded environment.
 *
 * @return int Returns 0 on successful execution.
 */
int main() {
  signal(SIGINT, signalHandler);
  vector<unique_ptr<ActiveObject>> pipeline;
  unique_ptr<Graph> g;
  MSTFactory factory;
  unique_ptr<Tree> mst;
  vector<pthread_t> clientThreads;
  unique_ptr<functArgs> faPtr;

  signalHandlerLambda = [&](int signum) {
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
    this_thread::sleep_for(chrono::milliseconds(300));

    ServerLogger::logCleanup("Cancelling and joining " + to_string(clientThreads.size()) + " client thread(s)", coutLock);
    for (auto &thread : clientThreads) {
      pthread_cancel(thread);
      pthread_join(thread, nullptr);
    }
    clientThreads.clear();
    
    ServerLogger::logCleanup("Resetting functArgs", coutLock);
    faPtr.reset();
    
    ServerLogger::logCleanup("Destroying MST strategy", coutLock);
    factory.destroyStrategy();
    
    ServerLogger::logCleanup("Resetting MST", coutLock);
    mst.reset();
    
    ServerLogger::logCleanup("Resetting Graph", coutLock);
    g.reset();
    
    ServerLogger::logCleanup("Resetting " + to_string(pipeline.size()) + " pipeline ActiveObjects", coutLock);
    for (auto &obj : pipeline) {
      obj.reset();
    }
    pipeline.clear();

  };

  // Create server socket using connection utilities
  int serverSock = ServerConnection::createServerSocket(port);
  if (serverSock < 0) {
    ServerLogger::logError("Failed to create server socket", coutLock);
    exit(1);
  }

  // Print server startup banner
  ServerLogger::printPipelineServerBanner(port, serverSock, PIPELINE_SIZE, coutLock);

  // Create pipeline ActiveObjects
  for (int i = 0; i < PIPELINE_SIZE; i++) {
    pipeline.push_back(make_unique<ActiveObject>(i));  // Pass stage ID
    ServerLogger::logThreads("Pipeline ActiveObject #" + to_string(i+1) + " created (stage " + to_string(i) + ")", coutLock);
  }
  
  ServerLogger::logThreads("Pipeline initialized with " + to_string(PIPELINE_SIZE) + " stages", coutLock);

  while (true) {
    int newClientSock =
        ServerConnection::acceptClient(serverSock, clientNumber, coutLock);
    if (newClientSock == -1) {
      continue;
    }

    // Get client IP for logging
    struct sockaddr_in client_addr;
    socklen_t sin_size = sizeof(client_addr);
    getpeername(newClientSock, (struct sockaddr *)&client_addr, &sin_size);
    char s[INET6_ADDRSTRLEN];
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, s, sizeof(s));
    ServerLogger::logConnect(clientNumber.load(), string(s), newClientSock, coutLock);
    ServerLogger::logStatus(clientNumber.load(), coutLock);
    
    // Add client to connected set
    {
      unique_lock<mutex> guard(clientsMutex);
      connectedClients.insert(newClientSock);
    }

    faPtr = unique_ptr<functArgs>(new functArgs{
        newClientSock, ref(pipeline), ref(g), ref(factory), ref(mst)});
    pthread_t tid;
    auto threadFunc = [](void *arg) -> void * {
      functArgs *fa = static_cast<functArgs *>(arg);
      handleCommands(fa->clientSock, fa->pipeline, fa->g, fa->factory, fa->mst);
      return nullptr;
    };
    pthread_create(&tid, nullptr, threadFunc, faPtr.get());
    clientThreads.push_back(tid);
    
    // Log thread creation
    stringstream ss;
    ss << hex << tid;
    ServerLogger::logThreads("Client handler thread created for socket " + to_string(newClientSock) + " (ID: 0x" + ss.str() + ")", coutLock);
  }
  close(serverSock);

  return 0;
}