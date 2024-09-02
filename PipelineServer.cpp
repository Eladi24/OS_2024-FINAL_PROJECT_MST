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
atomic<int> clientNumber(0);
mutex graphLock;
mutex futureLock;
mutex &coutLock = ActiveObject::getOutputMutex();

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
    coutLock.lock();
    cout << "Interrupt signal (" << signum << ") received.\n";
    coutLock.unlock();
    // Free memory
    terminateFlag.store(true);
    unique_lock<mutex> guard(graphLock);
    signalHandlerLambda(signum);
    guard.unlock();
    exit(signum);
}

void sendResponse(int clientSock, const string &future)
{
    if (send(clientSock, future.c_str(), future.size(), 0) < 0)
    {
        unique_lock<mutex> guard(coutLock);
        cerr << "send error" << endl;
    }
}

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

int scanSrcDest(stringstream &ss, atomic<int> &src, atomic<int> &dest)
{
    int tempSrc, tempDest;

    if (!(ss >> tempSrc >> tempDest) || tempSrc < 0 || tempDest < 0)
    {

        cerr << "Invalid source or destination" << endl;
        return -1;
    }

    // Atomically update the atomic<int> variables
    src.store(tempSrc, memory_order_release);
    dest.store(tempDest, memory_order_release);

    return 0;
}

