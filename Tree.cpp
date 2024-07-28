#include "Tree.hpp"

void Tree::init(vector<Edge> edges)
{
    V = edges.size() + 1;
    E = 0;
    adj.resize(V);

    for (Edge e : edges)
    {
        addEdge(e.src, e.dest, e.weight);
    }

    cout << "Tree initialized with " << V << " vertices and " << E << " edges" << endl;
}

int Tree::totalWeight()
{
    int total = 0;
    for (int i = 0; i < V / 2; i++)
    {
        for (Edge e : adj[i])
        {
            total += e.weight;
        }
    }
    return total;
}

vector<int> Tree::dijkstra(int src, vector<int> &parentTrack)
{
    // Create a priority queue to store vertices that are being preprocessed.
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> pq;
    // Create a vector for distances and initialize all distances as infinite (INT_MAX)
    vector<int> dist(V, INT_MAX);
    // Insert source itself in priority queue and initialize its distance as 0.
    pq.push({0, src});
    dist[src] = 0;
    parentTrack[src] = -1;

    // Looping till priority queue becomes empty (or all distances are not finalized)
    while (!pq.empty())
    {
        // The first vertex in pair is the minimum distance vertex, extract it from priority queue.
        int u = pq.top().second;
        pq.pop();

        // Loop through all adjacent of u
        for (Edge e : adj[u])
        {
            int v = e.dest;
            int weight = e.weight;
            // If there is a shorter path to v through u.
            if (dist[v] > dist[u] + weight)
            {
                // Updating distance of v
                dist[v] = dist[u] + weight;
                pq.push({dist[v], v});
                parentTrack[v] = u;
            }
        }
    }
    return dist;
}

void Tree::dfs(int node, int parent, vector<int> &dist, vector<int> &parentTrack)
{
    for (Edge e : adj[node])
    {
        if (e.dest != parent)
        {
            dist[e.dest] = dist[node] + e.weight;
            parentTrack[e.dest] = node;
            dfs(e.dest, node, dist, parentTrack);
        }
    }
}

string Tree::reconstructPath(int src, int dest, const vector<int> &parentTrack)
{
    if (parentTrack[dest] == -1)
    {
        return "No path";
    }

    vector<int> path;
    for (int i = dest; i != -1; i = parentTrack[i])
    {
        path.push_back(i);
    }
    reverse(path.begin(), path.end());
    string result;

    for (int i = 0; i < path.size(); i++)
    {
        result += to_string(path[i]);
        if (i > 0)
        {
            result += " -> ";
        }
        result += to_string(path[i]);
    }
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
            distanceMap[i][e.dest] = e.weight;
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

string Tree::shortestPath(int u, int v)
{
    vector<int> parentTrack(V);
    vector<int> dist = dijkstra(u, parentTrack);
    return reconstructPath(u, v, parentTrack);
}

string Tree::longestPath(int u, int v)
{
    // Distance vector to store the distance of each node from the source node
    vector<int> dist(V, INT_MAX);
    // Parent vector to store the parent of each node in the path
    vector<int> parentTrack(V, -1);
    // Distance of the source node from itself is 0
    dist[u] = 0;
    // Run DFS from the source node
    dfs(u, -1, dist, parentTrack);
    return reconstructPath(u, v, parentTrack);
}

void Tree::addEdge(int u, int v, int w)
{
    if (u < 0 || u >= V || v < 0 || v >= V)
    {
        cerr << "Invalid edge, vertices must be between 0 and " << V - 1 << endl;
        exit(1);
    }

    if (u == v)
    {
        cerr << "Invalid edge, vertices must be different" << endl;
        exit(1);
    }

    if (E >= V - 1)
    {
        cerr << "Invalid edge, number of edges must be V - 1 by definition" << endl;
        exit(1);
    }

    adj[u].push_back({u, v, w});
    adj[v].push_back({v, u, w});
    E++;
}

void Tree::removeEdge(int u, int v)
{
    throw runtime_error("Removing edges is not allowed in a tree");
}
