#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <csignal>
#include <cstring>
#include <csignal>
#include <future>
#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "MSTFactory.hpp"
#include "ActiveObject.hpp"

const int port = 4050;
function<void(int)> signalHandlerLambda;
int clientNumber = 0;
mutex graphLock;
mutex futureLock;
mutex coutLock;
atomic<bool> terminateFlag(false);

struct functArgs
{
    int clientSock;
    vector<unique_ptr<ActiveObject>> &pipeline;
    unique_ptr<Graph> &g;
    MSTFactory &factory;
    unique_ptr<Tree> &mst;
};

void signalHandler(int signum)
{
    unique_lock<mutex> guard(graphLock);
    cout << "Interrupt signal (" << signum << ") received.\n";
    // Free memory
    terminateFlag.store(true);
    signalHandlerLambda(signum);
    guard.unlock();
    exit(signum);
}

void sendResponse(int clientSock, const string &future)
{
    if (send(clientSock, future.c_str(), future.size(), 0) < 0)
    {
        cerr << "send error" << endl;
    }
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

void handleCommands(int clientSock, vector<unique_ptr<ActiveObject>> &pipeline, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst)
{
    condition_variable cv;
    atomic<bool> done(false);
    char buffer[1024] = {0};
    while (!terminateFlag.load())
    {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0)
        {
            clientNumber--;
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
        if (command.empty())
            continue;

        stringstream ss(command);
        string cmd;
        ss >> cmd;
        string future;

        if (cmd == "Newgraph")
        {
            unique_lock<mutex> guard(graphLock, try_to_lock);
            if (!guard.owns_lock())
            {
                sendResponse(clientSock, "Graph is being used by another thread can't create a new graph.\n");
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
            pipeline[0]->enqueue([&ss, &n, &m, &g]()
                                 { scanGraph(n, m, ss, g); });

            // Wait for the graph to be created
            this_thread::sleep_for(chrono::milliseconds(1));

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
                        clientNumber--; // Decrement the client number as we're losing a client
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
                    ss.write(buffer, bytesReceived);

                    // Attempt to parse again with the new data
                    if (!(ss >> u >> v >> w))
                    {
                        sendResponse(clientSock, "Invalid input format. Please enter 3 integers for u, v, and w.\n");
                        i--; // Retry this iteration since we didn't get valid input
                        continue;
                    }
                }

                // Validate the edge values (u, v, w)
                if (u < 0 || u > n || v < 0 || v > n || w < 0)
                {
                    sendResponse(clientSock, "Invalid edge values. Vertices should be in the range [1, n] and weight should be non-negative.\n");
                    i--; // Retry this iteration since the input was invalid
                    continue;
                }

                // Add the edge to the graph via the pipeline
                pipeline[0]->enqueue([&g, u, v, w]()
                                     {  unique_lock<mutex> guard(graphLock, try_to_lock);
                                        g->addEdge(u, v, w); });
            }

            future = "Graph created with " + to_string(n) + " vertices and " + to_string(m) + " edges.\n";
            done.store(true);
        }

        else if (cmd == "Prim")
        {
            unique_lock<mutex> guard(graphLock, try_to_lock);
            if (!guard.owns_lock())
            {

                sendResponse(clientSock, "Graph is being used by another thread can't search for MST prim.\n");
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

            // Add to the queue the function to set the strategy
            pipeline[1]->enqueue([&factory]()
                                 { factory.setStrategy(new PrimStrategy()); });
            pipeline[1]->enqueue([&g, &factory, &mst]()
                                 { mst = factory.createMST(g); });
            future = "MST created using Prim's algorithm.\n";
            pipeline[1]->enqueue([&mst, &future, &cv, &done]()
                                 {  unique_lock<mutex> guard(futureLock, try_to_lock);
                                    future += mst->printMST();
                                    done = true;
                                    guard.unlock();
                                    cv.notify_one(); });
            this_thread::sleep_for(chrono::milliseconds(1));
        }
        else if (cmd == "Kruskal")
        {
            unique_lock<mutex> guard(graphLock, try_to_lock);
            if (!guard.owns_lock())
            {

                sendResponse(clientSock, "Graph is being used by another thread can't search for MST Kruskal.\n");
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

            // Add to the queue the function to set the strategy
            pipeline[2]->enqueue([&factory]()
                                 { factory.setStrategy(new KruskalStrategy()); });
            // Add to the queue the function to create the MST
            pipeline[2]->enqueue([&g, &factory, &mst]()
                                 { mst = factory.createMST(g); });
            future = "MST created using Kruskal's algorithm.\n";
            // Add to the queue the function to print the MST
            pipeline[2]->enqueue([&mst, &future, &cv, &done]()
                                 { unique_lock<mutex> guard(futureLock, try_to_lock);
                                    future += mst->printMST();
                                    done = true;
                                    guard.unlock();
                                    cv.notify_one(); });
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
            pipeline[3]->enqueue([&mst, &future, &cv, &done]()
                                 {  unique_lock<mutex> guard(futureLock, try_to_lock);
                                    future += to_string(mst->totalWeight()) + "\n";
                                    done = true; 
                                    guard.unlock();
                                    cv.notify_one(); });
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
            pipeline[4]->enqueue([&mst, &src, &dest, &future, &cv, &done]()
                                 {  unique_lock<mutex> guard(futureLock, try_to_lock);
                                    future += mst->shortestPath(src, dest);
                                    done = true;
                                    guard.unlock();
                                    cv.notify_one(); });
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
            pipeline[5]->enqueue([&mst, &src, &dest, &future, &cv, &done]()
                                 {  unique_lock<mutex> guard(futureLock, try_to_lock);
                                    future += mst->longestPath(src, dest);
                                    done = true;
                                    guard.unlock();
                                    cv.notify_one(); });
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
            pipeline[6]->enqueue([&mst, &future, &cv, &done]()
                                 {  unique_lock<mutex> guard(futureLock, try_to_lock);
                                    future += to_string(mst->averageDistanceEdges()) + "\n";
                                    done = true;
                                    guard.unlock();
                                    cv.notify_one(); });

            this_thread::sleep_for(chrono::milliseconds(1));
        }
        else if (cmd == "Exit")
        {
            sendResponse(clientSock, "Goodbye\n");
            clientNumber--;
            break;
        }
        else
        {
            sendResponse(clientSock, "Invalid command" + cmd + "\n");
            continue;
        }
        unique_lock<mutex> guard(futureLock, try_to_lock);
        while (!done)
        {
            cv.wait(guard);
        }
        sendResponse(clientSock, future);
        done = false;
        future.clear();
    }
    close(clientSock);
    unique_lock<mutex> guard(coutLock);
    cout << "Thread number " << this_thread::get_id() << " exiting" << endl;
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
    unique_lock<mutex> guard(coutLock);
    cout << "New connection from " << s << " on socket " << client_sock << endl;
    cout << "Currently " << clientNumber << " clients connected" << endl;
    return client_sock;
}

// Server main function
int main()
{

    signal(SIGINT, signalHandler);
    vector<thread> threads;
    vector<unique_ptr<ActiveObject>> pipeline;
    unique_ptr<Graph> g;
    MSTFactory factory;
    unique_ptr<Tree> mst;
    vector<pthread_t> clientThreads;
    signalHandlerLambda = [&](int signum)
    {   
        
        cout << "Freeing memory" << endl;
        cout << boolalpha << terminateFlag.load() << endl;
        for (auto &thread : clientThreads)
        {
            pthread_cancel(thread);
            pthread_join(thread, nullptr);
        }
        clientThreads.clear();
        clientThreads.shrink_to_fit();
        factory.destroyStrategy();
        mst.reset();
        g.reset();
        for (auto &obj : pipeline)
        {
            cout << "Object: " << &obj << endl;
            obj.reset();
        }
        pipeline.clear();
        pipeline.shrink_to_fit();
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
    unique_lock<mutex> guard(coutLock);
    cout << "MST pipeline server waiting for requests on port " << port << endl;
    // The pipeline of active objects
    guard.unlock();
    guard.release();
    for (int i = 0; i < 7; i++)
    {
        pipeline.push_back(make_unique<ActiveObject>());
    }
    // Accept connections and handle commands
    while (true)
    {
        int newClientSock = acceptConnection(serverSock);
        if (newClientSock == -1)
        {
            continue;
        }
        // create pthread for each client
        functArgs fa = {newClientSock, ref(pipeline), ref(g), ref(factory), ref(mst)};
        pthread_t tid;
        auto threadFunc = [](void *arg) -> void *
        {
            functArgs *fa = static_cast<functArgs *>(arg);
            handleCommands(fa->clientSock, fa->pipeline, fa->g, fa->factory, fa->mst);
            return nullptr;
        };
        pthread_create(&tid, nullptr, threadFunc, &fa);
        clientThreads.push_back(tid);
    }
    close(serverSock);

    return 0;
}