#include "Tree.hpp"
#include <queue>
#include <vector>
#include <string>
#include <algorithm>
#include <climits>
#include <iostream>

using namespace std;

void Tree::init(vector<Edge> edges)
{
    V = edges.size() + 1;
    E = 0;
    adj.resize(V);
    
    for (const Edge& e : edges)
    {
        addEdge(e.src, e.dest, e.weight);
    }
}

int Tree::totalWeight()
{
    int total = 0;
    for (int i = 0; i < V; i++)
    {
        for (Edge e : adj[i])
        {
            total += e.weight;
        }
    }
    return total / 2;
}

vector<int> Tree::dijkstra(int src, vector<int> &parentTrack)
{
    // Create a priority queue to store vertices that are being preprocessed.
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> pq;
    // Create a vector for distances and initialize all distances as infinite (INT_MAX)
    vector<int> dist(V, INT_MAX);
    // Insert source itself in priority queue and initialize its distance as 0.
    pq.push({0, src});
    dist[src - 1] = 0;
    parentTrack[src - 1] = -1;

    // Looping till priority queue becomes empty (or all distances are not finalized)
    while (!pq.empty())
    {
        // The first vertex in pair is the minimum distance vertex, extract it from priority queue.
        int u = pq.top().second - 1; // Convert to 0-based index
        pq.pop();

        // Loop through all adjacent of u
        for (Edge e : adj[u])
        {
            int v = e.dest - 1; // Convert to 0-based index
            int weight = e.weight;
            // If there is a shorter path to v through u.
            if (dist[v] > dist[u] + weight)
            {
                // Updating distance of v
                dist[v] = dist[u] + weight;
                pq.push({dist[v], v + 1}); // Store as 1-based index
                parentTrack[v] = u + 1; // Store parent as 1-based index
            }
        }
    }
    return dist;
}

void Tree::dfs(int node, int parent, vector<int> &dist, vector<int> &parentTrack)
{
    for (Edge e : adj[node - 1])
    {
        if (e.dest != parent)
        {
            dist[e.dest - 1] = dist[node - 1] + e.weight;
            parentTrack[e.dest - 1] = node;
            dfs(e.dest, node, dist, parentTrack);
        }
    }
}

string Tree::shortestPath()
{
    // Initialize minimum weight as maximum possible integer value
    int minWeight = INT_MAX;
    int u = -1, v = -1;

    // Find the edge with the least weight
    for (int i = 0; i < V; i++)
    {
        for (Edge e : adj[i])
        {
            if (e.weight < minWeight)
            {
                minWeight = e.weight;
                u = e.src;
                v = e.dest;
            }
        }
    }

    // If no valid edge was found, return no path
    if (u == -1 || v == -1)
    {
        return "No path found\n";
    }

    // Use the existing Dijkstra algorithm to find the shortest path between u and v
    vector<int> parentTrack(V, -1);
    vector<int> dist = dijkstra(u, parentTrack);

    // Return the reconstructed path
    return reconstructPath(u, v, parentTrack, dist[v - 1]);
}


string Tree::reconstructPath(int src, int dest, const vector<int> &parentTrack, int totalWeight)
{
    int dest_index = dest - 1; // Convert to 0-based index
    if (parentTrack[dest_index] == -1)
    {
        return "No path";
    }

    vector<int> path;
    for (int i = dest_index; i != -1; i = parentTrack[i] - 1)
    {
        path.push_back(i + 1); // Store as 1-based index
        if (i == src - 1) break;  // Stop if we have reached the source
    }

    if (path.back() != src)
    {
        return "No path";
    }

    reverse(path.begin(), path.end());
    string result;

    for (size_t i = 0; i < path.size(); i++)
    {
        result += to_string(path[i]);
        if (i < path.size() - 1)
        {
            result += " -> ";
        }
    }
    result += " (" + to_string(totalWeight) + ")\n";  // Include the total weight
    return result;
}


void Tree::floydWarshall()
{
    distanceMap.resize(V, vector<int>(V, INT_MAX));
    for (int i = 0; i < V; i++)
    {
        distanceMap[i][i] = 0;
        for (Edge e : adj[i])
        {
            distanceMap[i][e.dest - 1] = e.weight;
        }
    }

    for (int k = 0; k < V; k++)
    {
        for (int i = 0; i < V; i++)
        {
            for (int j = 0; j < V; j++)
            {
                if (distanceMap[i][k] != INT_MAX && distanceMap[k][j] != INT_MAX &&
                    distanceMap[i][j] > distanceMap[i][k] + distanceMap[k][j])
                {
                    distanceMap[i][j] = distanceMap[i][k] + distanceMap[k][j];
                }
            }
        }
    }
}

float Tree::averageDistanceEdges()
{
    floydWarshall();
    int total = 0;
    int count = 0;
    for (int i = 0; i < V; i++)
    {
        for (int j = i + 1; j < V; j++)
        {
            if (distanceMap[i][j] != INT_MAX)
            {
                total += distanceMap[i][j];
                count++;
            }
        }
    }
    
    return static_cast<float>(total) / count;
}


pair<int, int> Tree::farthestNode(int start)
{
    vector<int> dist(V, -1);
    vector<int> parentTrack(V, -1);
    dist[start - 1] = 0;

    dfs(start, -1, dist, parentTrack);

    int maxDist = -1;
    int farthest = start;
    for (int i = 0; i < V; ++i)
    {
        if (dist[i] > maxDist)
        {
            maxDist = dist[i];
            farthest = i + 1;
        }
    }

    return {farthest, maxDist};
}

int Tree::diameter()
{
    // Step 1: Find the farthest node from an arbitrary starting node (e.g., node 1)
    auto [farthestNodeFromStart, _] = farthestNode(1);

    // Step 2: Find the farthest node from the first farthest node found
    auto [farthestNodeFromFarthest, diameterLength] = farthestNode(farthestNodeFromStart);

    return diameterLength;
}

bool Tree::addEdge(int u, int v, int w)
{
    if (u < 1 || u > V || v < 1 || v > V)
    {
        cerr << "Invalid edge, vertices must be between 1 and " << V << endl;
        return false;
    }

    if (u == v)
    {
        cerr << "Invalid edge, vertices must be different" << endl;
        return false;
    }

    if (E >= V - 1)
    {
        cerr << "Invalid edge, number of edges must be V - 1 by definition" << endl;
        exit(1);
    }

    adj[u - 1].push_back({u, v, w});
    adj[v - 1].push_back({v, u, w});
    E++;
    return true;
}

void Tree::removeEdge(int u, int v)
{
    throw runtime_error("Removing edges is not allowed in a tree");
}

string Tree::printMST()
{
    vector<bool> visited(V, false);
    return printMST(0, -1, 0, visited) + "\n";
}

string Tree::printMST(int node, int parent, int level, vector<bool>& visited)
{
    string result;
    if (level == 0) {
        result += "------------------\n";
    }
    
    visited[node] = true;

    for (auto& edge : adj[node])
    {
        if (!visited[edge.dest - 1])
        {
            // Use indentation to visually represent tree levels
            string indentation(level * 4, ' '); // Use 4 spaces per level for clarity

            // Format the output with clearer connection symbols and descriptions
            result += indentation + "|- Node " + to_string(node + 1) + " -> Node " + to_string(edge.dest) + " [weight: " + to_string(edge.weight) + "]\n";
            
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




