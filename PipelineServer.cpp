#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <csignal>
#include <cstring>
#include <csignal>

#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "MSTFactory.hpp"
#include "ActiveObject.hpp"

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

void sendResponse(int clientSock, const string &future)
{
    if (send(clientSock, future.c_str(), future.size(), 0) < 0)
    {
        cerr << "send error" << endl;
    }
}

void setupPipeline(vector<unique_ptr<ActiveObject>> &pipeline)
{
    // ActiveObject for creating the Graph
    auto graphAO = make_unique<ActiveObject>();

    // ActiveObject for finding MST using Prim's algorithm
    auto primAO = make_unique<ActiveObject>();

    auto kruskalAO = make_unique<ActiveObject>();

    // ActiveObject for computing MST weight
    auto mstWeightAO = make_unique<ActiveObject>();

    // ActiveObject for finding shortest path
    auto shortestPathAO = make_unique<ActiveObject>();

    // ActiveObject for finding longest path
    auto longestPathAO = make_unique<ActiveObject>();

    // ActiveObject for computing average distance
    auto averageDistanceAO = make_unique<ActiveObject>();

    // Add these ActiveObjects to the pipeline
    pipeline.push_back(move(graphAO));
    pipeline.push_back(move(primAO));
    pipeline.push_back(move(kruskalAO));
    pipeline.push_back(move(mstWeightAO));
    pipeline.push_back(move(shortestPathAO));
    pipeline.push_back(move(longestPathAO));
    pipeline.push_back(move(averageDistanceAO));
}

