#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <csignal>
#include <cstring>
#include <future>
#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "ActiveObject.hpp"

// Constants
const int port = 4050; ///< Server port number

// Global variables
function<void(int)> signalHandlerLambda; ///< Lambda function for handling signals
atomic<int> clientNumber(0); ///< Tracks the number of connected clients
mutex graphLock; ///< Mutex for synchronizing access to the graph
mutex futureLock; ///< Mutex for synchronizing access to shared futures
mutex &coutLock = ActiveObject::getOutputMutex(); ///< Mutex for synchronizing console output

atomic<bool> terminateFlag(false); ///< Flag to signal the termination of the server

/**
 * @struct functArgs
 * 
 * @brief Holds arguments for thread functions handling client commands.
 * 
 * This struct contains references to the client's socket, the pipeline of ActiveObjects,
 * the graph, the MST factory, and the resulting MST.
 */
struct functArgs
{
    int clientSock; ///< The client's socket descriptor
    vector<unique_ptr<ActiveObject>> &pipeline; ///< Pipeline of ActiveObjects for task execution
    unique_ptr<Graph> &g; ///< Reference to the graph object
    MSTFactory &factory; ///< Reference to the MST factory
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
void signalHandler(int signum)
{
    coutLock.lock();
    cout << "Interrupt signal (" << signum << ") received.\n";
    coutLock.unlock();
    terminateFlag.store(true);
    unique_lock<mutex> guard(graphLock);
    signalHandlerLambda(signum);
    guard.unlock();
    exit(signum);
}

/**
 * @brief Sends a response to the client.
 * 
 * @param clientSock The client's socket descriptor.
 * @param response The response string to be sent.
 */
void sendResponse(int clientSock, const string &response)
{
    if (send(clientSock, response.c_str(), response.size(), 0) < 0)
    {
        unique_lock<mutex> guard(coutLock);
        cerr << "send error" << endl;
    }
}

/**
 * @brief Scans the graph input from the client.
 * 
 * @param n Number of vertices.
 * @param m Number of edges.
 * @param ss The stringstream containing the client's input.
 * @param g Unique pointer to the graph object.
 * @return int 0 if successful, -1 otherwise.
 */
int scanGraph(int &n, int &m, stringstream &ss, unique_ptr<Graph> &g)
{
    if (!(ss >> n >> m) || n <= 0 || m < 0)
    {
        unique_lock<mutex> guard(coutLock);
        cerr << "Invalid graph input" << endl;
        return -1;
    }

    g = make_unique<Graph>(n, m);
    return 0;
}

/**
 * @brief Handles commands sent by the client.
 * 
 * This function processes various commands related to graph operations and MST calculations.
 * Commands are handled asynchronously using the pipeline of ActiveObjects.
 * 
 * @param clientSock The client's socket descriptor.
 * @param pipeline The pipeline of ActiveObjects for task execution.
 * @param g Unique pointer to the graph object.
 * @param factory The MSTFactory object for creating MSTs.
 * @param mst Unique pointer to the MST (Tree) object.
 */
void handleCommands(int clientSock, vector<unique_ptr<ActiveObject>> &pipeline, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst) 
{
    condition_variable cv;
    mutex ssLock;
    atomic<bool> done(false);
    char buffer[1024] = {0};

    while (!terminateFlag.load()) 
    {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) 
        {
            clientNumber.store(clientNumber.load(memory_order_acquire) - 1, memory_order_release);
            if (bytesReceived == 0) 
            {
                unique_lock<mutex> guard(coutLock);
                cout << "Connection closed" << endl;
                break;
            } 
            else 
            {
                cerr << "recv error" << endl;
            }
        }
        buffer[bytesReceived] = '\0';
        string command(buffer);
        if (command.empty()) continue;

        stringstream ss(command);
        string cmd;
        {
            unique_lock<mutex> sguard(ssLock);
            ss >> cmd;
        }

        string future;

        if (cmd == "Newgraph") 
        {
            unique_lock<mutex> graphGuard(graphLock, try_to_lock);

            if (!graphGuard.owns_lock()) 
            {
                sendResponse(clientSock, "Graph is being used by another thread. Cannot initialize new graph.\n");
                continue;
            }
            if (g != nullptr) 
            {
                g.reset();
                g = nullptr;
            }
            if (mst != nullptr) 
            {
                mst.reset();
                mst = nullptr;
            }

            int n, m, res = 0;
            mutex initLock;
            atomic<bool> initDone(false);
            condition_variable initCV;

            pipeline[0]->enqueue([&ss, &n, &m, &g, &ssLock, &initLock, &initDone, &initCV, &res]() 
            {
                unique_lock<mutex> guard2(ssLock);
                unique_lock<mutex> guard(initLock);
                res = scanGraph(n, m, ss, g);     
                initDone.store(true, memory_order_release);
                initCV.notify_one();
            });

            {
                unique_lock<mutex> guard(initLock);
                while (!initDone) {
                    initCV.wait(guard);
                }
            }
            if (res == -1) 
            {
                sendResponse(clientSock, "Invalid graph input. Please enter 2 integers for n and m.\n");
                continue;
            }

            for (int i = 0; i < m; i++) 
            {
                int u = 0, v = 0, w = 0;

                {
                    unique_lock<mutex> guard(ssLock);
                    if (!(ss >> u >> v >> w)) 
                    {
                        ss.clear();
                        ss.str("");
                        memset(buffer, 0, sizeof(buffer));
                        bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);

                        if (bytesReceived <= 0) 
                        {
                            clientNumber.store(clientNumber.load(memory_order_acquire) - 1, memory_order_release);
                            if (bytesReceived == 0) 
                            {
                                unique_lock<mutex> coutGuard(coutLock);
                                cout << "Connection closed by client." << endl;
                                break;
                            } 
                            else 
                            {
                                cerr << "recv error: " << strerror(errno) << endl;
                                break;
                            }
                        }

                        ss.write(buffer, bytesReceived);
                        if (!(ss >> u >> v >> w)) 
                        {
                            sendResponse(clientSock, "Invalid input format. Please enter 3 integers for u, v, and w.\n");
                            i--;
                            continue;
                        }
                    }
                }

                if (u < 0 || u > n || v < 0 || v > n || w < 0 || u == v) 
                {
                    sendResponse(clientSock, "Invalid edge values. Vertices should be in the range [1, n] and weight should be non-negative.\n");
                    i--;
                    continue;
                }

                pipeline[0]->enqueue([&g, u, v, w]() 
                {
                    unique_lock<mutex> guard(graphLock);
                    g->addEdge(u, v, w);
                });
            }

            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "\nGraph created with " + to_string(n) + " vertices and " + to_string(m) + " edges.\n";
                done.store(true, memory_order_release);
                cv.notify_one();
            }
        }
         else if (cmd == "AddEdge") 
        {
            int u = 0, v = 0, w = 0;
            if (!(ss >> u >> v >> w)) 
            {
                sendResponse(clientSock, "Invalid ADD_EDGE input. Please provide integers for u, v, and w.\n");
                continue;
            }

            pipeline[0]->enqueue([&g, u, v, w, &future, &done, &cv]() 
            {
                unique_lock<mutex> graphGuard(graphLock);
                if (g == nullptr || g->getAdj().empty()) 
                {
                    unique_lock<mutex> futureGuard(futureLock);
                    future = "Graph not initialized.\n";
                    done.store(true, memory_order_release);
                    cv.notify_one();
                    return;
                }
                bool success = g->addEdge(u, v, w);
                unique_lock<mutex> futureGuard(futureLock);
                if (!success) 
                {
                    future = "Invalid edge. Vertices should be in the range [1, n] and weight should be non-negative, or edge already exists.\n";
                } 
                else 
                {
                    future = "Edge added between vertices " + to_string(u) + " and " + to_string(v) + " with weight " + to_string(w) + ".\n";
                }
                done.store(true, memory_order_release);
                cv.notify_one();
            });
        }
        // Adding REMOVE_EDGE command handling
        else if (cmd == "RemoveEdge") 
        {
            int u = 0, v = 0;
            if (!(ss >> u >> v)) 
            {
                sendResponse(clientSock, "Invalid REMOVE_EDGE input. Please provide integers for u and v.\n");
                continue;
            }

            pipeline[0]->enqueue([&g, u, v, &future, &done, &cv]() 
            {
                unique_lock<mutex> graphGuard(graphLock);
                if (g == nullptr || g->getAdj().empty()) 
                {
                    unique_lock<mutex> futureGuard(futureLock);
                    future = "Graph not initialized.\n";
                    done.store(true, memory_order_release);
                    cv.notify_one();
                    return;
                }
                bool success = g->removeEdge(u, v);
                unique_lock<mutex> futureGuard(futureLock);
                if (!success) 
                {
                    future = "Edge between vertices " + to_string(u) + " and " + to_string(v) + " does not exist.\n";
                } 
                else 
                {
                    future = "Edge removed between vertices " + to_string(u) + " and " + to_string(v) + ".\n";
                }
                done.store(true, memory_order_release);
                cv.notify_one();
            });
            
        }
         
        else if (cmd == "Prim" || cmd == "Kruskal")
        {
            pipeline[1]->enqueue([&g, cmd, &factory, &mst, &future, &done, &cv, &pipeline]()
            {
                unique_lock<mutex> graphGuard(graphLock, try_to_lock);
            if (!graphGuard.owns_lock()) 
            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "Graph is being used by another thread. Cannot search for MST using " + cmd + ".\n";
                done.store(true, memory_order_release);
                cv.notify_one();
                return;
            }
            if (g == nullptr || g->getAdj().empty()) 
            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "Graph not initialized.\n";
                done.store(true, memory_order_release);
                cv.notify_one();
                return;
            }

            if (mst != nullptr) 
            {
                mst.reset();
                mst = nullptr;
            }

            if (cmd == "Prim") 
            {
                factory.setStrategy(new PrimStrategy);

            }
            else if (cmd == "Kruskal")
            {
                factory.setStrategy(new KruskalStrategy);
            }
            else 
            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "Invalid command: " + cmd + "\n";
                done.store(true, memory_order_release);
                cv.notify_one();
                return;
            }
            pipeline[2]->enqueue([&g, &factory, &mst, &future, &done, &cv, &pipeline, cmd]()
            {
                mst = factory.createMST(g);
                {
                    unique_lock<mutex> graphGuard(graphLock);
                    unique_lock<mutex> futureGuard(futureLock);
                    future += "MST created using " + cmd + " algorithm.\n";
                    future += mst->printMST();
                    
                }
                pipeline[3]->enqueue([&g, &mst, &future, &done, &cv, &pipeline]()
                {
                    {
                        unique_lock<mutex> graphGuard(graphLock);
                        unique_lock<mutex> futureGuard(futureLock);
                        future += "TOTAL WEIGHT OF THE MST IS: ";
                        future += to_string(mst->totalWeight()) + "\n\n";
                        pipeline[4]->enqueue([&g, &mst, &future, &done, &cv, &pipeline]()
                        {
                            {
                                unique_lock<mutex> graphGuard(graphLock);
                                unique_lock<mutex> futureGuard(futureLock);
                                future += "THE LONGEST PATH (DIAMETER) OF THE MST IS: ";
                                future += to_string(mst->diameter()) + "\n\n";
                                pipeline[5]->enqueue([&g, &mst, &future, &done, &cv, &pipeline]()
                                {
                                    {
                                        unique_lock<mutex> graphGuard(graphLock);
                                        unique_lock<mutex> futureGuard(futureLock);
                                        future += "AVERAGE DISTANCE OF THE MST IS: ";
                                        future += to_string(mst->averageDistanceEdges()) + "\n\n";
                                        pipeline[6]->enqueue([&g, &mst, &future, &done, &cv, &pipeline]()
                                        {
                                            {
                                                unique_lock<mutex> graphGuard(graphLock);
                                                unique_lock<mutex> futureGuard(futureLock);

                                                future += "SHORTEST PATH IS: ";
                                                future += mst->shortestPath() + "\n";
                                            }
                                            {
                                                unique_lock<mutex> futureGuard(futureLock);
                                                done.store(true, memory_order_release);
                                                cv.notify_one();
                                            }
                                        });
                                    }
                                });
                            }
                        });
                    }

                });

            });
            });
        }
        else if (cmd == "Exit") 
        {
            sendResponse(clientSock, "Goodbye\n");
            clientNumber.store(clientNumber.load(memory_order_acquire) - 1, memory_order_release);
            break;
        } 
        else 
        {
            sendResponse(clientSock, "Invalid command: " + cmd + "\n");
            continue;
        }

        {
            unique_lock<mutex> guard(futureLock);
            while (!done) {
                cv.wait(guard);
            }
            sendResponse(clientSock, future);
            done.store(false, memory_order_release);
            future.clear();
        }
    }

    close(clientSock);
}

