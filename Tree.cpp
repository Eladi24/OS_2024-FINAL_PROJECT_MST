#include "Tree.hpp"
#include <climits>
#include <iomanip>  // for std::setw

void Tree::init(vector<Edge> edges) {
    V = edges.size() + 1;
    E = 0;
    adj.resize(V);
    
    for (const Edge& e : edges) {
        addEdge(e.src, e.dest, e.weight);
    }
}

int Tree::totalWeight() {
    int total = 0;
    for (int i = 0; i < V; i++) {
        for (Edge e : adj[i]) {
            total += e.weight;
        }
    }
    return total / 2;
}

vector<int> Tree::dijkstra(int src, vector<int> &parentTrack) {
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> pq;
    vector<int> dist(V, INT_MAX);
    pq.push({0, src});
    dist[src - 1] = 0;
    parentTrack[src - 1] = -1;

    while (!pq.empty()) {
        int u = pq.top().second - 1;
        pq.pop();

        for (Edge e : adj[u]) {
            int v = e.dest - 1;
            int weight = e.weight;
            if (dist[v] > dist[u] + weight) {
                dist[v] = dist[u] + weight;
                pq.push({dist[v], v + 1});
                parentTrack[v] = u + 1;
            }
        }
    }
    return dist;
}

void Tree::dfs(int node, int parent, vector<int> &dist, vector<int> &parentTrack) {
    for (Edge e : adj[node - 1]) {
        if (e.dest != parent) {
            dist[e.dest - 1] = dist[node - 1] + e.weight;
            parentTrack[e.dest - 1] = node;
            dfs(e.dest, node, dist, parentTrack);
        }
    }
}

string Tree::reconstructPath(int src, int dest, const vector<int> &parentTrack) {
    int dest_index = dest - 1;
    if (parentTrack[dest_index] == -1) {
        return "No path";
    }

    vector<int> path;
    for (int i = dest_index; i != -1; i = parentTrack[i] - 1) {
        path.push_back(i + 1);
        if (i == src - 1) break;
    }

    if (path.back() != src) {
        return "No path";
    }

    reverse(path.begin(), path.end());
    string result;

    for (size_t i = 0; i < path.size(); i++) {
        result += to_string(path[i]);
        if (i < path.size() - 1) {
            result += " -> ";
        }
    }
    return result + "\n";
}

void Tree::floydWarshall() {
    distanceMap.resize(V, vector<int>(V, INT_MAX));
    for (int i = 0; i < V; i++) {
        distanceMap[i][i] = 0;
        for (Edge e : adj[i]) {
            distanceMap[i][e.dest - 1] = e.weight;
        }
    }

    for (int k = 0; k < V; k++) {
        for (int i = 0; i < V; i++) {
            for (int j = 0; j < V; j++) {
                if (distanceMap[i][k] != INT_MAX && distanceMap[k][j] != INT_MAX &&
                    distanceMap[i][j] > distanceMap[i][k] + distanceMap[k][j]) {
                    distanceMap[i][j] = distanceMap[i][k] + distanceMap[k][j];
                }
            }
        }
    }
}

float Tree::averageDistanceEdges() {
    floydWarshall();
    int total = 0;
    int count = 0;
    for (int i = 0; i < V; i++) {
        for (int j = i + 1; j < V; j++) {
            if (distanceMap[i][j] != INT_MAX) {
                total += distanceMap[i][j];
                count++;
            }
        }
    }
    return static_cast<float>(total) / count;
}


void Tree::addEdge(int u, int v, int w) {
    if (u < 0 || u > V || v < 0 || v > V) {
        cerr << "Invalid edge, vertices must be between 1 and " << V << endl;
        exit(1);
    }
    if (u == v) {
        cerr << "Invalid edge, vertices must be different" << endl;
        exit(1);
    }
    if (E >= V - 1) {
        cerr << "Invalid edge, number of edges must be V - 1 by definition" << endl;
        exit(1);
    }

    adj[u - 1].push_back({u, v, w});
    adj[v - 1].push_back({v, u, w});
    E++;
}

void Tree::removeEdge(int u, int v) {
    throw runtime_error("Removing edges is not allowed in a tree");
}

string Tree::printMST() {
    vector<bool> visited(V, false);
    string header = "\nMinimum Spanning Tree (MST):\n";
    header += "------------------------------------\n";
    header += "Node 1 -- Node 2 (Weight)\n";
    header += "------------------------------------\n";
    return header + printMST(0, -1, visited) + "\n";
}

string Tree::printMST(int node, int parent, vector<bool> &visited) {
    string result;
    visited[node] = true;

    for (auto& edge : adj[node]) {
        if (!visited[edge.dest - 1]) {
            result += to_string(node + 1) + " -- " + to_string(edge.dest) + " (" + to_string(edge.weight) + ")\n";
            result += printMST(edge.dest - 1, node, visited);
        }
    }
    return result;
}

std::pair<int, std::string> Tree::shortestPath() {
    floydWarshall();

    int shortest = INT_MAX;
    int start = -1;
    int end = -1;

    for (int i = 0; i < V; i++) {
        for (int j = i + 1; j < V; j++) {
            if (distanceMap[i][j] != INT_MAX && distanceMap[i][j] < shortest) {
                shortest = distanceMap[i][j];
                start = i + 1;  // Convert back to 1-based indexing
                end = j + 1;
            }
        }
    }

    if (start == -1 || end == -1) {
        return {INT_MAX, "No path"};
    }

    std::vector<int> parentTrack(V, -1);
    dijkstra(start, parentTrack);  // Populate parentTrack for the shortest path

    std::string path = reconstructPath(start, end, parentTrack);
    return {shortest, path};
}


int Tree::diameter() {
    auto bfs = [&](int start) {
        vector<int> dist(V, -1);
        queue<int> q;
        q.push(start);
        dist[start] = 0;
        int farthest = start;

        while (!q.empty()) {
            int u = q.front(); q.pop();
            for (const auto& edge : adj[u]) {
                int v = edge.dest - 1;
                if (dist[v] == -1) {
                    dist[v] = dist[u] + edge.weight;
                    q.push(v);
                    if (dist[v] > dist[farthest]) {
                        farthest = v;
                    }
                }
            }
        }

        return std::make_pair(farthest, dist[farthest]);
    };

    auto p1 = bfs(0); // Find the farthest node from node 0
    auto p2 = bfs(p1.first); // Find the farthest node from p1.first

    return p2.second; // The distance of p2 is the diameter of the tree
}