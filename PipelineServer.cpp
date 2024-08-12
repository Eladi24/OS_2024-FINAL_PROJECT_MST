#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <functional>
#include <sstream>
#include <mutex>
#include <atomic>
#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "ActiveObject.hpp"

using namespace std;

const int port = 4050;
function<void(int)> signalHandlerLambda;
int clientNumber = 0;
std::mutex pipelineMutex;
std::mutex graphMutex;  // Protect access to the Graph object
std::mutex mstMutex;    // Protect access to the MST object
std::mutex coutMutex;   // Mutex for protecting cout
std::mutex clientNumberMutex;
int serverSock;  // Declare serverSock at the global or class scope
std::atomic<bool> serverRunning(true);
std::atomic<bool> shutdownInProgress(false); // Indicates if shutdown is in progress
std::mutex shutdownMutex; // Protect shutdown operations

void initiateShutdown() {
    {
        std::lock_guard<std::mutex> lock(coutMutex);
        cout << "Initiating server shutdown..." << endl;
    }

    serverRunning = false; // Stop the server from accepting new connections
    shutdownInProgress = true; // Indicate shutdown is in progress
     //std::this_thread::sleep_for(std::chrono::milliseconds(50));

   // close(serverSock); // Close the server socket to unblock accept()
//exit(0);
    // Wait for all threads to finish (if you are tracking threads, join them here)
}
void signalHandler(int signum) {
    {
        std::lock_guard<std::mutex> lock(coutMutex);
        cout << "\nInterrupt signal (" << signum << ") received. Press 's' to shutdown the server safely." << endl;
    }

    char input;
    while (true) {
        std::cin >> input;
        if (input == 's' || input == 'S') {
            initiateShutdown();
            break;
        } else {
            std::lock_guard<std::mutex> lock(coutMutex);
            cout << "Invalid input. Press 's' to shutdown the server safely." << endl;
        }
    }
}


