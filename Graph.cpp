#include "Graph.hpp"

void Graph::init()
{
    
    adj.resize(V);

    for (int i = 0; i < E; i++)
    {
        int u, v, w;
        
        if (!(cin >> u >> v >> w) || u < 0 || u > V || v < 0 || v > V)
        {
            cerr << "Invalid edge" << endl;
            exit(1);
        }
        addEdge(u, v, w);
    }

    cout << "Graph initialized with " << V << " vertices and " << E << " edges" << endl;
}

void Graph::addEdge(int u, int v, int w)
{
    adj[u - 1].push_back({u, v, w});
    adj[v - 1].push_back({v, u, w});
}

void Graph::removeEdge(int u, int v) {
        // Remove edge u -> v
        adj[u].erase(remove_if(adj[u].begin(), adj[u].end(), [v](const Edge& e) {
            return e.dest == v;
        }), adj[u].end());

        // Remove edge v -> u
        adj[v].erase(remove_if(adj[v].begin(), adj[v].end(), [u](const Edge& e) {
            return e.dest == u;
        }), adj[v].end());
    }

vector<vector<int>> Graph::getAdjMatrix() const
{
    vector<vector<int>> adjMatrix(V, vector<int>(V, 0));

    for (int i = 0; i < V; i++)
    {
        for (Edge e : adj[i])
        {
            adjMatrix[i][e.dest] = e.weight;
        }
    }

    return adjMatrix;
}
