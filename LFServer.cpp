#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <csignal>
#include <cstring>
#include <thread>
#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "LFThreadPool.hpp"

// Constants
const int port = 4050; ///< Server port number

// Global variables
function<void(int)> signalHandlerLambda; ///< Lambda function for handling signals
atomic<int> clientNumber(0); ///< Tracks the number of connected clients
mutex graphMutex; ///< Mutex for synchronizing access to the graph
mutex &coutLock = LFThreadPool::getOutputMx(); ///< Mutex for synchronizing console output

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
    {
        unique_lock<mutex> guard(coutLock);
        cout << "Interrupt signal (" << signum << ") received." << endl;
    }
    // Free memory and exit
    signalHandlerLambda(signum);
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
        cerr << "send error" << endl;
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
int scanGraph(int clientSock, int &n, int &m, stringstream &ss, unique_ptr<Graph> &g)
{
    if (!(ss >> n >> m) || n <= 0 || m < 0)
    {
        cerr << "Invalid graph input" << endl;
        return -1;
    }

    g = make_unique<Graph>(n, m);
    return 0;
}

/**
 * @brief Scans source and destination vertices from the client's input.
 * 
 * @param ss The stringstream containing the client's input.
 * @param src Source vertex.
 * @param dest Destination vertex.
 * @return int 0 if successful, -1 otherwise.
 */
int scanSrcDest(stringstream &ss, int &src, int &dest)
{
    if (!(ss >> src >> dest) || src < 0 || dest < 0)
    {
        cerr << "Invalid source or destination" << endl;
        return -1;
    }

    return 0;
}

/**
 * @brief Handles commands sent by the client.
 * 
 * This function processes various commands related to graph operations and MST calculations.
 * 
 * @param clientSock The client's socket descriptor.
 * @param g Unique pointer to the graph object.
 * @param factory The MSTFactory object for creating MSTs.
 * @param mst Unique pointer to the MST (Tree) object.
 */
void handleCommands(int clientSock, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst)
{
    char buffer[1024];
    int bytesReceived;

    while (true)
    {
        bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0)
        {
            clientNumber.store(clientNumber.load(memory_order_acquire) - 1, memory_order_release);
            if (bytesReceived == 0)
            {
                {
                    unique_lock<mutex> guard(coutLock);
                    cout << "Connection closed by client." << endl;
                }
                break;
            }
            else
            {
                cerr << "recv error" << endl;
                continue;
            }
        }
        buffer[bytesReceived] = '\0';
        string command(buffer);
        if (command.empty()) continue;

        stringstream ss(command);
        string cmd;
        string response;
        ss >> cmd;

        if (cmd == "Newgraph")
        {
            unique_lock<mutex> guard(graphMutex, try_to_lock);
            if (!guard.owns_lock())
            {
                response = "Graph is being used by another thread can't initialize new graph\n";
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
            int n, m;
            int res = scanGraph(clientSock, n, m, ss, g);
            if (res == -1)
            {
                sendResponse(clientSock, "Invalid graph input. Please enter 2 integers for n and m.\n");
                continue;
            }

            // Wait for the graph to be created
            for (int i = 0; i < m; i++)
            {
                int u = 0, v = 0, w = 0;

                // Attempt to read and parse the edge data
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
                            unique_lock<mutex> guard(coutLock);
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

                if (u < 0 || u > n || v < 0 || v > n || w < 0)
                {
                    sendResponse(clientSock, "Invalid edge values. Vertices should be in the range [1, n] and weight should be non-negative.\n");
                    i--;
                    continue;
                }

                g->addEdge(u, v, w);
            }
            response = "Graph created with " + to_string(n) + " vertices and " + to_string(m) + " edges.\n";
        }
        else if (cmd == "Prim" || cmd == "Kruskal")
{
    unique_lock<mutex> guard(graphMutex, try_to_lock);
    if (!guard.owns_lock())
    {
        response = "Graph is being used by another thread can't search for MST using " + cmd + ".\n";
        sendResponse(clientSock, response);
        continue;
    }

    if (g == nullptr || g->getAdj().empty())
    {
        cerr << "Graph not initialized" << endl;
        continue;
    }

    if (mst != nullptr)
    {
        mst.reset();
        mst = nullptr;
    }

    if (cmd == "Prim")
    {
        factory.setStrategy(new PrimStrategy());
    }
    else
    {
        factory.setStrategy(new KruskalStrategy());
    }

    mst = factory.createMST(g);
    response = "MST created using " + cmd + " algorithm.\n";
    response += mst->printMST();

    // Automatically append results for MSTweight, Longestpath, and Averdist
    response += "Total weight of the MST is: " + to_string(mst->totalWeight()) + "\n";
    response += "The longest path (diameter) of the MST is: " + to_string(mst->diameter()) + '\n';
    response += "Average distance of the MST is: " + to_string(mst->averageDistanceEdges()) + "\n";

    // Example for Shortest path between two vertices (1 and 2 in this example, change as needed)
    int src = 1, dest = 2;  // You can customize this, or get input from client
    response += "Shortest path from " + to_string(src) + " to " + to_string(dest) + " is: " + mst->shortestPath(src, dest) + "\n";
}

        else if (cmd == "Exit")
        {
            sendResponse(clientSock, "Goodbye\n");
            {
                unique_lock<mutex> guard(coutLock);
                cout << "Client: " << clientSock << " disconnected" << endl;
                cout << "Thread: " << this_thread::get_id() << " exiting" << endl;
            }
            clientNumber.store(clientNumber.load(memory_order_acquire) - 1, memory_order_release);
            break;
        }
        else
        {
            response = "Invalid command: " + cmd + "\n";
        }
        // Send the response to the client
        sendResponse(clientSock, response);
    }
    close(clientSock);
}

