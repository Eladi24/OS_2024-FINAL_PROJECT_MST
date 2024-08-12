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
#include <atomic>  // For std::atomic
#include <errno.h>  // For checking EINTR
#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "LFThreadPool.hpp"
#include "Reactor.hpp"
#include "ConcreteEventHandler.hpp"

const int port = 4050;
std::atomic<int> clientNumber(0);
std::mutex graphMutex;  // Mutex to protect Graph access
std::mutex treeMutex;   // Mutex to protect Tree access
std::mutex coutMutex;   // Mutex to protect std::cout
std::mutex cerrMutex;   // Mutex to protect std::cerr
std::mutex threadVectorMutex;  // Mutex to protect access to the threads vector
std::shared_ptr<Reactor> reactor;
std::unique_ptr<LFThreadPool> pool;
std::vector<std::thread> threads;
std::atomic<bool> serverRunning(true);  // Atomic variable to prevent data races
std::mutex serverSockMutex;
int serverSock = -1;

void sendResponse(int clientSock, const std::string &response) {
    if (send(clientSock, response.c_str(), response.size(), 0) < 0) {
        std::lock_guard<std::mutex> lock(cerrMutex);
        std::cerr << "Send error" << std::endl;
    }
}

void handleCommands(int clientSock, std::shared_ptr<Graph> &g, MSTFactory &factory, std::shared_ptr<Tree> &mst) {
    char buffer[1024];

    while (true) {
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            {
                std::lock_guard<std::mutex> lock(cerrMutex);
                std::cerr << "recv error or connection closed" << std::endl;
            }
            close(clientSock);
            clientNumber--;
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
            if (!(ss >> n >> m) || n <= 0 || m < 0) {
                sendResponse(clientSock, "Invalid graph input\n");
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(graphMutex);
                g = std::make_shared<Graph>(n, m);
            }

            sendResponse(clientSock, "Graph created with " + std::to_string(n) + " vertices. Please send " + std::to_string(m) + " edges.\n");

            for (int i = 0; i < m; ++i) {
                bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
                if (bytesReceived <= 0) {
                    std::lock_guard<std::mutex> lock(cerrMutex);
                    std::cerr << "recv error or connection closed" << std::endl;
                    close(clientSock);
                    clientNumber--;
                    return;
                }

                buffer[bytesReceived] = '\0';
                std::stringstream edgeStream(buffer);
                int u, v, w;
                if (!(edgeStream >> u >> v >> w)) {
                    sendResponse(clientSock, "Invalid edge input\n");
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(graphMutex);
                    g->addEdge(u, v, w);
                }
            }

            sendResponse(clientSock, "Graph creation complete with " + std::to_string(m) + " edges.\n");

        } else if (cmd == "Prim" || cmd == "Kruskal") {
            {
                std::lock_guard<std::mutex> lock(graphMutex);
                if (!g || g->getAdj().empty()) {
                    sendResponse(clientSock, "Graph not initialized\n");
                    continue;
                }

                if (cmd == "Prim") {
                    factory.setStrategy(std::make_unique<PrimStrategy>());
                } else if (cmd == "Kruskal") {
                    factory.setStrategy(std::make_unique<KruskalStrategy>());
                }

                mst = factory.createMST(g);
            }

            sendResponse(clientSock, "MST created using " + cmd + "'s algorithm.\n" + mst->printMST());

        } else if (cmd == "MSTweight") {
            std::lock_guard<std::mutex> lock(treeMutex);
            if (!mst) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }
            sendResponse(clientSock, "Total weight of the MST is: " + std::to_string(mst->totalWeight()) + "\n");

        } else if (cmd == "LongestPath") {
            std::lock_guard<std::mutex> lock(treeMutex);
            if (!mst) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            auto [distance, path] = mst->shortestPath();
            sendResponse(clientSock, "Longest path length: " + std::to_string(distance) + "\nPath: " + path);

        } else if (cmd == "ShortestPath") {
            std::lock_guard<std::mutex> lock(treeMutex);
            if (!mst) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            auto [distance, path] = mst->shortestPath();
            sendResponse(clientSock, "Shortest path length: " + std::to_string(distance) + "\nPath: " + path);

        } else if (cmd == "AverDist") {
            std::lock_guard<std::mutex> lock(treeMutex);
            if (!mst) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            float avgDistance = mst->averageDistanceEdges();
            sendResponse(clientSock, "Average distance between edges in the MST is: " + std::to_string(avgDistance) + "\n");

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

std::function<void(int)> signalHandlerLambda;

void signalHandler(int signum) {
    if (signalHandlerLambda) {
        signalHandlerLambda(signum);
    }
}

int main() {
    signal(SIGINT, signalHandler);

    // Shared resources
    auto g = std::make_shared<Graph>();
    auto t = std::make_shared<Tree>();
    MSTFactory factory;

    std::vector<std::thread> threads;
    std::shared_ptr<ConcreteEventHandler> acceptHandler;

    signalHandlerLambda = [&](int signum) {
        {
            std::lock_guard<std::mutex> coutLock(coutMutex);
            std::cout << "Signal " << signum << " received, initiating shutdown..." << std::endl;
        }

        if (serverRunning.exchange(false)) {
            // Close server socket to stop accepting new connections
            {
                std::lock_guard<std::mutex> lock(serverSockMutex);
                if (serverSock >= 0) {
                    close(serverSock);
                    serverSock = -1;
                }
            }

            // Stop the reactor first to ensure no more events are processed
            if (reactor) {
                reactor->stop();
            }

            // Stop the thread pool to join all threads
            if (pool) {
                pool->stop();  // This will join all worker threads
                pool.reset();  // Ensure the pool is destroyed
            }

            // Join all additional threads
            {
                std::lock_guard<std::mutex> lock(threadVectorMutex);
                for (auto &thread : threads) {
                    if (thread.joinable()) {
                        thread.join();
                    }
                }
            }

            // Clean up resources
            g.reset();
            t.reset();

            if (acceptHandler) {
                reactor->removeHandler(acceptHandler, EventType::ACCEPT);
                acceptHandler.reset();
            }

            reactor.reset();  // Ensure reactor is destroyed
        }
        //exit(0);

        // Do not use std::exit(0) inside the signal handler; instead, let the program exit normally
    };

    // Server socket setup
    int opt = 1;
    serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return EXIT_FAILURE;
    }

    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(4050);  // Assuming port is 4050

    if (bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Bind error" << std::endl;
        return EXIT_FAILURE;
    }

    if (listen(serverSock, 10) < 0) {
        std::cerr << "Listen error" << std::endl;
        return EXIT_FAILURE;
    }

    {
        std::lock_guard<std::mutex> coutLock(coutMutex);
        std::cout << "Server listening on port " << ntohs(serverAddr.sin_port) << std::endl;
    }

    // Reactor and ThreadPool initialization
    reactor = std::make_shared<Reactor>();
    pool = std::make_unique<LFThreadPool>(10, reactor);

acceptHandler = std::make_shared<ConcreteEventHandler>(
    serverSock, [&, factoryPtr = &factory](int fd) mutable {
        struct sockaddr_in clientAddr;
        socklen_t sinSize = sizeof(clientAddr);
        while (serverRunning.load()) {
            int clientSock = accept(fd, (struct sockaddr *)&clientAddr, &sinSize);
            if (clientSock >= 0) {
                {
                    std::lock_guard<std::mutex> lock(coutMutex);
                    std::cout << "New connection on socket " << clientSock << std::endl;
                }

                {
                    std::lock_guard<std::mutex> lock(threadVectorMutex);
                    threads.emplace_back([clientSock, &g, &t, factoryPtr]() mutable {
                        handleCommands(clientSock, g, *factoryPtr, t);
                    });
                }
            } else if (errno == EINTR) {
                // `accept` was interrupted by a signal
                break;
            } else if (!serverRunning.load()) {
                // Server is shutting down, exit the loop
                break;
            } else {
                std::lock_guard<std::mutex> lock(cerrMutex);
                std::cerr << "Error accepting connection on fd: " << fd << std::endl;
                break;
            }
        }
    }
);


    reactor->registerHandler(acceptHandler, EventType::ACCEPT);
    pool->run();

    // Main loop to keep server running
    while (serverRunning.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup is done in the signal handler lambda
    return 0;  // Allow normal exit
}
