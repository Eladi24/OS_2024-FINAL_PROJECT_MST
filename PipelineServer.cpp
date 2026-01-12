#include "ActiveObject.hpp"
#include "Graph.hpp"
#include "MSTCache.hpp"
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
#include <map>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

// Constants
const int port = 4050; 
const int PIPELINE_SIZE = 7;
const int BUFFER_SIZE = 1024;
const int MAX_CLIENTS = 10; 

// Global variables
function<void(int)>
    signalHandlerLambda;     
atomic<int> clientNumber(0); 
mutex graphLock;        
mutex futureLock; 
atomic<bool>terminateFlag(false);
map<int, pthread_t> clientThreadsMap;  // Map socket -> thread ID
mutex threadsMapMutex; 
mutex &coutLock =ActiveObject::getOutputMutex();

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
 * 2. Check if client is still connected before sending response
 * 3. Send response to client if still connected
 * 4. Reset state for next operation
 *
 * @param clientSock Client socket
 * @param future Reference to response string (set by ActiveObject)
 * @param done Reference to completion flag (set by ActiveObject)
 * @param cv Condition variable for waiting
 * @return true if response was sent, false if client disconnected
 */
/**
 * @brief Waits for pipeline operation to complete, then sends response ONCE
 * 

 */
bool waitAndSendResponse(int clientSock, string& future, atomic<bool>& done,
                         condition_variable& cv) {
  // Acquire lock to wait for pipeline completion
  unique_lock<mutex> guard(futureLock);
  
  // Wait with predicate: exit when done is set OR terminateFlag is set (shutdown)
  cv.wait(guard, [&done]() {
    return done.load(memory_order_acquire) || terminateFlag.load(memory_order_acquire);
  });
  
  // Check if shutdown was requested
  if (terminateFlag.load(memory_order_acquire)) {
    done.store(false, memory_order_release);
    future.clear();
    return false;
  }
  
  {
    unique_lock<mutex> clientGuard(threadsMapMutex);
    if (clientThreadsMap.find(clientSock) == clientThreadsMap.end()) {
      ServerLogger::logInfo("Client " + to_string(clientSock) + 
                            " disconnected during operation. Response discarded.", coutLock);
      done.store(false, memory_order_release);
      future.clear();
      return false;
    }
  }
  
  // Send the response ONCE to client (client still connected)
  ServerConnection::sendResponse(clientSock, future, coutLock);
  
  // Reset state for next operation
  done.store(false, memory_order_release);
  future.clear();
  return true;
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
  ServerLogger::sendWelcomeMessage(clientSock, [](int sock, const string& msg) {
    ServerConnection::sendResponse(sock, msg, coutLock);
  });
  
  condition_variable cv;
  mutex ssLock;
  atomic<bool> done(false);
  char buffer[BUFFER_SIZE] = {0};

  while (!terminateFlag.load(memory_order_acquire)) {

    memset(buffer, 0, sizeof(buffer));
    int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
    if (bytesReceived <= 0) {
      bool isShutdown = terminateFlag.load(memory_order_acquire);
      int remainingSlots;
      clientNumber.store(clientNumber.load(memory_order_acquire) - 1,
                         memory_order_release);
      
      {
        unique_lock<mutex> guard(threadsMapMutex);
        clientThreadsMap.erase(clientSock);
        remainingSlots = MAX_CLIENTS - clientThreadsMap.size();
      }
      
      // Only log disconnect messages if not shutting down (quiet shutdown)
      if (!isShutdown) {
        if (bytesReceived == 0) {
          ServerLogger::logDisconnect(clientSock, coutLock);
          ServerLogger::logStatus(clientNumber.load(), coutLock);
          ServerLogger::logInfo("Slot available. " + to_string(remainingSlots) + " connection slot(s) remaining.", coutLock);
        } else {
          ServerLogger::logError("recv() failed for client " + to_string(clientSock), coutLock);
          ServerLogger::logInfo("Slot available. " + to_string(remainingSlots) + " connection slot(s) remaining.", coutLock);
        }
      }
      break;
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
      pipeline[0]->enqueue([&g, &mst, command, &future, &done, &cv, &pipeline, clientSock]() {
        stringstream ss(command);
        string cmd;
        ss >> cmd; // 
        
        int n, m;
        if (!(ss >> n >> m)) {
          setResponseAndSignal("Invalid graph input. Please enter 2 integers for n and m.\n",
                              future, done, cv);
          return;
        }
        
        // Validate n > 0, m >= 0
        if (n <= 0 || m < 0) {
          setResponseAndSignal("Invalid graph input. n must be > 0 and m must be >= 0.\n",
                              future, done, cv);
          return;
        }
        
        // Validate maxEdges constraint
        int maxEdges = (n * (n - 1)) / 2;
        if (m > maxEdges) {
          setResponseAndSignal("Invalid graph parameters: A graph with " + to_string(n) +
                              " vertices can have at most " + to_string(maxEdges) +
                              " edges, but " + to_string(m) + " edges were requested.\n",
                              future, done, cv);
          return;
        }
        
        // Read and validate all m edges
        vector<tuple<int, int, int>> edges;
        char buffer[BUFFER_SIZE];
        string edgeData = "";
        
        string temp;
        while (ss >> temp) {
          if (!edgeData.empty()) edgeData += " ";
          edgeData += temp;
        }
        
        for (int i = 0; i < m; i++) {
          int u, v, w;
          
          // Try to read edge from current data
          stringstream edgeStream(edgeData);
          if (!(edgeStream >> u >> v >> w)) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytesReceived = recv(clientSock, buffer, BUFFER_SIZE, 0);
            if (bytesReceived <= 0) {
              setResponseAndSignal("Invalid input format. Expected " + to_string(m) + 
                                 " edges (format: u v w), but connection closed at edge " + 
                                 to_string(i + 1) + ". Newgraph command aborted. Please start with a new command.\n",
                                 future, done, cv);
              return;
            }
            buffer[bytesReceived] = '\0';
            
            if (!edgeData.empty()) edgeData += " ";
            edgeData += string(buffer);
            
            // Try parsing again with updated data
            edgeStream.clear();
            edgeStream.str(edgeData);
            if (!(edgeStream >> u >> v >> w)) {
              setResponseAndSignal("Invalid input format. Expected " + to_string(m) + 
                                 " edges (format: u v w), but received invalid data at edge " + 
                                 to_string(i + 1) + ". Newgraph command aborted. Please start with a new command.\n",
                                 future, done, cv);
              return;
            }
          }
          
          // Remove parsed edge from edgeData - get remaining tokens after u, v, w
          edgeData = "";
          string remaining;
          while (edgeStream >> remaining) {
            if (!edgeData.empty()) edgeData += " ";
            edgeData += remaining;
          }
          
          // Validate edge values
          if (u < 1 || u > n || v < 1 || v > n || w < 0 || u == v) {
            setResponseAndSignal("Invalid edge values. Vertices should be in the range [1, n] "
                               "and weight should be non-negative.\n",
                               future, done, cv);
            return;
          }
          
          edges.push_back({u, v, w});
        }
        
        // Validation passed - pass to stage 1 to create graph
        pipeline[1]->enqueue([&g, &mst, n, m, edges, &future, &done, &cv, clientSock]() {
          {
            unique_lock<mutex> graphGuard(graphLock);
            resetGraphAndMST(g, mst);
            g = make_unique<Graph>(n, m);
            for (const auto& [u, v, w] : edges) {
              g->addEdge(u, v, w);
            }
          }
          MSTCacheManager::incrementGraphVersion();
          setResponseAndSignal("\nGraph created with " + to_string(n) + " vertices and " +
                              to_string(m) + " edges.\n", future, done, cv);
          ServerLogger::logGraphCreated(clientSock, n, m, coutLock);
        });
      });

    } else if (cmd == "AddEdge") {
      // Pass command string to stage 0 for validation
      pipeline[0]->enqueue([&g, command, &future, &done, &cv, &pipeline, clientSock]() {
        stringstream ss(command);
        string cmd;
        ss >> cmd; // skip command name
        
        int u = 0, v = 0, w = 0;
        if (!(ss >> u >> v >> w)) {
          setResponseAndSignal("Invalid ADD_EDGE input. Please provide "
                              "integers for u, v, and w.\n",
                              future, done, cv);
          return;
        }
        
        // Validate graph exists
        {
          unique_lock<mutex> graphGuard(graphLock);
          if (!isGraphValid(g)) {
            setResponseAndSignal("Graph not initialized.\n", future, done, cv);
            return;
          }
        }
        
        // Validation passed - pass to stage 1 to add edge
        pipeline[1]->enqueue([&g, u, v, w, &future, &done, &cv, clientSock]() {
          bool success;
          {
            unique_lock<mutex> graphGuard(graphLock);
            success = g->addEdge(u, v, w);
          }
    
          if (success) {
            MSTCacheManager::incrementGraphVersion();
          }
          
          if (!success) {
            setResponseAndSignal("Invalid edge. Vertices should be in the range [1, n] and "
                                "weight should be non-negative, or edge already exists.\n",
                                future, done, cv);
          } else {
            setResponseAndSignal("Edge added between vertices " + to_string(u) + " and " +
                                to_string(v) + " with weight " + to_string(w) + ".\n",
                                future, done, cv);
            ServerLogger::logEdgeAdded(clientSock, u, v, w, coutLock);
          }
        });
      });

    } else if (cmd == "RemoveEdge") {
      pipeline[0]->enqueue([&g, command, &future, &done, &cv, &pipeline, clientSock]() {
        stringstream ss(command);
        string cmd;
        ss >> cmd; 
        
        int u = 0, v = 0;
        if (!(ss >> u >> v)) {
          setResponseAndSignal("Invalid REMOVE_EDGE input. Please provide "
                              "integers for u and v.\n",
                              future, done, cv);
          return;
        }
        
        // Validate graph exists
        {
          unique_lock<mutex> graphGuard(graphLock);
          if (!isGraphValid(g)) {
            setResponseAndSignal("Graph not initialized.\n", future, done, cv);
            return;
          }
        }
        
        pipeline[1]->enqueue([&g, u, v, &future, &done, &cv, clientSock]() {
          bool success;
          {
            unique_lock<mutex> graphGuard(graphLock);
            success = g->removeEdge(u, v);
          }
          if (success) {
            MSTCacheManager::incrementGraphVersion();
          }
          
          if (!success) {
            setResponseAndSignal("Edge between vertices " + to_string(u) + " and " +
                                to_string(v) + " does not exist.\n", future, done, cv);
          } else {
            setResponseAndSignal("Edge removed between vertices " + to_string(u) + " and " +
                                to_string(v) + ".\n", future, done, cv);
            ServerLogger::logEdgeRemoved(clientSock, u, v, coutLock);
          }
        });
      });

    } else if (cmd == "Prim" || cmd == "Kruskal") {
      pipeline[0]->enqueue([&g, cmd, &factory, &mst, &future, &done, &cv,
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
        
        pipeline[1]->enqueue([&g, cmd, &factory, &mst, &future, &done, &cv,
                              &pipeline, clientSock]() {
          // Check cache first - only set factory if cache is invalid
          MSTCache* selectedCache = MSTCacheManager::getCache(cmd);
          bool useCache = false;
          unsigned long long currentVersion = MSTCacheManager::graphVersion.load(memory_order_acquire);
          
          if (selectedCache != nullptr) {
            unique_lock<mutex> cacheGuard(MSTCacheManager::cacheLock);
            if (MSTCacheManager::isCacheValid(selectedCache, currentVersion)) {
              useCache = true;
              mst = make_unique<Tree>(*selectedCache->cachedMST);
            }
          }
          
          // Only reset MST and set factory strategy if we need to compute new MST
          if (!useCache) {
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
          }
          
          pipeline[2]->enqueue([&g, &factory, &mst, &future, &done, &cv,
                                &pipeline, cmd, clientSock, useCache, selectedCache, currentVersion]() {
            if (useCache) {
              appendToResponse("MST created using " + cmd + " algorithm. (📦 Using cached data - no computation needed)\n", future);
              appendToResponse(selectedCache->cachedMSTString, future);
              ServerLogger::logInfo("Client " + to_string(clientSock) + " using cached MST for " + cmd + " algorithm", coutLock);
            } else {
              {
                unique_lock<mutex> graphGuard(graphLock);
                mst = factory.createMST(g);
                appendToResponse("MST created using " + cmd + " algorithm.\n", future);
                string mstString = mst->printMST();
                appendToResponse(mstString, future);
                

                if (selectedCache != nullptr) {
                  unique_lock<mutex> cacheGuard(MSTCacheManager::cacheLock);
                  selectedCache->cachedMST = make_unique<Tree>(*mst);
                  selectedCache->cachedMSTString = mstString;
                  selectedCache->lastComputedVersion.store(currentVersion, memory_order_release);
                  selectedCache->cachedTotalWeight = -1;
                  selectedCache->cachedDiameter = -1;
                  selectedCache->cachedAverageDistance = -1.0;
                  selectedCache->cachedShortestPath.clear();
                }
              }
              ServerLogger::logMSTComputed(clientSock, cmd, coutLock);
            }

            pipeline[3]->enqueue([&mst, useCache, selectedCache, &future, &done, &cv, &pipeline]() {
              if (useCache) {
                appendToResponse("TOTAL WEIGHT OF THE MST IS: ", future);
                appendToResponse(to_string(selectedCache->cachedTotalWeight) + "\n\n", future);
              } else {

                {
                  unique_lock<mutex> graphGuard(graphLock);
                  long long weight = mst->totalWeight();
                  appendToResponse("TOTAL WEIGHT OF THE MST IS: ", future);
                  appendToResponse(to_string(weight) + "\n\n", future);
          
                  if (selectedCache != nullptr) {
                    unique_lock<mutex> cacheGuard(MSTCacheManager::cacheLock);
                    selectedCache->cachedTotalWeight = weight;
                  }
                }
              }
             
              pipeline[4]->enqueue([&mst, useCache, selectedCache, &future, &done, &cv, &pipeline]() {
                if (useCache) {
                  // Use cached value
                  appendToResponse("THE LONGEST PATH (DIAMETER) OF THE MST IS: ", future);
                  appendToResponse(to_string(selectedCache->cachedDiameter) + "\n\n", future);
                } else {
                  
                  {
                    unique_lock<mutex> graphGuard(graphLock);
                    long long diameter = mst->diameter();
                    appendToResponse("THE LONGEST PATH (DIAMETER) OF THE MST IS: ", future);
                    appendToResponse(to_string(diameter) + "\n\n", future);
                    // Update cache
                    {
                      unique_lock<mutex> cacheGuard(MSTCacheManager::cacheLock);
                      selectedCache->cachedDiameter = diameter;
                    }
                  }
                }
                
                pipeline[5]->enqueue([&mst, useCache, selectedCache, &future, &done, &cv, &pipeline]() {
                  if (useCache) {
                    
                    appendToResponse("AVERAGE DISTANCE OF THE MST IS: ", future);
                    appendToResponse(to_string(selectedCache->cachedAverageDistance) + "\n\n", future);
                  } else {
                  
                    {
                      unique_lock<mutex> graphGuard(graphLock);
                      double avgDist = mst->averageDistanceEdges();
                      appendToResponse("AVERAGE DISTANCE OF THE MST IS: ", future);
                      appendToResponse(to_string(avgDist) + "\n\n", future);
                      
                      {
                        unique_lock<mutex> cacheGuard(MSTCacheManager::cacheLock);
                        selectedCache->cachedAverageDistance = avgDist;
                      }
                    }
                  }
                  
                  pipeline[6]->enqueue([&mst, useCache, selectedCache, &future, &done, &cv]() {
                    if (useCache) {
                      
                      appendToResponse("SHORTEST PATH IS: ", future);
                      appendToResponse(selectedCache->cachedShortestPath, future);
                    } else {
                      
                      {
                        unique_lock<mutex> graphGuard(graphLock);
                        string shortestPath = mst->shortestPath();
                        appendToResponse("SHORTEST PATH IS: ", future);
                        appendToResponse(shortestPath + "\n", future);
                      
                        {
                          unique_lock<mutex> cacheGuard(MSTCacheManager::cacheLock);
                          selectedCache->cachedShortestPath = shortestPath;
                        }
                      }
                    }
                    signalCompletion(future, done, cv);
                  });
                });
              });
            });
          });
        }); 
      });  
    } else if (cmd == "Exit") {
      bool isShutdown = terminateFlag.load(memory_order_acquire);
      ServerConnection::sendResponse(clientSock, "Goodbye\n", coutLock);
      
      if (!isShutdown) {
        ServerLogger::logDisconnect(clientSock, coutLock);
      }
      
      int remainingSlots;
      {
        unique_lock<mutex> guard(threadsMapMutex);
        clientThreadsMap.erase(clientSock);
        remainingSlots = MAX_CLIENTS - clientThreadsMap.size();
      }
      
      clientNumber.store(clientNumber.load(memory_order_acquire) - 1,
                         memory_order_release);
      
      if (!isShutdown) {
        ServerLogger::logStatus(clientNumber.load(), coutLock);
        ServerLogger::logInfo("Slot available. " + to_string(remainingSlots) + " connection slot(s) remaining.", coutLock);
      }

      shutdown(clientSock, SHUT_WR);
      break; 

    } else {

      ServerConnection::sendResponse(
          clientSock, "Invalid command: " + cmd + "\n", coutLock);
      continue;
    }

    if (!waitAndSendResponse(clientSock, future, done, cv)) {

      break;
    }
  }

  bool isShutdown = terminateFlag.load(memory_order_acquire);
  int remainingSlots;
  {
    unique_lock<mutex> guard(threadsMapMutex);
    clientThreadsMap.erase(clientSock);
    remainingSlots = MAX_CLIENTS - clientThreadsMap.size();
  }
  
  if (!isShutdown) {
    ServerLogger::logInfo("Slot available. " + to_string(remainingSlots) + " connection slot(s) remaining.", coutLock);
  }
  shutdown(clientSock, SHUT_RDWR);
  close(clientSock);
  
  // Thread will exit naturally here - it's responsible for its own cleanup
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
  unique_ptr<functArgs> faPtr;
  int serverSock = -1;  // Declare before lambda so it can be captured

  signalHandlerLambda = [&](int signum) {
    
    string shutdownMsg =
        "\n🛑 [SERVER] Server is shutting down. Connection will be closed.\n";
    // Get a copy of client threads and close sockets to wake up blocked threads
    map<int, pthread_t> threadsToJoin;
    {
      unique_lock<mutex> guard(threadsMapMutex);
      ServerLogger::logCleanup("Closing " + to_string(clientThreadsMap.size()) + " client connection(s)", coutLock);
      threadsToJoin = clientThreadsMap;  // Copy the map
      for (auto& [clientSock, tid] : clientThreadsMap) {
        if (clientSock >= 0) {
          send(clientSock, shutdownMsg.c_str(), shutdownMsg.size(), 0);
          shutdown(clientSock, SHUT_RDWR);  // Shutdown socket to wake up recv()
          close(clientSock);
          ServerLogger::logClientClosed(clientSock, coutLock);
        }
      }
      clientNumber.store(0, memory_order_release);
    }

    // Stop pipeline ActiveObjects
    ServerLogger::logCleanup("Stopping " + to_string(pipeline.size()) + " pipeline ActiveObjects", coutLock);
    for (auto &obj : pipeline) {
      obj.reset();
    }
    pipeline.clear();
    ServerLogger::logCleanup("Pipeline ActiveObjects stopped", coutLock);
    
    // Wait for client threads to finish
    ServerLogger::logCleanup("Waiting for " + to_string(threadsToJoin.size()) + " client thread(s) to finish...", coutLock);
    for (auto& [clientSock, tid] : threadsToJoin) {
      pthread_join(tid, nullptr);
    }
    
    {
      unique_lock<mutex> guard(threadsMapMutex);
      clientThreadsMap.clear();
    }
    ServerLogger::logCleanup("All client threads have been joined", coutLock);
    
    ServerLogger::logCleanup("Resetting functArgs", coutLock);
    faPtr.reset();
    
    ServerLogger::logCleanup("Destroying MST strategy", coutLock);
    factory.destroyStrategy();
    
    ServerLogger::logCleanup("Resetting MST", coutLock);
    mst.reset();
    
    ServerLogger::logCleanup("Resetting Graph", coutLock);
    g.reset();
    
    // Close server socket
    if (serverSock >= 0) {
      ServerLogger::logCleanup("Closing server socket", coutLock);
      close(serverSock);
      serverSock = -1;
    }

  };

  serverSock = ServerConnection::createServerSocket(port);
  if (serverSock < 0) {
    ServerLogger::logError("Failed to create server socket", coutLock);
    exit(1);
  }

  ServerLogger::printPipelineServerBanner(port, serverSock, PIPELINE_SIZE, coutLock);

  for (int i = 0; i < PIPELINE_SIZE; i++) {
    pipeline.push_back(make_unique<ActiveObject>(i));  
    ServerLogger::logThreads("Pipeline ActiveObject #" + to_string(i+1) + " created (stage " + to_string(i) + ")", coutLock);
  }
  

  while (!terminateFlag.load()) {
    // Accept client connection
    struct sockaddr_in client_addr;
    socklen_t sin_size = sizeof(client_addr);
    int newClientSock = accept(serverSock, (struct sockaddr *)&client_addr, &sin_size);
    if (newClientSock == -1) {
      perror("accept");
      continue;
    }

    char s[INET6_ADDRSTRLEN];
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, s, sizeof(s));

    {
      unique_lock<mutex> guard(threadsMapMutex);
      if (clientThreadsMap.size() >= MAX_CLIENTS) {
        string rejectMsg = "Connection rejected: Server has reached maximum capacity (" + 
                          to_string(MAX_CLIENTS) + " clients). Please try again later.\n";
        ServerConnection::sendResponse(newClientSock, rejectMsg, coutLock);
        
        ServerLogger::logError("Connection from " + string(s) + " rejected: Maximum client limit (" + 
                               to_string(MAX_CLIENTS) + ") reached", coutLock);
        
        shutdown(newClientSock, SHUT_RDWR);
        close(newClientSock);
        continue;
      }
      
      clientNumber.store(clientNumber.load(memory_order_acquire) + 1,
                         memory_order_release);
    }

    ServerLogger::logConnect(clientNumber.load(), string(s), newClientSock, coutLock);
    ServerLogger::logStatus(clientNumber.load(), coutLock);

    faPtr = unique_ptr<functArgs>(new functArgs{
        newClientSock, ref(pipeline), ref(g), ref(factory), ref(mst)});
    pthread_t tid;
    auto threadFunc = [](void *arg) -> void * {
      functArgs *fa = static_cast<functArgs *>(arg);
      handleCommands(fa->clientSock, fa->pipeline, fa->g, fa->factory, fa->mst);
      return nullptr;
    };
    pthread_create(&tid, nullptr, threadFunc, faPtr.get());
    
    {
      unique_lock<mutex> guard(threadsMapMutex);
      clientThreadsMap[newClientSock] = tid;
    }
    
    stringstream ss;
    ss << hex << tid;
    ServerLogger::logThreads("Client handler thread created for socket " + to_string(newClientSock) + " (ID: 0x" + ss.str() + ")", coutLock);
  }
  close(serverSock);

  return 0;
}