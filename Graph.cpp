#include "Graph.hpp"

void Graph::init() {
    adj.clear();
    adj.resize(V); // Ensure the adjacency list is resized to the number of vertices

    for (int i = 0; i < E; i++) {
        int u, v, w;

        if (!(std::cin >> u >> v >> w) || u < 1 || u > V || v < 1 || v > V) {
            std::cerr << "Invalid edge" << std::endl;
            exit(1); // Halt the process on invalid edge input
        }
        addEdge(u, v, w);
    }

    std::cout << "Graph initialized with " << V << " vertices and " << E << " edges" << std::endl;
}

void Graph::addEdge(int u, int v, int w) {
    if (u < 1 || u > V || v < 1 || v > V) {
        std::cerr << "Invalid edge, vertices must be between 1 and " << V << std::endl;
        return;
    }
    adj[u - 1].push_back({u, v, w});
    adj[v - 1].push_back({v, u, w});
}

void Graph::removeEdge(int u, int v) {
    u -= 1;
    v -= 1;

    adj[u].erase(std::remove_if(adj[u].begin(), adj[u].end(), [v](const Edge& e) {
        return e.dest - 1 == v;
    }), adj[u].end());

    adj[v].erase(std::remove_if(adj[v].begin(), adj[v].end(), [u](const Edge& e) {
        return e.dest - 1 == u;
    }), adj[v].end());
}

std::vector<std::vector<int>> Graph::getAdjMatrix() const {
    std::vector<std::vector<int>> adjMatrix(V, std::vector<int>(V, 0));

    for (int i = 0; i < V; i++) {
        for (const Edge& e : adj[i]) {
            adjMatrix[i][e.dest - 1] = e.weight;
        }
    }

    return adjMatrix;
}
