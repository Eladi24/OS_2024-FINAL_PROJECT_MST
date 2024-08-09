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

#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "ActiveObject.hpp"

using namespace std;

const int port = 4050;
function<void(int)> signalHandlerLambda;
int clientNumber = 0;

void signalHandler(int signum) {
    cout << "Interrupt signal (" << signum << ") received.\n";
    // Free memory
    signalHandlerLambda(signum);
    exit(signum);
}
void executePipeline(vector<unique_ptr<ActiveObject>> &pipeline) {
    for (auto &stage : pipeline) {
        stage->enqueue([]{});  // Ensure all tasks are enqueued
    }

    for (auto &stage : pipeline) {
        // The destructor of `ActiveObject` ensures all tasks complete before joining the thread
        stage.reset();
    }
}


int scanGraph(int &n, int &m, stringstream &ss, unique_ptr<Graph> &g) {
    if (!(ss >> n >> m) || n <= 0 || m < 0) {
        cerr << "Invalid graph input" << endl;
        return -1;
    }
    g = make_unique<Graph>(n, m);
    return 0;
}

int scanSrcDest(stringstream &ss, int &src, int &dest) {
    if (!(ss >> src >> dest) || src < 0 || dest < 0) {
        cerr << "Invalid source or destination" << endl;
        return -1;
    }
    return 0;
}

void sendResponse(int clientSock, const string &response) {
    if (send(clientSock, response.c_str(), response.size(), 0) < 0) {
        cerr << "send error" << endl;
    }
}

void setupPipeline(vector<unique_ptr<ActiveObject>> &pipeline, const string &algorithm) {
    pipeline.clear(); // Clear any previous pipeline setup

    if (algorithm == "Prim") {
        auto primAO = make_unique<ActiveObject>();
        pipeline.push_back(move(primAO));
    } else if (algorithm == "Kruskal") {
        auto kruskalAO = make_unique<ActiveObject>();
        pipeline.push_back(move(kruskalAO));
    }

    auto mstWeightAO = make_unique<ActiveObject>();
    auto shortestPathAO = make_unique<ActiveObject>();
    auto longestPathAO = make_unique<ActiveObject>();
    auto averageDistanceAO = make_unique<ActiveObject>();

    pipeline.push_back(move(mstWeightAO));
    pipeline.push_back(move(shortestPathAO));
    pipeline.push_back(move(longestPathAO));
    pipeline.push_back(move(averageDistanceAO));
}