void handleCommands(int clientSock, vector<unique_ptr<ActiveObject>> &pipeline, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst) {
    condition_variable cv;
    mutex ssLock;
    atomic<bool> done(false);
    char buffer[1024] = {0};

    while (!terminateFlag.load()) {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            clientNumber.store(clientNumber.load(memory_order_acquire) - 1, memory_order_release);
            if (bytesReceived == 0) {
                unique_lock<mutex> guard(coutLock);
                cout << "Connection closed" << endl;
                break;
            } else {
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

        if (cmd == "Newgraph") {
            // Lock the graph mutex to prevent other threads from using the graph
            unique_lock<mutex> graphGuard(graphLock, try_to_lock);

            // If the graph resource is being used by another thread, the current thread will not be blocked
            if (!graphGuard.owns_lock()) {
                sendResponse(clientSock, "Graph is being used by another thread. Cannot initialize new graph.\n");
                continue;
            }
            if (g != nullptr) {
                g.reset();
                g = nullptr;
            }
            if (mst != nullptr) {
                mst.reset();
                mst = nullptr;
            }

            int n, m;
            mutex initLock;
            atomic<bool> initDone(false);
            condition_variable initCV;

            pipeline[0]->enqueue([&ss, &n, &m, &g, &ssLock, &initLock, &initDone, &initCV]() {
                unique_lock<mutex> guard2(ssLock);
                unique_lock<mutex> guard(initLock);
                scanGraph(n, m, ss, g);
                initDone.store(true, memory_order_release);
                initCV.notify_one();
            });

            // Wait for the graph to be created
            {
                unique_lock<mutex> guard(initLock);
                while (!initDone) {
                    initCV.wait(guard);
                }
            }

            for (int i = 0; i < m; i++) {
                int u = 0, v = 0, w = 0;

                {
                    unique_lock<mutex> guard(ssLock);
                    if (!(ss >> u >> v >> w)) {
                        ss.clear();
                        ss.str("");
                        memset(buffer, 0, sizeof(buffer));
                        bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);

                        if (bytesReceived <= 0) {
                            clientNumber.store(clientNumber.load(memory_order_acquire) - 1, memory_order_release);
                            if (bytesReceived == 0) {
                                unique_lock<mutex> coutGuard(coutLock);
                                cout << "Connection closed by client." << endl;
                                break;
                            } else {
                                cerr << "recv error: " << strerror(errno) << endl;
                                break;
                            }
                        }

                        ss.write(buffer, bytesReceived);
                        if (!(ss >> u >> v >> w)) {
                            sendResponse(clientSock, "Invalid input format. Please enter 3 integers for u, v, and w.\n");
                            i--; // Retry this iteration since we didn't get valid input
                            continue;
                        }
                    }
                }

                if (u < 0 || u > n || v < 0 || v > n || w < 0) {
                    sendResponse(clientSock, "Invalid edge values. Vertices should be in the range [1, n] and weight should be non-negative.\n");
                    i--; // Retry this iteration since the input was invalid
                    continue;
                }

                pipeline[0]->enqueue([&g, u, v, w]() {
                    unique_lock<mutex> guard(graphLock);
                    g->addEdge(u, v, w);
                });
            }

            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "Graph created with " + to_string(n) + " vertices and " + to_string(m) + " edges.\n";
                done.store(true, memory_order_release);
                cv.notify_one();
            }
        } else if (cmd == "Prim") {
            unique_lock<mutex> graphGuard(graphLock); // Lock for MST operations
            if (g == nullptr || g->getAdj().empty()) {
                sendResponse(clientSock, "Graph not initialized\n");
                continue;
            }

            if (mst != nullptr) {
                mst.reset();
                mst = nullptr;
            }

            pipeline[1]->enqueue([&g, &factory, &mst]() {
                unique_lock<mutex> guard(graphLock);
                factory.setStrategy(new PrimStrategy());
                mst = factory.createMST(g);
            });

            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "MST created using Prim's algorithm.\n";
            }

            pipeline[1]->enqueue([&mst, &future, &cv, &done]() {
                unique_lock<mutex> guard(graphLock);
                unique_lock<mutex> futureGuard(futureLock);
                future += mst->printMST();
                done.store(true, memory_order_release);
                cv.notify_one();
            });
        } else if (cmd == "Kruskal") {
            unique_lock<mutex> graphGuard(graphLock); // Lock for MST operations
            if (g == nullptr || g->getAdj().empty()) {
                sendResponse(clientSock, "Graph not initialized\n");
                continue;
            }

            if (mst != nullptr) {
                mst.reset();
                mst = nullptr;
            }

            pipeline[2]->enqueue([&g, &factory, &mst]() {
                unique_lock<mutex> guard(graphLock);
                factory.setStrategy(new KruskalStrategy());
                mst = factory.createMST(g);
            });

            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "MST created using Kruskal's algorithm.\n";
            }

            pipeline[2]->enqueue([&mst, &future, &cv, &done]() {
                unique_lock<mutex> guard(graphLock);
                unique_lock<mutex> futureGuard(futureLock);
                future += mst->printMST();
                done.store(true, memory_order_release);
                cv.notify_one();
            });
        } else if (cmd == "MSTweight") {
            unique_lock<mutex> graphGuard(graphLock); // Lock for MST operations
            if (mst == nullptr) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "Total weight of the MST is: ";
            }

            pipeline[3]->enqueue([&mst, &future, &cv, &done]() {
                unique_lock<mutex> guard(graphLock);
                unique_lock<mutex> futureGuard(futureLock);
                future += to_string(mst->totalWeight()) + "\n";
                done.store(true, memory_order_release);
                cv.notify_one();
            });
        } else if (cmd == "Shortestpath") {
            unique_lock<mutex> graphGuard(graphLock); // Lock for MST operations
            if (mst == nullptr) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            atomic<int> src{-1}, dest{-1};
            atomic<int> res{0};

            mutex resMutex;
            condition_variable resCv;
            bool ready = false;

            pipeline[4]->enqueue([&ss, &src, &dest, &res, &resMutex, &resCv, &ready]() {
                res.store(scanSrcDest(ss, src, dest), memory_order_release);
                unique_lock<mutex> lock(resMutex);
                ready = true;
                resCv.notify_one();
            });

            {
                unique_lock<mutex> lock(resMutex);
                resCv.wait(lock, [&ready]() { return ready; });
            }

            if (res.load(memory_order_acquire) == -1) {
                sendResponse(clientSock, "Invalid source or destination\n");
                continue;
            }

            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "Shortest path from " + to_string(src.load()) + " to " + to_string(dest.load()) + " is: ";
            }

            pipeline[4]->enqueue([&mst, &src, &dest, &future, &cv, &done]() {
                unique_lock<mutex> guard(graphLock);
                string shortestPathResult = mst->shortestPath(src.load(), dest.load());

                unique_lock<mutex> futureGuard(futureLock);
                future += shortestPathResult;
                done.store(true, memory_order_release);
                cv.notify_one();
            });
        } else if (cmd == "Longestpath") {
            unique_lock<mutex> graphGuard(graphLock); // Lock for MST operations
            if (mst == nullptr) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            {
                unique_lock<mutex> futureGuard(futureLock);
                future = "The longest path (diameter) of the MST is: ";
            }

            pipeline[5]->enqueue([&mst, &future, &cv, &done]() {
                unique_lock<mutex> guard(graphLock);
                int diameter = mst->diameter();
                unique_lock<mutex> futureGuard(futureLock);
                future += to_string(diameter) + "\n";
                done.store(true, memory_order_release);
                cv.notify_one();
            });
        } else if (cmd == "Averdist") {
            unique_lock<mutex> graphGuard(graphLock); // Lock for MST operations
            if (mst == nullptr) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            pipeline[6]->enqueue([&mst, &future, &cv, &done]() {
                unique_lock<mutex> guard(graphLock);
                unique_lock<mutex> futureGuard(futureLock);
                future += to_string(mst->averageDistanceEdges()) + "\n";
                done.store(true, memory_order_release);
                cv.notify_one();
            });
        } else if (cmd == "Exit") {
            sendResponse(clientSock, "Goodbye\n");
            clientNumber.store(clientNumber.load(memory_order_acquire) - 1, memory_order_release);
            break;
        } else {
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
    {
        unique_lock<mutex> guard(coutLock);
        cout << "Thread number " << this_thread::get_id() << " exiting" << endl;
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
    unique_ptr<functArgs> faPtr;
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
        faPtr.reset();
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

    {
        unique_lock<mutex> guard(coutLock);
        cout << "MST pipeline server waiting for requests on port " << port << endl;
    }
    // The pipeline of active objects
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