void handleCommands(int clientSock, vector<unique_ptr<ActiveObject>> &pipeline, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst)
{

    char buffer[1024];
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
        ss >> cmd;
        string future;

        if (cmd == "Newgraph")
        {
            int n, m;
            pipeline[0]->enqueue([&ss, &n, &m, &g]()
                                 { scanGraph(n, m, ss, g); });

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
                    pipeline[0]->enqueue([&g, &u, &v, &w]()
                                         { g->addEdge(u, v, w); });
                }
            }

            future = "Graph created with " + to_string(n) + " vertices and " + to_string(m) + " edges.\n";
        }

        else if (cmd == "Prim")
        {
            if (g->getAdj().empty())
            {
                cerr << "Graph not initialized" << endl;
                continue;
            }
            if (mst != nullptr)
            {
                mst.reset();
                mst = nullptr;
            }

            // Add to the queue the function to set the strategy
            pipeline[1]->enqueue([&factory]()
                                 { factory.setStrategy(new PrimStrategy()); });
            pipeline[1]->enqueue([&g, &factory, &mst]()
                                 { mst = factory.createMST(g); });
            future = "MST created using Prim's algorithm.\n";
            pipeline[1]->enqueue([&mst, &future]()
                                 { future += mst->printMST(); });
            this_thread::sleep_for(chrono::milliseconds(1));
        }
        else if (cmd == "Kruskal")

        {
            if (g->getAdj().empty())
            {
                cerr << "Graph not initialized" << endl;
                continue;
            }
            if (mst != nullptr)
            {
                mst.reset();
                mst = nullptr;
            }

            // Add to the queue the function to set the strategy
            pipeline[2]->enqueue([&factory]()
                                 { factory.setStrategy(new KruskalStrategy()); });
            // Add to the queue the function to create the MST
            pipeline[2]->enqueue([&g, &factory, &mst]()
                                 { mst = factory.createMST(g); });
            future = "MST created using Kruskal's algorithm.\n";
            // Add to the queue the function to print the MST
            pipeline[2]->enqueue([&mst, &future]()
                                 { future += mst->printMST(); });
            this_thread::sleep_for(chrono::milliseconds(1));
        }

        else if (cmd == "MSTweight")
        {
            if (mst == nullptr)
            {
                cerr << "MST not created" << endl;
                continue;
            }
            future = "Total weight of the MST is: ";

            // Add to the queue the function to compute the total weight of the MST
            pipeline[3]->enqueue([&mst, &future]()
                                 { future += to_string(mst->totalWeight()) + "\n"; });
            this_thread::sleep_for(chrono::milliseconds(1));
        }

        else if (cmd == "Shortestpath")
        {
            if (mst == nullptr)
            {
                cerr << "MST not created" << endl;
                continue;
            }

            int src, dest;
            int res;
            pipeline[4]->enqueue([&ss, &src, &dest, &res]()
                                 { res = scanSrcDest(ss, src, dest); });
            this_thread::sleep_for(chrono::milliseconds(1));

            if (res == -1)
                continue;
            future = "Shortest path from " + to_string(src) + " to " + to_string(dest) + " is: ";
            // Add to the queue the function to find the shortest path
            pipeline[4]->enqueue([&mst, &src, &dest, &future]()
                                 { future += mst->shortestPath(src, dest); });
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
            pipeline[5]->enqueue([&ss, &src, &dest, &res]()
                                 { res = scanSrcDest(ss, src, dest); });
            if (res == -1)
                continue;
            future = "Longest path from " + to_string(src) + " to " + to_string(dest) + " is: ";
            // Add to the queue the function to find the longest path
            pipeline[5]->enqueue([&mst, &src, &dest, &future]()
                                 { future += mst->longestPath(src, dest); });
            this_thread::sleep_for(chrono::milliseconds(1));
        }
        else if (cmd == "Averdist")
        {
            if (mst == nullptr)
            {
                cerr << "MST not created" << endl;
                continue;
            }

           future = "Average distance of the MST is: ";
            // Add to the queue the function to compute the average distance
            pipeline[6]->enqueue([&mst, &future]()
                                 { future += to_string(mst->averageDistanceEdges()) + "\n"; });
            
            this_thread::sleep_for(chrono::milliseconds(1));
            
        }
        else if (cmd == "Exit")
        {
            sendResponse(clientSock, "Goodbye\n");
            cout << "Thread number " << this_thread::get_id() << " exiting" << endl;
            close(clientSock);
            clientNumber--;
            break;
        }
        else
        {
            cerr << "Unknown command: " << cmd << endl;
        }
        sendResponse(clientSock, future);
    }
}

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
    clientNumber++;
    char s[INET6_ADDRSTRLEN];
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, s, sizeof s);
    cout << "New connection from " << s << " on socket " << client_sock << endl;
    cout << "Currently " << clientNumber << " clients connected" << endl;
    return client_sock;
}

// Server main function
int main()
{
    signal(SIGINT, signalHandler);
    vector<unique_ptr<ActiveObject>> pipeline;
    unique_ptr<Graph> g;
    MSTFactory factory;
    unique_ptr<Tree> mst;
    signalHandlerLambda = [&](int signum)
    {
        cout << "Freeing memory" << endl;
        mst.reset();
        g.reset();
        for (auto &obj : pipeline)
        {
            obj.reset();
        }
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

    cout << "MST pipeline server waiting for requests on port " << port << endl;
    // The pipeline of active objects

    setupPipeline(pipeline);
    // Accept connections and handle commands
    while (true)
    {
        int newClientSock = acceptConnection(serverSock);
        if (newClientSock == -1)
        {
            continue;
        }

        // Create a new thread for each client
        thread clientThread([newClientSock, &pipeline, &g, &factory, &mst]()
                                 {
                                 auto clientHandler = make_unique<ActiveObject>();
                                 clientHandler->enqueue([newClientSock, &pipeline, &g, &factory, &mst]()
                                                        { handleCommands(newClientSock, pipeline, g, factory, mst); }); });
        clientThread.detach();
    }
    mst.reset();
    g.reset();
    for (auto &obj : pipeline)
    {
        obj.reset();
    }
    close(serverSock);
    return 0;
}