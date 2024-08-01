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

void signalHandler(int signum)
{
    cout << "Interrupt signal (" << signum << ") received.\n";
    // Free memory
    signalHandlerLambda(signum);
    exit(signum);
}

int scanGraph(int &n, int &m, stringstream &ss, unique_ptr<Graph> &g)
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




void sendResponse(int clientSock, const string &response)
{
    if (send(clientSock, response.c_str(), response.size(), 0) < 0)
    {
        cerr << "send error" << endl;
    }
}

void handleCommands(int clientSock, Reactor& reactor, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst)
{

    char buffer[1024];
    // Create new event handler for reading the command and add it to the reactor
    unique_ptr<EventHandler> commandHandler = make_unique<ConcreteEventHandler>(clientSock, [&clientSock, &buffer]()
                                                                                 { recv(clientSock, buffer, sizeof(buffer), 0); });
    reactor.registerHandler(commandHandler.get(), EventType::CONNECT);

    while (true)
    {
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0)
        {
            clientNumber--;
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
            int n, m;
            // Create new event handler and add it to the reactor
            unique_ptr<EventHandler> graphHandler = make_unique<ConcreteEventHandler>(clientSock, [&ss, &n, &m, &g]()
                                                                                     { scanGraph(n, m, ss, g); });
            reactor.registerHandler(graphHandler.get(), EventType::WRITE);

            // Wait for the graph to be created
            this_thread::sleep_for(chrono::milliseconds(1));
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
                        clientNumber--;
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
                    // Create new event handler for adding the edge and add it to the reactor
                    unique_ptr<EventHandler> edgeHandler = make_unique<ConcreteEventHandler>(clientSock, [&g, u, v, w]()
                                                                                             { g->addEdge(u, v, w); });
                    reactor.registerHandler(edgeHandler.get(), EventType::WRITE);

                }
            }

            response = "Graph created with " + to_string(n) + " vertices and " + to_string(m) + " edges.\n";
        }

        else if (cmd == "Prim")
        {
            if (g->getAdj().empty())
            {
                sendResponse(clientSock, "Graph not initialized\n");
                continue;
            }
            if (mst != nullptr)
            {
                mst.reset();
                mst = nullptr;
            }

            // Create new event handler for setting the strategy and add it to the reactor
            unique_ptr<EventHandler> primHandler = make_unique<ConcreteEventHandler>(clientSock, [&factory]()
                                                                                     { factory.setStrategy(new PrimStrategy()); });
            reactor.registerHandler(primHandler.get(), EventType::WRITE);
            unique_ptr<EventHandler> mstHandler = make_unique<ConcreteEventHandler>(clientSock, [&g, &factory, &mst]()
                                                                                    { mst = factory.createMST(g); });
            reactor.registerHandler(mstHandler.get(), EventType::WRITE);
            response = "MST created using Prim's algorithm.\n";
            unique_ptr<EventHandler> printHandler = make_unique<ConcreteEventHandler>(clientSock, [&mst, &response]()
                                                                                      { response += mst->printMST(); });
            reactor.registerHandler(printHandler.get(), EventType::WRITE);
            this_thread::sleep_for(chrono::milliseconds(1));
            
        }
        else if (cmd == "Kruskal")

        {
            if (g->getAdj().empty())
            {
                sendResponse(clientSock, "Graph not initialized\n");
                continue;
            }
            if (mst != nullptr)
            {
                mst.reset();
                mst = nullptr;
            }

            // Create new event handler for setting the strategy and add it to the reactor
            unique_ptr<EventHandler> kruskalHandler = make_unique<ConcreteEventHandler>(clientSock, [&factory]()
                                                                                        { factory.setStrategy(new KruskalStrategy()); });
            reactor.registerHandler(kruskalHandler.get(), EventType::WRITE);
            // Create new event handler for creating the MST and add it to the reactor
            unique_ptr<EventHandler> mstHandler = make_unique<ConcreteEventHandler>(clientSock, [&g, &factory, &mst]()
                                                                                    { mst = factory.createMST(g); });
            reactor.registerHandler(mstHandler.get(), EventType::WRITE);
            response = "MST created using Kruskal's algorithm.\n";
            // Add to the queue the function to print the MST
            unique_ptr<EventHandler> printHandler = make_unique<ConcreteEventHandler>(clientSock, [&mst, &response]()
                                                                                      { response += mst->printMST(); });
            reactor.registerHandler(printHandler.get(), EventType::WRITE);
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

            // Create new event handler for computing the weight of the MST and add it to the reactor
            unique_ptr<EventHandler> weightHandler = make_unique<ConcreteEventHandler>(clientSock, [&mst, &response]()
                                                                                      { response += to_string(mst->totalWeight()) + "\n"; });
            reactor.registerHandler(weightHandler.get(), EventType::READ);
           
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
            // Create new event handler for scanning the source and destination and add it to the reactor
            unique_ptr<EventHandler> shortestPathHandler = make_unique<ConcreteEventHandler>(clientSock, [&ss, &src, &dest, &res]()
                                                                                             { res = scanSrcDest(ss, src, dest); });
            this_thread::sleep_for(chrono::milliseconds(1));

            if (res == -1)
                continue;
            response = "Shortest path from " + to_string(src) + " to " + to_string(dest) + " is: ";
            // Create new event handler for finding the shortest path and add it to the reactor
            unique_ptr<EventHandler> pathHandler = make_unique<ConcreteEventHandler>(clientSock, [&mst, src, dest, &response]()
                                                                                      { response += mst->shortestPath(src, dest); });
            reactor.registerHandler(pathHandler.get(), EventType::READ);
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
            // Create new event handler for scanning the source and destination and add it to the reactor
            unique_ptr<EventHandler> longestPathHandler = make_unique<ConcreteEventHandler>(clientSock, [&ss, &src, &dest, &res]()
                                                                                            { res = scanSrcDest(ss, src, dest); });
            reactor.registerHandler(longestPathHandler.get(), EventType::WRITE);                                                                                
            if (res == -1)
                continue;
            response = "Longest path from " + to_string(src) + " to " + to_string(dest) + " is: ";
            // Create new event handler for finding the longest path and add it to the reactor
            unique_ptr<EventHandler> pathHandler = make_unique<ConcreteEventHandler>(clientSock, [&mst, src, dest, &response]()
                                                                                      { response += mst->longestPath(src, dest); });
            reactor.registerHandler(pathHandler.get(), EventType::READ);
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
            // Add to the queue the function to compute the average distance
            unique_ptr<EventHandler> averageHandler = make_unique<ConcreteEventHandler>(clientSock, [&mst, &response]()
                                                                                         { response += to_string(mst->averageDistanceEdges()) + "\n"; });
            reactor.registerHandler(averageHandler.get(), EventType::READ);
            
            this_thread::sleep_for(chrono::milliseconds(1));
            
        }
        else if (cmd == "Exit")
        {
            sendResponse(clientSock, "Goodbye\n");
            cout << "Thread number " << this_thread::get_id() << " exiting" << endl;
            // Create new event handler for closing the connection and add it to the reactor
            unique_ptr<EventHandler> closeHandler = make_unique<ConcreteEventHandler>(clientSock, [&clientSock]()
                                                                                     { close(clientSock); });
            reactor.deactivateHandle(closeHandler.get(), EventType::READ); 
            clientNumber--;
            break;
        }
        else
        {
            cerr << "Unknown command: " << cmd << endl;
        }
        // Create new event handler for sending the response and add it to the reactor
        unique_ptr<EventHandler> responseHandler = make_unique<ConcreteEventHandler>(clientSock, [&clientSock, &response]()
                                                                                     { sendResponse(clientSock, response); });
        reactor.registerHandler(responseHandler.get(), EventType::READ);
    }
}

