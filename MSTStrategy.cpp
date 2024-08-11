#include "MSTStrategy.hpp"
#include <climits>
#include <algorithm>
#include <iostream>
#include <mutex>

// Global mutex to protect access to the Graph
std::mutex graphMutex;
std::mutex coutMutex;


// PrimStrategy implementation

int PrimStrategy::minKey(const std::vector<int>& key, const std::vector<char>& mstSet, int V) {
    int min = INT_MAX, min_index = -1;
    for (int v = 0; v < V; v++) {
        if (!mstSet[v] && key[v] < min) {
            min = key[v];
            min_index = v;
        }
    }
    return min_index;
}

std::vector<Edge> PrimStrategy::findMST(const Graph& g) {
    std::lock_guard<std::mutex> lock(graphMutex);  // Protect access to the Graph

    int V = g.getVerticesNumber();
    std::vector<Edge> result;
    std::vector<int> parent(V, -1); // Array to store the constructed MST
    std::vector<int> key(V, INT_MAX); // Key values used to pick minimum weight edge in cut
    std::vector<char> mstSet(V, 0); // To represent set of vertices not yet included in MST

    // Always include the first vertex in MST
    key[0] = 0;
    parent[0] = -1; // First node is always the root of MST

    // The MST will have V vertices
    for (int count = 0; count < V - 1; count++) {
        int u = minKey(key, mstSet, V); // Pick the minimum key vertex from the set of vertices not yet included in MST
        mstSet[u] = 1; // Add the picked vertex to the MST set

        // Update key value and parent index of the adjacent vertices of the picked vertex
        for (const Edge& e : g.getAdj()[u]) {
            int v = e.dest - 1;
            int weight = e.weight;
            if (!mstSet[v] && weight < key[v]) {
                parent[v] = u;
                key[v] = weight;
            }
        }
    }

    // Construct the result vector of edges
    for (int i = 1; i < V; i++) {
        result.push_back({parent[i] + 1, i + 1, key[i]});
    }

    return result;
}

// KruskalStrategy implementation

class DSU {
public:
    DSU(int n) : parent(n, -1), rank(n, 0) {}

    int find(int i);
    void unite(int x, int y);

private:
    std::vector<int> parent;
    std::vector<int> rank;
};

int DSU::find(int i) {
    if (parent[i] == -1) {
        return i;
    }
    return find(parent[i]);
}

void DSU::unite(int x, int y) {
    int s1 = find(x);
    int s2 = find(y);

    if (s1 != s2) {
        if (rank[s1] < rank[s2]) {
            parent[s1] = s2;
        } else if (rank[s1] > rank[s2]) {
            parent[s2] = s1;
        } else {
            parent[s2] = s1;
            rank[s1]++;
        }
    }
}

std::vector<Edge> KruskalStrategy::findMST(const Graph& g) {
    std::lock_guard<std::mutex> lock(graphMutex);  // Protect access to the Graph

    int V = g.getVerticesNumber();
    std::vector<Edge> result;
    int e = 0; // Number of edges in the MST
    size_t i = 0; // Index used for sorted edges

    // Get all edges from the graph
    std::vector<Edge> edges;
    for (int u = 0; u < V; u++) {
        for (const Edge& edge : g.getAdj()[u]) {
            if (u < edge.dest) { // Ensure each edge is added only once
                edges.push_back(edge);
            }
        }
    }

    // Sort all the edges in non-decreasing order of their weight
    std::sort(edges.begin(), edges.end(), [](Edge a, Edge b) {
        return a.weight < b.weight;
    });

    DSU dsu(V);

    // Iterate through sorted edges and apply union-find
    while (e < V - 1 && i < edges.size()) {
        Edge next_edge = edges[i++];

        int x = dsu.find(next_edge.src - 1);
        int y = dsu.find(next_edge.dest - 1);

        if (x != y) {
            result.push_back(next_edge);
            dsu.unite(x, y);
            e++;
        }
    }

    // Print the MST
    std::lock_guard<std::mutex> coutLock(coutMutex);  // Protect cout access
    std::cout << "Edges in the constructed MST\n";
    for (const auto& edge : result) {
        std::cout << edge.src << " -- " << edge.dest << " == " << edge.weight << std::endl;
    }
    return result;
}
