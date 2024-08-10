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
#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "LFThreadPool.hpp"
#include "Reactor.hpp"
#include "ConcreteEventHandler.hpp"
#include <climits>

const int port = 4050;
int clientNumber = 0;
std::mutex graphMutex;

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received.\n";
    exit(signum);
}

void sendResponse(int clientSock, const std::string &response) {
    if (send(clientSock, response.c_str(), response.size(), 0) < 0) {
        std::cerr << "Send error" << std::endl;
    }
}

int scanGraph(int clientSock, int &n, int &m, std::stringstream &ss, std::unique_ptr<Graph> &g) {
    char buffer[1024];
    if (!(ss >> n >> m) || n <= 0 || m < 0) {
        std::cerr << "Invalid graph input" << std::endl;
        return -1;
    }

    std::cout << "Creating graph with " << n << " vertices and " << m << " edges." << std::endl;

    g = std::make_unique<Graph>(n, m);
    for (int i = 0; i < m; i++) {
        int u, v, w;
        if (!(ss >> u >> v >> w)) {
            memset(buffer, 0, sizeof(buffer));
            int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0) {
                clientNumber--;
                std::cerr << "connection closed" << std::endl;
                return -1;
            }
            ss.clear();
            ss.str(buffer);
            ss >> u >> v >> w;
        }
        std::cout << "Adding edge: " << u << "-" << v << " with weight " << w << std::endl;
        g->addEdge(u, v, w);
    }
    return 0;
}

int scanSrcDest(std::stringstream &ss, int &src, int &dest) {
    if (!(ss >> src >> dest) || src < 0 || dest < 0) {
        std::cerr << "Invalid source or destination input" << std::endl;
        return -1;
    }
    return 0;
}

void handleCommands(int clientSock, std::unique_ptr<Graph> &g, MSTFactory &factory, std::unique_ptr<Tree> &mst) {
    char buffer[1024];
    while (true) {
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            clientNumber--;
            std::cerr << "recv error or connection closed" << std::endl;
            close(clientSock);
            return;
        }
        buffer[bytesReceived] = '\0';
        std::string command(buffer);

        std::stringstream ss(command);
        std::string cmd;
        ss >> cmd;

        std::lock_guard<std::mutex> lock(graphMutex);

        if (cmd == "Newgraph") {
            int n, m;
            if (scanGraph(clientSock, n, m, ss, g) == -1) {
                sendResponse(clientSock, "Invalid graph input\n");
                continue;
            }
            sendResponse(clientSock, "Graph created with " + std::to_string(n) + " vertices and " + std::to_string(m) + " edges.\n");

        } else if (cmd == "Prim") {
            if (!g || g->getAdj().empty()) {
                sendResponse(clientSock, "Graph not initialized\n");
                continue;
            }
            factory.setStrategy(std::make_unique<PrimStrategy>());
            mst = factory.createMST(g);
            sendResponse(clientSock, "MST created using Prim's algorithm.\n" + mst->printMST());

        } else if (cmd == "Kruskal") {
            if (!g || g->getAdj().empty()) {
                sendResponse(clientSock, "Graph not initialized\n");
                continue;
            }
            factory.setStrategy(std::make_unique<KruskalStrategy>());
            mst = factory.createMST(g);
            sendResponse(clientSock, "MST created using Kruskal's algorithm.\n" + mst->printMST());

        } else if (cmd == "MSTweight") {
            if (!mst) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }
            sendResponse(clientSock, "Total weight of the MST is: " + std::to_string(mst->totalWeight()) + "\n");

     } else if (cmd == "Shortestpath") {
           if (!mst) {
        sendResponse(clientSock, "MST not created\n");
        continue;
    }

    auto result = mst->shortestPath();
    int shortestDistance = result.first;
    std::string shortestPath = result.second;

    sendResponse(clientSock, "Shortest path in MST is: " + shortestPath + " with distance: " + std::to_string(shortestDistance) + "\n");

        } else if (cmd == "Longestpath") {
    if (!mst) {
        sendResponse(clientSock, "MST not created\n");
        continue;
    }

    // Get the diameter and the path
    int longestDistance = mst->diameter();

    if (longestDistance == INT_MAX) {
        sendResponse(clientSock, "Error: \n");
    } else {
        // Send the response with both the length of the longest path and the actual path
        sendResponse(clientSock, "Longest path in the MST is: " + std::to_string(longestDistance) + 
                     "\n");
    }
}
else if (cmd == "Averdist") {
            if (!mst) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }
            sendResponse(clientSock, "Average distance in the MST is: " + std::to_string(mst->averageDistanceEdges()) + "\n");

        } else if (cmd == "Exit") {
            sendResponse(clientSock, "Goodbye\n");
            close(clientSock);
            clientNumber--;
            break;

        } else {
            sendResponse(clientSock, "Unknown command\n");
        }
    }
}

int main() {
    signal(SIGINT, signalHandler);

    std::shared_ptr<Reactor> reactor = std::make_shared<Reactor>();
    LFThreadPool pool(10, reactor);

    int serverSock;
    struct sockaddr_in serverAddr;
    int opt = 1;

    serverSock = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(serverSock, 10);

    std::cout << "Server listening on port " << port << std::endl;

    auto g = std::make_unique<Graph>();  // Create a unique_ptr for Graph
    auto t = std::make_unique<Tree>();   // Create a unique_ptr for Tree
    MSTFactory factory;

    auto acceptHandler = std::make_shared<ConcreteEventHandler>(
        serverSock, [&reactor, &g, &t, &factory](int fd) {
            struct sockaddr_in clientAddr;
            socklen_t sinSize = sizeof(clientAddr);
            int clientSock = accept(fd, (struct sockaddr *)&clientAddr, &sinSize);
            if (clientSock >= 0) {
                std::cout << "New connection on socket " << clientSock << std::endl;

                std::thread([clientSock, &g, &t, &factory]() {
                    handleCommands(clientSock, g, factory, t);
                }).detach();
            } else {
                std::cerr << "Error accepting connection on fd: " << fd << std::endl;
            }
        }
    );

    reactor->registerHandler(acceptHandler, EventType::ACCEPT);

    pool.run();
}
