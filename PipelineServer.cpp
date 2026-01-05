#include "ActiveObject.hpp"
#include "Graph.hpp"
#include "MSTFactory.hpp"
#include "ServerConnection.hpp"
#include "ServerLogger.hpp"
#include "Tree.hpp"
#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <future>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// Constants
const int port = 4050; ///< Server port number
const int PIPELINE_SIZE = 7; ///< Number of ActiveObject stages in pipeline
const int BUFFER_SIZE = 1024; ///< Buffer size for receiving client data

// Global variables
function<void(int)>
    signalHandlerLambda;     ///< Lambda function for handling signals
atomic<int> clientNumber(0); ///< Tracks the number of connected clients
mutex graphLock;             ///< Mutex for synchronizing access to the graph
mutex futureLock; ///< Mutex for synchronizing access to shared futures
mutex &coutLock =
    ActiveObject::getOutputMutex(); ///< Mutex for synchronizing console output

atomic<bool>
    terminateFlag(false); ///< Flag to signal the termination of the server

/**
 * @struct functArgs
 *
 * @brief Holds arguments for thread functions handling client commands.
 *
 * This struct contains references to the client's socket, the pipeline of
 * ActiveObjects, the graph, the MST factory, and the resulting MST.
 */
struct functArgs {
  int clientSock; ///< The client's socket descriptor
  vector<unique_ptr<ActiveObject>>
      &pipeline;         ///< Pipeline of ActiveObjects for task execution
  unique_ptr<Graph> &g;  ///< Reference to the graph object
  MSTFactory &factory;   ///< Reference to the MST factory
  unique_ptr<Tree> &mst; ///< Reference to the MST (Tree) object
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
          errorMsg = "Invalid input format. Please enter 3 integers for u, v, and w.\n";
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
      // ========== SERVER RESPONSIBILITY: Comprehensive Input Validation ==========
      int n, m;
      vector<tuple<int, int, int>> edges;
      string errorMsg;
      
      // Single function handles all validation: parse, validate constraints, read edges
      if (!validateAndReadNewgraph(clientSock, ss, ssLock, buffer, bytesReceived,
                                    n, m, edges, errorMsg)) {
        // SERVER ERROR: Validation failed (format, constraints, or I/O)
        ServerConnection::sendResponse(clientSock, errorMsg, coutLock);
        continue;
      }