/**
 * @brief Accepts incoming connections and returns the client's socket descriptor.
 * 
 * @param server_sock The server's socket descriptor.
 * @return int The client's socket descriptor, or -1 on error.
 */
int acceptConnection(int server_sock)
{
    struct sockaddr_in client_addr;
    socklen_t sin_size = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
    if (client_sock == -1)
    {
        perror("accept");
        return -1;
    }
    clientNumber.store(clientNumber.load(memory_order_acquire) + 1, memory_order_release);
    char s[INET6_ADDRSTRLEN];
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, s, sizeof s);
    {
        unique_lock<mutex> guard(coutLock);
        cout << "New connection from " << s << " on socket " << client_sock << endl;
        cout << "Currently " << clientNumber.load(memory_order_acquire) << " clients connected" << endl;
    }
    return client_sock;
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
int main()
{
    signal(SIGINT, signalHandler);
    vector<thread> threads;
    vector<unique_ptr<ActiveObject>> pipeline;
    unique_ptr<Graph> g;
    MSTFactory factory;
    unique_ptr<Tree> mst;
    vector<pthread_t> clientThreads;
    unique_ptr<functArgs> faPtr;
    
    signalHandlerLambda = [&](int signum)
    {
        for (auto &thread : clientThreads)
        {
            pthread_cancel(thread);
            pthread_join(thread, nullptr);
        }
        clientThreads.clear();
        clientThreads.shrink_to_fit();
        faPtr.reset();
        factory.destroyStrategy();
        mst.reset();
        g.reset();
        for (auto &obj : pipeline)
        {
            obj.reset();
        }
        pipeline.clear();
        pipeline.shrink_to_fit();
    };

    int serverSock;
    struct sockaddr_in serverAddr;
    int opt = 1;

    if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cerr << "Socket creation error" << endl;
        exit(1);
    }

    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        cerr << "Setsockopt error" << endl;
        exit(1);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    memset(&(serverAddr.sin_zero), '\0', 8);

    if (bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "Bind error" << endl;
        exit(1);
    }

    if (listen(serverSock, 10) < 0)
    {
        cerr << "Listen error" << endl;
        exit(1);
    }

    {
        unique_lock<mutex> guard(coutLock);
        cout << "MST pipeline server waiting for requests on port " << port << endl;
    }

    for (int i = 0; i < 7; i++)
    {
        pipeline.push_back(make_unique<ActiveObject>());
    }

    while (true)
    {
        int newClientSock = acceptConnection(serverSock);
        if (newClientSock == -1)
        {
            continue;
        }

        faPtr = unique_ptr<functArgs>(new functArgs{newClientSock, ref(pipeline), ref(g), ref(factory), ref(mst)});
        pthread_t tid;
        auto threadFunc = [](void *arg) -> void *
        {
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
