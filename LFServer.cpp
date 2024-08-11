#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <csignal>
#include <iostream>
#include <memory>
#include <string.h>
#include <mutex>
#include <vector>
#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "LFThreadPool.hpp"
#include "Reactor.hpp"
#include "ConcreteEventHandler.hpp"
#include <climits>

const int port = 4050;
int clientNumber = 0;
std::mutex graphMutex; // Mutex to protect Graph access
std::mutex treeMutex;  // Mutex to protect Tree access
std::mutex clientNumberMutex; // Mutex to protect clientNumber access
std::mutex coutMutex;  // Mutex to protect std::cout
std::mutex cerrMutex;  // Mutex to protect std::cerr
std::shared_ptr<Reactor> reactor;
std::unique_ptr<LFThreadPool> pool;
std::vector<std::thread> threads;

int serverSock;

void signalHandler(int signum) {
    {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cout << "\nInterrupt signal (" << signum << ") received.\n";
    }

    // Stop reactor
    if (reactor) {
        reactor->stop();
    }

    // Stop thread pool
    if (pool) {
        pool->stop();
    }

    // Join all threads before exiting
    for (auto &t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Explicitly reset shared pointers to ensure cleanup
    reactor.reset();
    pool.reset();
    //acceptHandler.reset();  // Reset the ConcreteEventHandler shared pointer

    // Close server socket
    if (serverSock >= 0) {
        close(serverSock);
        serverSock = -1;
    }

    // Exit the program
    exit(signum);
}

void sendResponse(int clientSock, const std::string &response) {
    if (send(clientSock, response.c_str(), response.size(), 0) < 0) {
        std::lock_guard<std::mutex> lock(cerrMutex);
        std::cerr << "Send error" << std::endl;
    }
}

int scanGraph(int clientSock, int &n, int &m, std::stringstream &ss, std::shared_ptr<Graph> &g) {
    if (!(ss >> n >> m) || n <= 0 || m < 0) {
        std::lock_guard<std::mutex> lock(cerrMutex);
        std::cerr << "Invalid graph input" << std::endl;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cout << "Creating graph with " << n << " vertices and " << m << " edges." << std::endl;
    }

    // Create the new graph object first
    std::shared_ptr<Graph> newGraph = std::make_shared<Graph>(n, m);

    {
        std::lock_guard<std::mutex> lock(graphMutex);
        g = newGraph;
    }

    for (int i = 0; i < m; i++) {
        char buffer[1024];
        int u, v, w;

        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            {
                std::lock_guard<std::mutex> lock(clientNumberMutex);
                clientNumber--;
            }
            {
                std::lock_guard<std::mutex> lock(cerrMutex);
                std::cerr << "Connection closed or recv error" << std::endl;
            }
            return -1;
        }

        buffer[bytesReceived] = '\0';
        std::stringstream edgeStream(buffer);

        if (!(edgeStream >> u >> v >> w)) {
            std::lock_guard<std::mutex> lock(cerrMutex);
            std::cerr << "Invalid edge input" << std::endl;
            sendResponse(clientSock, "Invalid edge input\n");
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(coutMutex);
            std::cout << "Adding edge: " << u << "-" << v << " with weight " << w << std::endl;
        }

        {
            std::lock_guard<std::mutex> lock(graphMutex);
            g->addEdge(u, v, w);
        }
    }

    return 0;
}

std::mutex scanSrcDestMutex;

int scanSrcDest(std::stringstream &ss, int &src, int &dest) {
    std::lock_guard<std::mutex> lock(scanSrcDestMutex);

    if (!(ss >> src >> dest) || src < 0 || dest < 0) {
        std::lock_guard<std::mutex> lock(cerrMutex);
        std::cerr << "Invalid source or destination input" << std::endl;
        return -1;
    }
    return 0;
}

