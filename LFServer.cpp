#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <csignal>
#include <cstring>

#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "MSTFactory.hpp"
#include "LFThreadPool.hpp"

const int port = 4050;
function<void(int)> signalHandlerLambda;
int clientNumber = 0;
mutex graphMutex;
mutex clientNumMutex;

void signalHandler(int signum)
{
    cout << "Interrupt signal (" << signum << ") received.\n";
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
    cout << n << " " << m << endl;
    this_thread::sleep_for(chrono::milliseconds(1));

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

void handleCommands(int clientSock, LFThreadPool &pool, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst)
{
    cout << "Client socket in handleCommands: " << clientSock << endl;
    char buffer[1024];
    int bytesReceived;
    cout << "Trying to read command" << endl;

    while (true)
    {
        bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0)
        {
            unique_lock<mutex> guard(clientNumMutex);
            clientNumber--;
            guard.unlock();
            if (bytesReceived == 0)
            {
                cout << "Connection closed" << endl;
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
        if (command.empty())
            continue;

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
            scanGraph(clientSock, n, m, ss, g);
            
            // Wait for the graph to be created
            for (int i = 0; i < m; i++)
            {
                int u, v, w;
                if (!(ss >> u >> v >> w))
                {
                    if (u < 0 || u > n || v < 0 || v > n || w < 0)
                    {
                        sendResponse(clientSock, "Invalid edge\n");
                        continue;
                    }
                    memset(buffer, 0, sizeof(buffer));
                    bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
                    if (bytesReceived <= 0)
                    {
                        unique_lock<mutex> guard(clientNumMutex);
                        clientNumber--;
                        guard.unlock();
                        if (bytesReceived == 0)
                        {
                            cout << "Connection closed" << endl;
                            break;
                        }
                        else
                        {
                            cerr << "recv error" << endl;
                        }
                    }
                    ss.clear();
                    ss.str(buffer);
                    ss >> u >> v >> w;
                    // Add the event to the pool
                    g->addEdge(u, v, w);
                    
                }
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

            this_thread::sleep_for(chrono::milliseconds(1));
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

            this_thread::sleep_for(chrono::milliseconds(1));
        }

        else if (cmd == "MSTweight")
        {
            if (mst == nullptr)
            {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }
            response = "Total weight of the MST is: ";

            response += to_string(mst->totalWeight()) + "\n";
            

            this_thread::sleep_for(chrono::milliseconds(1));
        }

        else if (cmd == "Shortestpath")
        {
            if (mst == nullptr)
            {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            int src, dest;
            int res;
            res = scanSrcDest(ss, src, dest);
            
            this_thread::sleep_for(chrono::milliseconds(1));

            if (res == -1)
                continue;
            response = "Shortest path from " + to_string(src) + " to " + to_string(dest) + " is: ";
            response += mst->shortestPath(src, dest);

            this_thread::sleep_for(chrono::milliseconds(1));
        }
        else if (cmd == "Longestpath")
        {
            if (mst == nullptr)
            {
                cerr << "MST not created" << endl;
                continue;
            }

            int src, dest;
            int res;
            
            res = scanSrcDest(ss, src, dest);

            if (res == -1)
                continue;
            response = "Longest path from " + to_string(src) + " to " + to_string(dest) + " is: ";
            
            response += mst->longestPath(src, dest);

            this_thread::sleep_for(chrono::milliseconds(1));
        }
        else if (cmd == "Averdist")
        {
            if (mst == nullptr)
            {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            response = "Average distance of the MST is: ";
            response += to_string(mst->averageDistanceEdges()) + "\n";

            this_thread::sleep_for(chrono::milliseconds(1));
        }
        else if (cmd == "Exit")
        {
            sendResponse(clientSock, "Goodbye\n");
            cout << "Thread number " << this_thread::get_id() << " exiting" << endl;
            // Close the client socket
            close(clientSock);
            unique_lock<mutex> guard(clientNumMutex);
            clientNumber--;
            guard.unlock();
            break;
        }
        else
        {
            cerr << "Unknown command: " << cmd << endl;
        }
        // Send the response to the client
        sendResponse(clientSock, response);
        
    }
}

void acceptConnection(int server_sock, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst, LFThreadPool &pool)
{
    cout << "Thread number " << this_thread::get_id() << " accepting connection" << endl;
    struct sockaddr_in client_addr;
    socklen_t sin_size = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
    if (client_sock == -1)
    {
        perror("accept");
        return;
    }
    unique_lock<mutex> guard(clientNumMutex);
    clientNumber++;
    guard.unlock();
    char s[INET6_ADDRSTRLEN];
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, s, sizeof s);
    cout << "New connection from " << s << " on socket " << client_sock << endl;
    cout << "Currently " << clientNumber << " clients connected" << endl;
    cout << "Server socket: " << server_sock << " Client socket: " << client_sock << endl;
    // Create new event handler for handling the commands and add it to the reactor
    function<void()> commandHandler = [&client_sock, &g, &factory, &mst, &pool]()
    {
        handleCommands(client_sock, pool, g, factory, mst);
    };
    pool.addFd(client_sock, commandHandler);
    cout << "Client: " << client_sock << " was assigned to thread: " << this_thread::get_id() << endl;
}

int main()
{
    signal(SIGINT, signalHandler);
    shared_ptr<Reactor> reactor = make_shared<Reactor>();

    unique_ptr<Graph> g;
    unique_ptr<Tree> t;
    MSTFactory factory;
    unique_ptr<thread::id> leader = make_unique<thread::id>(this_thread::get_id());
    signalHandlerLambda = [&](int signum)
    {
        cout << "Freeing memory" << endl;
        g.reset();
        t.reset();
        reactor.reset();
    };
    // The server socket
    int serverSock;
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

    cout << "MST LF server waiting for requests on port " << port << endl;
    cout << "Server socket: " << serverSock << endl;

    // Add the acceptConnection function to the reactor as a lambda function
    LFThreadPool pool(10, reactor);
    reactor->addHandle(serverSock, [serverSock, &g, &factory, &t, &pool]()
                       { acceptConnection(serverSock, g, factory, t, pool); });
    // Allow the threads in the pool to run without the finishing the server
    cout << "Server running on thread: " << this_thread::get_id() << endl;
    while (true)
    {
        this_thread::sleep_for(chrono::milliseconds(1));

    }
    return 0;
}