void executePipeline(vector<unique_ptr<ActiveObject>> &pipeline) {
    std::lock_guard<std::mutex> lock(pipelineMutex);  // Lock the mutex before accessing the pipeline

    for (auto &stage : pipeline) {
        stage->enqueue([]{});  // Access the ActiveObject in a thread-safe manner
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Wait for tasks to complete
}

int scanGraph(int &n, int &m, stringstream &ss, std::shared_ptr<Graph> &g) {
    std::lock_guard<std::mutex> lock(graphMutex);  // Lock the graph mutex

    {
        std::lock_guard<std::mutex> coutLock(coutMutex);  // Lock the cout mutex
        cout << "scanGraph: Lock acquired" << endl;
    }

    if (!(ss >> n >> m) || n <= 0 || m < 0) {
        std::lock_guard<std::mutex> cerrLock(coutMutex);  // Lock the cerr mutex (shared with cout)
        cerr << "scanGraph: Invalid graph input (n: " << n << ", m: " << m << ")" << endl;
        return -1;
    }

    {
        std::lock_guard<std::mutex> coutLock(coutMutex);  // Lock the cout mutex
        cout << "scanGraph: Parsed n = " << n << ", m = " << m << endl;
    }

    g = std::make_shared<Graph>(n, m);  // Assign the new graph

    {
        std::lock_guard<std::mutex> coutLock(coutMutex);  // Lock the cout mutex
        cout << "scanGraph: Graph created with " << n << " vertices and " << m << " edges." << endl;
    }

    return 0;
}

void sendResponse(int clientSock, const string &response) {
    if (send(clientSock, response.c_str(), response.size(), 0) < 0) {
        std::lock_guard<std::mutex> cerrLock(coutMutex);  // Lock the mutex to protect cerr
        cerr << "send error" << endl;
    }
}

void setupPipeline(std::vector<std::unique_ptr<ActiveObject>> &pipeline, const std::string &algorithm) {
    std::lock_guard<std::mutex> lock(pipelineMutex);  // Lock the mutex before accessing pipeline

    pipeline.clear();  // Clear any previous pipeline setup

    if (algorithm == "Prim") {
        auto primAO = std::make_unique<ActiveObject>();
        pipeline.push_back(std::move(primAO));
    } else if (algorithm == "Kruskal") {
        auto kruskalAO = std::make_unique<ActiveObject>();
        pipeline.push_back(std::move(kruskalAO));
    }

    auto mstWeightAO = std::make_unique<ActiveObject>();
    auto shortestPathAO = std::make_unique<ActiveObject>();
    auto longestPathAO = std::make_unique<ActiveObject>();
    auto averageDistanceAO = std::make_unique<ActiveObject>();

    pipeline.push_back(std::move(mstWeightAO));
    pipeline.push_back(std::move(shortestPathAO));
    pipeline.push_back(std::move(longestPathAO));
    pipeline.push_back(std::move(averageDistanceAO));
}

void handleCommands(int clientSock, vector<unique_ptr<ActiveObject>> &pipeline, MSTFactory &factory, std::shared_ptr<Graph> &g, std::shared_ptr<Tree> &mst) {
    char buffer[1024];

    while (true) {
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);

        if (bytesReceived <= 0) {
            std::lock_guard<std::mutex> lock(clientNumberMutex);  // Protect clientNumber
            clientNumber--;

            if (bytesReceived == 0) {
                {
                    std::lock_guard<std::mutex> coutLock(coutMutex);
                    cout << "Connection closed" << endl;
                }
                break;
            } else {
                {
                    std::lock_guard<std::mutex> coutLock(coutMutex);
                    cerr << "recv error" << endl;
                }
            }
            close(clientSock);
            return;
        }

        buffer[bytesReceived] = '\0';
        string command(buffer);
        if (command.empty()) continue;

        stringstream ss(command);
        string cmd;
        ss >> cmd;

        if (cmd == "Newgraph") {
            {
                std::lock_guard<std::mutex> coutLock(coutMutex);
                cout << "Handling Newgraph command." << endl;
            }

            int n, m;
            if (scanGraph(n, m, ss, g) == -1) {
                sendResponse(clientSock, "Invalid graph input\n");
                continue;
            }

            sendResponse(clientSock, "Graph created with " + to_string(n) + " vertices. Please send " + to_string(m) + " edges.\n");

            for (int i = 0; i < m; ++i) {
                bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
                if (bytesReceived <= 0) {
                    std::lock_guard<std::mutex> lock(clientNumberMutex);
                    clientNumber--;
                    if (bytesReceived == 0) {
                        {
                            std::lock_guard<std::mutex> coutLock(coutMutex);
                            cout << "Connection closed" << endl;
                        }
                        break;
                    } else {
                        {
                            std::lock_guard<std::mutex> coutLock(coutMutex);
                            cerr << "recv error" << endl;
                        }
                    }
                    close(clientSock);
                    return;
                }

                buffer[bytesReceived] = '\0';
                string edgeInput(buffer);
                stringstream edgeStream(edgeInput);
                int u, v, w;
                if (!(edgeStream >> u >> v >> w)) {
                    sendResponse(clientSock, "Invalid edge input\n");
                    continue;
                }
                {
                    std::lock_guard<std::mutex> graphLock(graphMutex);  // Protect Graph access
                    g->addEdge(u, v, w);
                }
            }

            sendResponse(clientSock, "Graph creation complete with " + to_string(m) + " edges.\n");

        } else if (cmd == "Prim" || cmd == "Kruskal") {
            {
                std::lock_guard<std::mutex> graphLock(graphMutex);  // Protect Graph access
                if (g->getAdj().empty()) {
                    sendResponse(clientSock, "Graph not initialized\n");
                    continue;
                }
            }

            setupPipeline(pipeline, cmd);  // cmd can be "Prim" or "Kruskal"

            pipeline[0]->enqueue([&factory, cmd]() {
                if (cmd == "Prim") {
                    factory.setStrategy(make_unique<PrimStrategy>());
                } else if (cmd == "Kruskal") {
                    factory.setStrategy(make_unique<KruskalStrategy>());
                }
            });

            pipeline[0]->enqueue([&g, &factory, &mst]() {
                std::lock_guard<std::mutex> mstLock(mstMutex);      // Protect MST modification
                mst = factory.createMST(g);
            });

            pipeline[0]->enqueue([clientSock, &mst, cmd]() {
                std::string future;
                {
                    std::lock_guard<std::mutex> mstLock(mstMutex);  // Protect MST access
                    future = "MST created using " + cmd + "'s algorithm.\n";
                    future += mst->printMST();
                }
                sendResponse(clientSock, future);
            });

            executePipeline(pipeline);

        } else if (cmd == "MSTweight" || cmd == "Shortestpath" || cmd == "Longestpath" || cmd == "Averdist") {
            std::shared_ptr<Tree> localMst;
            {
                std::lock_guard<std::mutex> mstLock(mstMutex);  // Protect MST access
                localMst = mst;
            }

            if (localMst == nullptr) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            int pipelineIndex = 1;  // Default to MSTweight
            if (cmd == "Shortestpath") pipelineIndex = 2;
            else if (cmd == "Longestpath") pipelineIndex = 3;
            else if (cmd == "Averdist") pipelineIndex = 4;

            pipeline[pipelineIndex]->enqueue([clientSock, localMst, cmd]() {
                std::string future;
                if (cmd == "MSTweight") {
                    future = "Total weight of the MST is: " + std::to_string(localMst->totalWeight()) + "\n";
                } else if (cmd == "Shortestpath") {
                    auto result = localMst->shortestPath();
                    future = "Shortest path in the MST is: " + result.second + " with distance: " + std::to_string(result.first) + "\n";
                } else if (cmd == "Longestpath") {
                    future = "Longest path in the MST is: " + std::to_string(localMst->diameter()) + "\n";
                } else if (cmd == "Averdist") {
                    future = "Average distance in the MST is: " + std::to_string(localMst->averageDistanceEdges()) + "\n";
                }
                sendResponse(clientSock, future);
            });

            executePipeline(pipeline);

        } else if (cmd == "Exit") {
            sendResponse(clientSock, "Goodbye\n");
            close(clientSock);
            std::lock_guard<std::mutex> lock(clientNumberMutex);  // Protect clientNumber
            clientNumber--;
            break;

        } else {
            sendResponse(clientSock, "Unknown command\n");
        }
    }
}