/**
 * @brief Accepts incoming connections and assigns them to a thread in the pool.
 * 
 * This function handles the process of accepting new client connections, creating a
 * handler for the connection, and assigning the handler to a thread in the thread pool.
 * 
 * @param server_sock The server's socket descriptor.
 * @param g Unique pointer to the graph object.
 * @param factory The MSTFactory object for creating MSTs.
 * @param mst Unique pointer to the MST (Tree) object.
 * @param pool Unique pointer to the thread pool.
 */
void acceptConnection(int server_sock, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst, unique_ptr<LFThreadPool> &pool)
{
    {
        unique_lock<mutex> guard(coutLock);
        cout << "[Server] Accepting connection on thread: " << this_thread::get_id() << endl;
    }

    struct sockaddr_in client_addr;
    socklen_t sin_size = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
    if (client_sock == -1)
    {
        perror("accept");
        return;
    }
    clientNumber.store(clientNumber.load(memory_order_acquire) + 1, memory_order_release);
    char s[INET6_ADDRSTRLEN];
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, s, sizeof s);
    {
        unique_lock<mutex> guard(coutLock);
        cout << "[Server] New connection from " << s << " on socket " << client_sock << endl;
        cout << "[Server] Currently " << clientNumber << " clients connected" << endl;
    }
    function<void()> commandHandler = [client_sock, &g, &factory, &mst]()
    {
        handleCommands(client_sock, g, factory, mst);
    };
    pool->addFd(client_sock, commandHandler);
    {
        unique_lock<mutex> guard(coutLock);
        cout << "[Server] Client: " << client_sock << " was assigned to thread: " << this_thread::get_id() << endl;
    }
}

/**
 * @brief Main function to start the MST server.
 * 
 * This function sets up the server, initializes the reactor and thread pool,
 * and handles incoming connections.
 * 
 * @return int Returns 0 on successful execution.
 */
int main()
{
    signal(SIGINT, signalHandler);
    unique_ptr<Reactor> reactor = make_unique<Reactor>();
    unique_ptr<Graph> g;
    unique_ptr<Tree> t;
    MSTFactory factory;
    unique_ptr<LFThreadPool> pool;
    int serverSock;

    signalHandlerLambda = [&](int signum)
    {
        {
            unique_lock<mutex> guard(coutLock);
            cout << "[Server] Freeing memory" << endl;
        }
        close(serverSock);
        reactor.reset();
        pool.reset();
        factory.destroyStrategy();
        g.reset();
        t.reset();
    };

    struct sockaddr_in serverAddr;
    int opt = 1;

    if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cerr << "[Server] Socket creation error" << endl;
        exit(1);
    }

    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        cerr << "[Server] Setsockopt error" << endl;
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
        cout << "[Server] MST LF server waiting for requests on port " << port << endl;
        cout << "[Server] Server socket: " << serverSock << endl;
    }

    pool = make_unique<LFThreadPool>(10, *reactor);
    reactor->addHandle(serverSock, [serverSock, &g, &factory, &t, &pool]()
                       { acceptConnection(serverSock, g, factory, t, pool); });
    {
        unique_lock<mutex> guard(coutLock);
        cout << "[Server] Server running on thread: " << this_thread::get_id() << endl;
    }
    while (true)
    {
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return 0;
}