void acceptConnection(int server_sock, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst, unique_ptr<Reactor> &reactor)
{
    struct sockaddr_in client_addr;
    socklen_t sin_size = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
    if (client_sock == -1)
    {
        perror("accept");
        return ;
    }
    clientNumber++;
    char s[INET6_ADDRSTRLEN];
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, s, sizeof s);
    cout << "New connection from " << s << " on socket " << client_sock << endl;
    cout << "Currently " << clientNumber << " clients connected" << endl;
    // Create new event handler for handling the commands and add it to the reactor
    unique_ptr<EventHandler> commandHandler = make_unique<ConcreteEventHandler>(client_sock, [&client_sock, &reactor, &g, &factory, &mst]()
                                                                                 { handleCommands(client_sock, *reactor, g, factory, mst); });
    reactor->registerHandler(commandHandler.get(), EventType::READ);
     
}

int main()
{
    signal(SIGINT, signalHandler);
    unique_ptr<Reactor> reactor = make_unique<Reactor>();
    LFThreadPool pool(10, reactor.get());
    unique_ptr<Graph> g;
    unique_ptr<Tree> t;
    MSTFactory factory;
    signalHandlerLambda = [&](int signum) {
        cout << "Freeing memory" << endl;
        g.reset();
        t.reset();
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
    while (true)
    {
        // Create a new event handler for accepting connections and add it to the reactor
        unique_ptr<EventHandler> acceptHandler = make_unique<ConcreteEventHandler>(serverSock, [&serverSock, &g, &factory, &t, &reactor]()
                                                                                   { acceptConnection(serverSock, g, factory, t, reactor); });
                                                                                          
        reactor->registerHandler(acceptHandler.get(), EventType::ACCEPT);
        reactor->handleEvents();
    }

    close(serverSock);
    return 0;

}