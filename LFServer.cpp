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
#include "MSTFactory.hpp"
#include "LFThreadPool.hpp"

const int port = 4050;
function<void(int)> signalHandlerLambda;
atomic<int> clientNumber(0);
mutex graphMutex;
mutex &coutLock = LFThreadPool::getOutputMx();

void signalHandler(int signum)
{
    {
        unique_lock<mutex> guard(coutLock);
        cout << "Interrupt signal (" << signum << ") received." << endl;
    }
    // Free memory
    signalHandlerLambda(signum);
    exit(signum);
}

void sendResponse(int clientSock, const string &response)
{
    if (send(clientSock, response.c_str(), response.size(), 0) < 0)
    {
        cerr << "send error" << endl;
    }
}

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

int scanSrcDest(stringstream &ss, int &src, int &dest)
{
    if (!(ss >> src >> dest) || src < 0 || dest < 0)
    {
        cerr << "Invalid source or destination" << endl;
        return -1;
    }

    return 0;
}

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
                    // Clear the stringstream and read new input from the socket
                    ss.clear();
                    ss.str("");
                    memset(buffer, 0, sizeof(buffer));
                    bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);

                    // Check if we received valid data
                    if (bytesReceived <= 0)
                    {
                        // Decrement the client number as we're losing a client
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

                    // Load the new input into the stringstream
                    {
                        ss.write(buffer, bytesReceived);

                        // Attempt to parse again with the new data
                        if (!(ss >> u >> v >> w))
                        {
                            sendResponse(clientSock, "Invalid input format. Please enter 3 integers for u, v, and w.\n");
                            i--; // Retry this iteration since we didn't get valid input
                            continue;
                        }
                    }
                }

                // Validate the edge values (u, v, w)
                if (u < 0 || u > n || v < 0 || v > n || w < 0)
                {
                    sendResponse(clientSock, "Invalid edge values. Vertices should be in the range [1, n] and weight should be non-negative.\n");
                    i--; // Retry this iteration since the input was invalid
                    continue;
                }

                g->addEdge(u, v, w);
            }
            response = "Graph created with " + to_string(n) + " vertices and " + to_string(m) + " edges.\n";
        }

        else if (cmd == "Prim")
        {
            unique_lock<mutex> guard(graphMutex, try_to_lock);
            if (!guard.owns_lock())
            {
                response = "Graph is being used by another thread can't search for MST prim.\n";
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

            factory.setStrategy(new PrimStrategy());

            mst = factory.createMST(g);

            response = "MST created using Prim's algorithm.\n";

            response += mst->printMST();
        }
        else if (cmd == "Kruskal")
        {
            unique_lock<mutex> guard(graphMutex, try_to_lock);
            if (!guard.owns_lock())
            {
                response = "Graph is being used by another thread can't search for MST Kruskal.\n";
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

            factory.setStrategy(new KruskalStrategy());

            mst = factory.createMST(g);

            response = "MST created using Kruskal's algorithm.\n";
            response += mst->printMST();
        }

        else if (cmd == "MSTweight")
        {
            unique_lock<mutex> mstGuard(graphMutex);
            if (mst == nullptr)
            {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }
            response = "Total weight of the MST is: ";

            response += to_string(mst->totalWeight()) + "\n";
        }

        else if (cmd == "Shortestpath")
        {
            unique_lock<mutex> mstGuard(graphMutex);
            if (mst == nullptr)
            {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            int src, dest;
            int res;
            res = scanSrcDest(ss, src, dest);

            if (res == -1)
                continue;
            response = "Shortest path from " + to_string(src) + " to " + to_string(dest) + " is: ";
            response += mst->shortestPath(src, dest);
        }
        else if (cmd == "Longestpath")
        {
            unique_lock<mutex> mstGuard(graphMutex);
            if (mst == nullptr)
            {
                cerr << "MST not created" << endl;
                continue;
            }
            response = "The longest path (diameter) of the MST is: ";

            response += to_string(mst->diameter()) + '\n';
        }
        else if (cmd == "Averdist")
        {
            unique_lock<mutex> mstGuard(graphMutex);
            if (mst == nullptr)
            {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            response = "Average distance of the MST is: ";
            response += to_string(mst->averageDistanceEdges()) + "\n";
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

void acceptConnection(int server_sock, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst, unique_ptr<LFThreadPool> &pool)
{
    {
        unique_lock<mutex> guard(coutLock);
        cout << "Accepting connection on thread: " << this_thread::get_id() << endl;
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
        cout << "New connection from " << s << " on socket " << client_sock << endl;
        cout << "Currently " << clientNumber << " clients connected" << endl;
    }
    // Create new event handler for handling the commands and add it to the reactor
    function<void()> commandHandler = [client_sock, &g, &factory, &mst]()
    {
        handleCommands(client_sock, g, factory, mst);
    };
    pool->addFd(client_sock, commandHandler);
    {
        unique_lock<mutex> guard(coutLock);
        cout << "Client: " << client_sock << " was assigned to thread: " << this_thread::get_id() << endl;
    }
}

int main()
{
    signal(SIGINT, signalHandler);
    unique_ptr<Reactor> reactor = make_unique<Reactor>();
    unique_ptr<Graph> g;
    unique_ptr<Tree> t;
    MSTFactory factory;
    unique_ptr<LFThreadPool> pool;
    // The server socket
    int serverSock;

    signalHandlerLambda = [&](int signum)
    {
        {
            unique_lock<mutex> guard(coutLock);
            cout << "Freeing memory" << endl;
        }
        close(serverSock);
        reactor.reset();
        pool.reset();
        factory.destroyStrategy();
        g.reset();
        t.reset();
    };

    struct sockaddr_in serverAddr;
    // The opt variable is used to set the socket options
    int opt = 1;

    // If the server socket cannot be created, the program will exit
    if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cerr << "Socket creation error" << endl;
        exit(1);
    }

    // If the cannot set the socket to reuse the address, the program will exit
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        cerr << "Setsockopt error" << endl;
        exit(1);
    }

    // Set the server address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    memset(&(serverAddr.sin_zero), '\0', 8);

    // If the server cannot bind to the address, the program will exit
    if (bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "Bind error" << endl;
        exit(1);
    }

    // If the server cannot listen for incoming connections, the program will exit
    if (listen(serverSock, 10) < 0)
    {
        cerr << "Listen error" << endl;
        exit(1);
    }

    {
        unique_lock<mutex> guard(coutLock);
        cout << "MST LF server waiting for requests on port " << port << endl;
        cout << "Server socket: " << serverSock << endl;
    }

    // Add the acceptConnection function to the reactor as a lambda function
    pool = make_unique<LFThreadPool>(10, *reactor);
    reactor->addHandle(serverSock, [serverSock, &g, &factory, &t, &pool]()
                       { acceptConnection(serverSock, g, factory, t, pool); });
    // Allow the threads in the pool to run without the finishing the server
    {
        unique_lock<mutex> guard(coutLock);
        cout << "Server running on thread: " << this_thread::get_id() << endl;
    }
    while (true)
    {
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return 0;
}