int acceptConnection(int serverSock) {
    struct sockaddr_in clientAddr;
    socklen_t sin_size = sizeof(clientAddr);
    int clientSock = accept(serverSock, (struct sockaddr *)&clientAddr, &sin_size);
    if (clientSock == -1) {
        perror("accept");
        return -1;
    }

    // Lock the mutex to protect clientNumber and the printing of the client count
    {
        std::lock_guard<std::mutex> lock(clientNumberMutex);
        clientNumber++;

        char s[INET6_ADDRSTRLEN];
        inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, s, sizeof s);

        // Lock the mutex to protect cout
        std::lock_guard<std::mutex> coutLock(coutMutex);
        cout << "New connection from " << s << " on socket " << clientSock << endl;
        cout << "Currently " << clientNumber << " clients connected" << endl;
    }

    return clientSock;
}


int main() {
    signal(SIGINT, signalHandler);
    vector<unique_ptr<ActiveObject>> pipeline;
    auto g = std::make_shared<Graph>();  // Shared graph for all clients
    auto mst = std::make_shared<Tree>(); // Shared MST for all clients
    MSTFactory factory;

    signalHandlerLambda = [&](int signum) {
        std::lock_guard<std::mutex> coutLock(coutMutex);  // Protect cout
        cout << "Freeing memory" << endl;
        g.reset();
        mst.reset();
        for (auto &obj : pipeline) {
            obj.reset();
        }
    };

    int serverSock;
    struct sockaddr_in serverAddr;
    int opt = 1;

    if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::lock_guard<std::mutex> coutLock(coutMutex);  // Protect cerr
        cerr << "Socket creation error" << endl;
        exit(1);
    }

    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::lock_guard<std::mutex> coutLock(coutMutex);  // Protect cerr
        cerr << "Setsockopt error" << endl;
        exit(1);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    memset(&(serverAddr.sin_zero), '\0', 8);

    if (bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        std::lock_guard<std::mutex> coutLock(coutMutex);  // Protect cerr
        cerr << "Bind error" << endl;
        exit(1);
    }

    if (listen(serverSock, 10) < 0) {
        std::lock_guard<std::mutex> coutLock(coutMutex);  // Protect cerr
        cerr << "Listen error" << endl;
        exit(1);
    }

    {
        std::lock_guard<std::mutex> coutLock(coutMutex);  // Protect cout
        cout << "MST pipeline server waiting for requests on port " << port << endl;
    }

    vector<std::thread> threads;  // Vector to keep track of threads

    while (true) {
        if (!serverRunning) {
            break;
        }

        int newClientSock = acceptConnection(serverSock);
        if (newClientSock == -1) {
            if (!serverRunning) {
                break;
            }
            continue;
        }

        threads.emplace_back([newClientSock, &pipeline, &g, &mst, &factory]() {
            handleCommands(newClientSock, pipeline, factory, g, mst);
        });
    }

    // Join all threads before exiting
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    signalHandlerLambda(SIGINT);

    close(serverSock);
    return 0;
}