      // ========== SERVER: Enqueue to ActiveObject ==========
      pipeline[0]->enqueue([&g, &mst, edges, n, m, &future, &done, &cv, clientSock]() {
        // ========== ACTIVEOBJECT RESPONSIBILITY: Graph Operations ==========
        unique_lock<mutex> graphGuard(graphLock);
        
        // ACTIVEOBJECT: Reset graph and MST
        resetGraphAndMST(g, mst);

        // ACTIVEOBJECT: Create new graph
        g = make_unique<Graph>(n, m);

        // ACTIVEOBJECT: Add all edges (business logic - edge validation happens in addEdge)
        for (const auto& [u, v, w] : edges) {
          g->addEdge(u, v, w);  // addEdge validates internally
        }

        // ACTIVEOBJECT: Set response when all edges are added
        setResponseAndSignal("\nGraph created with " + to_string(n) + " vertices and " +
                             to_string(m) + " edges.\n", future, done, cv);
        
        // Log graph creation (outside lock to avoid deadlock)
        ServerLogger::logGraphCreated(clientSock, n, m, coutLock);
      });

    } else if (cmd == "AddEdge") {
      // ========== SERVER RESPONSIBILITY: Input Parsing & Format Validation ==========
      int u = 0, v = 0, w = 0;
      if (!(ss >> u >> v >> w)) {
        // SERVER ERROR: Invalid input format
        ServerConnection::sendResponse(clientSock,
                                       "Invalid ADD_EDGE input. Please provide "
                                       "integers for u, v, and w.\n",
                                       coutLock);
        continue;
      }

      // ========== SERVER: Enqueue to ActiveObject ==========
      pipeline[0]->enqueue([&g, u, v, w, &future, &done, &cv, clientSock]() {
        // ========== ACTIVEOBJECT RESPONSIBILITY: Graph Operations & Errors ==========
        unique_lock<mutex> graphGuard(graphLock);
        
        // ACTIVEOBJECT: Validate graph state
        if (!isGraphValid(g)) {
          setResponseAndSignal("Graph not initialized.\n", future, done, cv);
          return;
        }
        
        bool success = g->addEdge(u, v, w);
        
        if (!success) {
          setResponseAndSignal("Invalid edge. Vertices should be in the range [1, n] and "
                               "weight should be non-negative, or edge already exists.\n",
                               future, done, cv);
        } else {
          setResponseAndSignal("Edge added between vertices " + to_string(u) + " and " +
                               to_string(v) + " with weight " + to_string(w) + ".\n",
                               future, done, cv);
          // Log edge addition (outside lock to avoid deadlock)
          ServerLogger::logEdgeAdded(clientSock, u, v, w, coutLock);
        }
      });
    }
    // Adding REMOVE_EDGE command handling
    else if (cmd == "RemoveEdge") {
      // ========== SERVER RESPONSIBILITY: Input Parsing & Format Validation ==========
      int u = 0, v = 0;
      if (!(ss >> u >> v)) {
        // SERVER ERROR: Invalid input format
        ServerConnection::sendResponse(
            clientSock,
            "Invalid REMOVE_EDGE input. Please provide "
            "integers for u and v.\n",
            coutLock);
        continue;
      }

      // ========== SERVER: Enqueue to ActiveObject ==========
      pipeline[0]->enqueue([&g, u, v, &future, &done, &cv, clientSock]() {
        // ========== ACTIVEOBJECT RESPONSIBILITY: Graph Operations & Errors ==========
        unique_lock<mutex> graphGuard(graphLock);
        
        // ACTIVEOBJECT: Validate graph state
        if (!isGraphValid(g)) {
          setResponseAndSignal("Graph not initialized.\n", future, done, cv);
          return;
        }
        
        // ACTIVEOBJECT: Perform operation
        bool success = g->removeEdge(u, v);
        
        // ACTIVEOBJECT: Handle business logic errors
        if (!success) {
          setResponseAndSignal("Edge between vertices " + to_string(u) + " and " +
                               to_string(v) + " does not exist.\n", future, done, cv);
        } else {
          setResponseAndSignal("Edge removed between vertices " + to_string(u) + " and " +
                               to_string(v) + ".\n", future, done, cv);
          // Log edge removal (outside lock to avoid deadlock)
          ServerLogger::logEdgeRemoved(clientSock, u, v, coutLock);
        }
      });
    }

    else if (cmd == "Prim" || cmd == "Kruskal") {
      // ========== SERVER: Enqueue to ActiveObject (no input parsing needed) ==========
      pipeline[1]->enqueue([&g, cmd, &factory, &mst, &future, &done, &cv,
                            &pipeline, clientSock]() {
        // ========== ACTIVEOBJECT RESPONSIBILITY: Graph Operations & Errors ==========
        // ACTIVEOBJECT: Try to acquire lock (non-blocking for this operation)
        unique_lock<mutex> graphGuard(graphLock, try_to_lock);
        if (!graphGuard.owns_lock()) {
          // ACTIVEOBJECT ERROR: Graph is busy (concurrency issue)
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
          unique_lock<mutex> graphGuard(graphLock);
          mst = factory.createMST(g);
          appendToResponse("MST created using " + cmd + " algorithm.\n", future);
          appendToResponse(mst->printMST(), future);
          // Log MST computation (outside lock to avoid deadlock)
          ServerLogger::logMSTComputed(clientSock, cmd, coutLock);
          pipeline[3]->enqueue([&mst, &future, &done, &cv, &pipeline]() {
            {
              unique_lock<mutex> graphGuard(graphLock);
              appendToResponse("TOTAL WEIGHT OF THE MST IS: ", future);
              appendToResponse(to_string(mst->totalWeight()) + "\n\n", future);
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
                        // ACTIVEOBJECT: Signal completion (response already built incrementally)
                        signalCompletion(future, done, cv);
                      });
                    }
                  });
                }
              });
            }
          });
        });
      });
    } else if (cmd == "Exit") {
      // Immediate response (no async operation)
      ServerConnection::sendResponse(clientSock, "Goodbye\n", coutLock);
      ServerLogger::logDisconnect(clientSock, coutLock);
      clientNumber.store(clientNumber.load(memory_order_acquire) - 1,
                         memory_order_release);
      

      shutdown(clientSock, SHUT_WR);

      close(clientSock);
      return;  // Exit function immediately (socket already closed)
    } else {
      // Immediate response (no async operation)
      ServerConnection::sendResponse(
          clientSock, "Invalid command: " + cmd + "\n", coutLock);
      continue;
    }

    // ========== SERVER: Wait for async operation completion and send response ==========
    // Only reaches here if command enqueued an async operation
    // ActiveObject sets future and done when operation completes
    waitAndSendResponse(clientSock, future, done, cv);
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
  vector<thread> threads;
  vector<unique_ptr<ActiveObject>> pipeline;
  unique_ptr<Graph> g;
  MSTFactory factory;
  unique_ptr<Tree> mst;
  vector<pthread_t> clientThreads;
  unique_ptr<functArgs> faPtr;

  signalHandlerLambda = [&](int signum) {
    for (auto &thread : clientThreads) {
      pthread_cancel(thread);
      pthread_join(thread, nullptr);
    }
    clientThreads.clear();
    clientThreads.shrink_to_fit();
    faPtr.reset();
    factory.destroyStrategy();
    mst.reset();
    g.reset();
    for (auto &obj : pipeline) {
      obj.reset();
    }
    pipeline.clear();
    pipeline.shrink_to_fit();
  };

  // Create server socket using connection utilities
  int serverSock = ServerConnection::createServerSocket(port);
  if (serverSock < 0) {
    ServerLogger::logError("Failed to create server socket", coutLock);
    exit(1);
  }

  // Print server startup banner
  {
    unique_lock<mutex> guard(coutLock);
    cout << "\n" << ServerLogger::BOLD << ServerLogger::BLUE
         << "╔═══════════════════════════════════════════════════════╗" << ServerLogger::RESET
         << endl;
    cout << ServerLogger::BOLD << ServerLogger::BLUE << "║" << ServerLogger::RESET << "  " 
         << ServerLogger::GREEN << ServerLogger::BOLD
         << "🚀 Graph Computation Server (Pipeline/Active Object)" << ServerLogger::RESET << "  "
         << ServerLogger::BOLD << ServerLogger::BLUE << "║" << ServerLogger::RESET << endl;
    cout << ServerLogger::BOLD << ServerLogger::BLUE
         << "╠═══════════════════════════════════════════════════════╣" << ServerLogger::RESET
         << endl;
    cout << ServerLogger::BOLD << ServerLogger::BLUE << "║" << ServerLogger::RESET << "  " 
         << ServerLogger::CYAN << "📍 Port: " << ServerLogger::BOLD
         << port << ServerLogger::RESET << "                                    " 
         << ServerLogger::BOLD << ServerLogger::BLUE << "║" << ServerLogger::RESET << endl;
    cout << ServerLogger::BOLD << ServerLogger::BLUE << "║" << ServerLogger::RESET << "  " 
         << ServerLogger::CYAN
         << "🔌 Socket: " << serverSock << ServerLogger::RESET
         << "                                  " << ServerLogger::BOLD << ServerLogger::BLUE 
         << "║" << ServerLogger::RESET << endl;
    cout << ServerLogger::BOLD << ServerLogger::BLUE << "║" << ServerLogger::RESET << "  " 
         << ServerLogger::MAGENTA
         << "⚙️  Pipeline: " << PIPELINE_SIZE << " ActiveObject stages" << ServerLogger::RESET 
         << "              " << ServerLogger::BOLD << ServerLogger::BLUE << "║" << ServerLogger::RESET << endl;
    cout << ServerLogger::BOLD << ServerLogger::BLUE << "║" << ServerLogger::RESET << "  " 
         << ServerLogger::YELLOW
         << "⏳ Waiting for connections..." << ServerLogger::RESET << "                    "
         << ServerLogger::BOLD << ServerLogger::BLUE << "║" << ServerLogger::RESET << endl;
    cout << ServerLogger::BOLD << ServerLogger::BLUE
         << "╚═══════════════════════════════════════════════════════╝" << ServerLogger::RESET
         << "\n" << endl;
  }

  for (int i = 0; i < PIPELINE_SIZE; i++) {
    pipeline.push_back(make_unique<ActiveObject>());
  }

  while (true) {
    int newClientSock =
        ServerConnection::acceptClient(serverSock, clientNumber, coutLock);
    if (newClientSock == -1) {
      continue;
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
  }
  close(serverSock);

  return 0;
}