void handleNewGraphCommand(int clientSock, stringstream &ss, unique_ptr<Graph> &g) {
    int n, m;
    if (scanGraph(n, m, ss, g) == -1) {
        sendResponse(clientSock, "Invalid graph input\n");
        return;
    }

    // Handle adding edges
    for (int i = 0; i < m; ++i) {
        int u, v, w;
        if (!(ss >> u >> v >> w)) {
            sendResponse(clientSock, "Invalid edge\n");
            return;
        }
        g->addEdge(u, v, w);
    }
    sendResponse(clientSock, "Graph created with " + to_string(n) + " vertices and " + to_string(m) + " edges.\n");
}
void handleCommands(int clientSock, vector<unique_ptr<ActiveObject>> &pipeline, unique_ptr<Graph> &g, MSTFactory &factory, unique_ptr<Tree> &mst) {
    char buffer[1024];
    while (true) {
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            clientNumber--;
            if (bytesReceived == 0) {
                cout << "Connection closed" << endl;
                break;
            } else {
                cerr << "recv error" << endl;
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
            int n, m;
            if (scanGraph(n, m, ss, g) == -1) {
                sendResponse(clientSock, "Invalid graph input\n");
                continue;
            }

            sendResponse(clientSock, "Graph created with " + to_string(n) + " vertices. Please send " + to_string(m) + " edges.\n");

            // Wait for m edges to be received from the client
            for (int i = 0; i < m; ++i) {
                bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
                if (bytesReceived <= 0) {
                    clientNumber--;
                    if (bytesReceived == 0) {
                        cout << "Connection closed" << endl;
                        break;
                    } else {
                        cerr << "recv error" << endl;
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
                g->addEdge(u, v, w);
            }

            sendResponse(clientSock, "Graph creation complete with " + to_string(m) + " edges.\n");
        } else if (cmd == "Prim") {
            if (g->getAdj().empty()) {
                sendResponse(clientSock, "Graph not initialized\n");
                continue;
            }
            setupPipeline(pipeline, "Prim");

            pipeline[0]->enqueue([&factory]() {
                factory.setStrategy(make_unique<PrimStrategy>());
            });

            pipeline[0]->enqueue([&g, &factory, &mst]() {
                mst = factory.createMST(g);
            });

            string future = "MST created using Prim's algorithm.\n";
            pipeline[0]->enqueue([&mst, &future]() {
                future += mst->printMST();
            });
            executePipeline(pipeline);
            sendResponse(clientSock, future);
        } else if (cmd == "Kruskal") {
            if (g->getAdj().empty()) {
                sendResponse(clientSock, "Graph not initialized\n");
                continue;
            }
            setupPipeline(pipeline, "Kruskal");

            pipeline[0]->enqueue([&factory]() {
                factory.setStrategy(make_unique<KruskalStrategy>());
            });

            pipeline[0]->enqueue([&g, &factory, &mst]() {
                mst = factory.createMST(g);
            });

            string future = "MST created using Kruskal's algorithm.\n";
            pipeline[0]->enqueue([&mst, &future]() {
                future += mst->printMST();
            });
            executePipeline(pipeline);
            sendResponse(clientSock, future);
        } else if (cmd == "MSTweight") {
            if (mst == nullptr) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            string future = "Total weight of the MST is: ";
            pipeline[1]->enqueue([&mst, &future]() {
                future += to_string(mst->totalWeight()) + "\n";
            });
            executePipeline(pipeline);
            sendResponse(clientSock, future);
        } else if (cmd == "Shortestpath") {
            if (mst == nullptr) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            string future = "Shortest path in the MST is: ";
            pipeline[2]->enqueue([&mst, &future]() {
                future += to_string(mst->shortestPath()) + "\n";
            });
            executePipeline(pipeline);
            sendResponse(clientSock, future);
        } else if (cmd == "Longestpath") {
            if (mst == nullptr) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            string future = "Longest path in the MST is: ";
            pipeline[3]->enqueue([&mst, &future]() {
                future += to_string(mst->diameter()) + "\n";
            });
            executePipeline(pipeline);
            sendResponse(clientSock, future);
        } else if (cmd == "Averdist") {
            if (mst == nullptr) {
                sendResponse(clientSock, "MST not created\n");
                continue;
            }

            string future = "Average distance in the MST is: ";
            pipeline[4]->enqueue([&mst, &future]() {
                future += to_string(mst->averageDistanceEdges()) + "\n";
            });
            executePipeline(pipeline);
            sendResponse(clientSock, future);
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


int acceptConnection(int serverSock) {
    struct sockaddr_in clientAddr;
    socklen_t sin_size = sizeof(clientAddr);
    int clientSock = accept(serverSock, (struct sockaddr *)&clientAddr, &sin_size);
    if (clientSock == -1) {
        perror("accept");
        return -1;
    }
    clientNumber++;
    char s[INET6_ADDRSTRLEN];
    inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, s, sizeof s);
    cout << "New connection from " << s << " on socket " << clientSock << endl;
    cout << "Currently " << clientNumber << " clients connected" << endl;
    return clientSock;
}

// Server main function
int main() {
    signal(SIGINT, signalHandler);
    vector<unique_ptr<ActiveObject>> pipeline;
    unique_ptr<Graph> g;
    MSTFactory factory;
    unique_ptr<Tree> mst;

    signalHandlerLambda = [&](int signum) {
        cout << "Freeing memory" << endl;
        mst.reset();
        g.reset();
        for (auto &obj : pipeline) {
            obj.reset();
        }
    };

    int serverSock;
    struct sockaddr_in serverAddr;
    int opt = 1;

    if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        cerr << "Socket creation error" << endl;
        exit(1);
    }

    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        cerr << "Setsockopt error" << endl;
        exit(1);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    memset(&(serverAddr.sin_zero), '\0', 8);

    if (bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Bind error" << endl;
        exit(1);
    }

    if (listen(serverSock, 10) < 0) {
        cerr << "Listen error" << endl;
        exit(1);
    }

    cout << "MST pipeline server waiting for requests on port " << port << endl;

    while (true) {
        int newClientSock = acceptConnection(serverSock);
        if (newClientSock == -1) {
            continue;
        }

        thread clientThread([newClientSock, &pipeline, &g, &factory, &mst]() {
            handleCommands(newClientSock, pipeline, g, factory, mst);
        });
        clientThread.detach();
    }

    close(serverSock);
    return 0;
}
