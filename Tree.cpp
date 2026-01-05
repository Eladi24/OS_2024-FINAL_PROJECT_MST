#include "Tree.hpp"
#include <algorithm>
#include <climits>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

void Tree::init(vector<Edge> edges) {
  V = edges.size() + 1;
  E = 0;
  adj.resize(V);

  for (const Edge &e : edges) {
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

void Tree::dfs(int node, int parent, vector<int> &dist) {
  for (Edge e : adj[node - 1]) {
    if (e.dest != parent) {
      dist[e.dest - 1] = dist[node - 1] + e.weight;
      dfs(e.dest, node, dist);
    }
  }
}

string Tree::shortestPath() {
  // Find the edge with the minimum weight
  // Since u and v are directly connected by this edge, the path is just u -> v
  int minWeight = INT_MAX;
  int u = -1, v = -1;

  for (int i = 0; i < V; i++) {
    for (Edge e : adj[i]) {
      if (e.weight < minWeight) {
        minWeight = e.weight;
        u = e.src;
        v = e.dest;
      }
    }
  }

  // If no valid edge was found, return no path
  if (u == -1 || v == -1) {
    return "No path found\n";
  }

  // The path is just the single edge: u -> v with weight minWeight
  return to_string(u) + " -> " + to_string(v) + " (" + to_string(minWeight) +
         ")\n";
}

void Tree::computeAllPairsDistances() {
  // For trees, we use DFS from each vertex (O(V²)) 

  distanceMap.resize(V, vector<int>(V, INT_MAX));

  // For each vertex, do a DFS to find distances to all other vertices
  for (int i = 0; i < V; i++) {
    distanceMap[i][i] = 0;
    vector<int> dist(V, -1);
    dist[i] = 0;

    // Use the existing DFS method to compute distances from vertex i+1
    // (1-based)
    dfs(i + 1, -1, dist);

    // Copy distances to distanceMap
    for (int j = 0; j < V; j++) {
      if (dist[j] != -1) {
        distanceMap[i][j] = dist[j];
      }
    }
  }
}

float Tree::averageDistanceEdges() {
  computeAllPairsDistances();
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

pair<int, int> Tree::farthestNode(int start) {
  vector<int> dist(V, -1);
  dist[start - 1] = 0;

  dfs(start, -1, dist);

  int maxDist = -1;
  int farthest = start;
  for (int i = 0; i < V; ++i) {
    if (dist[i] > maxDist) {
      maxDist = dist[i];
      farthest = i + 1;
    }
  }

  return {farthest, maxDist};
}

int Tree::diameter() {
  // Step 1: Find the farthest node from an arbitrary starting node (e.g., node
  // 1)
  auto [farthestNodeFromStart, _] = farthestNode(1);

  // Step 2: Find the farthest node from the first farthest node found
  auto [farthestNodeFromFarthest, diameterLength] =
      farthestNode(farthestNodeFromStart);

  return diameterLength;
}

bool Tree::addEdge(int u, int v, int w) {
  if (u < 1 || u > V || v < 1 || v > V) {
    cerr << "Invalid edge, vertices must be between 1 and " << V << endl;
    return false;
  }

  if (u == v) {
    cerr << "Invalid edge, vertices must be different" << endl;
    return false;
  }

  if (E >= V - 1) {
    cerr << "Invalid edge, number of edges must be V - 1 by definition" << endl;
    exit(1);
  }

  adj[u - 1].push_back({u, v, w});
  adj[v - 1].push_back({v, u, w});
  E++;
  return true;
}

void Tree::removeEdge(int u, int v) {
  throw runtime_error("Removing edges is not allowed in a tree");
}

string Tree::printMST() {
  vector<bool> visited(V, false);
  return printMST(0, -1, 0, visited) + "\n";
}

string Tree::printMST(int node, int parent, int level, vector<bool> &visited) {
  string result;
  if (level == 0) {
    result += "------------------\n";
  }

  visited[node] = true;

  for (auto &edge : adj[node]) {
    if (!visited[edge.dest - 1]) {
      // Use indentation to visually represent tree levels
      string indentation(level * 4, ' '); // Use 4 spaces per level for clarity

      // Format the output with clearer connection symbols and descriptions
      result += indentation + "|- Node " + to_string(node + 1) + " -> Node " +
                to_string(edge.dest) + " [weight: " + to_string(edge.weight) +
                "]\n";

      // Add an extra blank line for more spacing between connections
      result += "\n";

      // Recursively print the subtree with incremented level
      result += printMST(edge.dest - 1, node, level + 1, visited);
    }
  }

  if (level == 0) {
    result += "--------------------\n";
  }

  return result;
}