void handleCommands(int clientSock, std::shared_ptr<Graph> &g, MSTFactory &factory, std::shared_ptr<Tree> &mst) {
    char buffer[1024];

    while (true) {
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            std::lock_guard<std::mutex> lock(clientNumberMutex);
            clientNumber--;
            {
                std::lock_guard<std::mutex> lock(cerrMutex);
                std::cerr << "recv error or connection closed" << std::endl;
            }
            close(clientSock);
            return;
        }

        buffer[bytesReceived] = '\0';
        std::string command(buffer);
        {
            std::lock_guard<std::mutex> lock(coutMutex);
            std::cout << "Received command: " << command << std::endl;
        }

        std::stringstream ss(command);
        std::string cmd;
        ss >> cmd;

        if (cmd == "Newgraph") {
            int n, m;
            if (scanGraph(clientSock, n, m, ss, g) == -1) {
                sendResponse(clientSock, "Invalid graph input\n");
                continue;
            }
            sendResponse(clientSock, "Graph created with " + std::to_string(n) + " vertices and " + std::to_string(m) + " edges.\n");

        } else if (cmd == "Prim") {
            {
                std::lock_guard<std::mutex> lock(graphMutex);
                if (!g || g->getAdj().empty()) {
                    sendResponse(clientSock, "Graph not initialized\n");
                    continue;
                }
            }

            factory.setStrategy(std::make_unique<PrimStrategy>());
            {
                std::lock_guard<std::mutex> lock(treeMutex);
                mst = factory.createMST(g);
            }
            sendResponse(clientSock, "MST created using Prim's algorithm.\n" + mst->printMST());

        } else if (cmd == "Kruskal") {
            {
                std::lock_guard<std::mutex> lock(graphMutex);
                if (!g || g->getAdj().empty()) {
                    sendResponse(clientSock, "Graph not initialized\n");
                    continue;
                }
            }

            factory.setStrategy(std::make_unique<KruskalStrategy>());
            {
                std::lock_guard<std::mutex> lock(treeMutex);
                mst = factory.createMST(g);
            }
            sendResponse(clientSock, "MST created using Kruskal's algorithm.\n" + mst->printMST());

        } else if (cmd == "MSTweight") {
            {
                std::lock_guard<std::mutex> lock(treeMutex);
                if (!mst) {
                    sendResponse(clientSock, "MST not created\n");
                    continue;
                }
            }
            sendResponse(clientSock, "Total weight of the MST is: " + std::to_string(mst->totalWeight()) + "\n");

        } else if (cmd == "LongestPath") {
            {
                std::lock_guard<std::mutex> lock(treeMutex);
                if (!mst) {
                    sendResponse(clientSock, "MST not created\n");
                    continue;
                }
            }
            auto [distance, path] = mst->shortestPath();
            sendResponse(clientSock, "Longest path length: " + std::to_string(distance) + "\nPath: " + path);

        } else if (cmd == "ShortestPath") {
            {
                std::lock_guard<std::mutex> lock(treeMutex);
                if (!mst) {
                    sendResponse(clientSock, "MST not created\n");
                    continue;
                }
            }
            auto [distance, path] = mst->shortestPath();
            sendResponse(clientSock, "Shortest path length: " + std::to_string(distance) + "\nPath: " + path);

        } else if (cmd == "AverDist") {
            {
                std::lock_guard<std::mutex> lock(treeMutex);
                if (!mst) {
                    sendResponse(clientSock, "MST not created\n");
                    continue;
                }
            }
            float avgDistance = mst->averageDistanceEdges();
            sendResponse(clientSock, "Average distance between edges in the MST is: " + std::to_string(avgDistance) + "\n");

        } else if (cmd == "Exit") {
            sendResponse(clientSock, "Goodbye\n");
            close(clientSock);
            {
                std::lock_guard<std::mutex> lock(clientNumberMutex);
                clientNumber--;
            }
            break;

        } else {
            sendResponse(clientSock, "Unknown command\n");
        }
    }
}

  auto g = std::make_shared<Graph>();
    auto t = std::make_shared<Tree>();
int main() {
    signal(SIGINT, signalHandler);

    reactor = std::make_shared<Reactor>();
    pool = std::make_unique<LFThreadPool>(10, reactor);

    serverSock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(serverSock, 10);

    {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cout << "Server listening on port " << port << std::endl;
    }

    auto g = std::make_shared<Graph>();
    auto t = std::make_shared<Tree>();
    MSTFactory factory;

    auto acceptHandler = std::make_shared<ConcreteEventHandler>(
        serverSock, [&, factoryPtr = &factory](int fd) mutable {
            struct sockaddr_in clientAddr;
            socklen_t sinSize = sizeof(clientAddr);
            int clientSock = accept(fd, (struct sockaddr *)&clientAddr, &sinSize);
            if (clientSock >= 0) {
                {
                    std::lock_guard<std::mutex> lock(coutMutex);
                    std::cout << "New connection on socket " << clientSock << std::endl;
                }

                // Create a thread and store it for later joining
                threads.emplace_back([clientSock, &g, &t, factoryPtr]() mutable {
                    handleCommands(clientSock, g, *factoryPtr, t);
                });
            } else {
                std::lock_guard<std::mutex> lock(cerrMutex);
                std::cerr << "Error accepting connection on fd: " << fd << std::endl;
            }
        }
    );

    reactor->registerHandler(acceptHandler, EventType::ACCEPT);
    
    pool->run();
acceptHandler.reset();

    // Clean up before exiting
    signalHandler(SIGINT);  // Mimic signal handling to clean up resources
t.reset();
g.reset();
    return 0;
}
