#include "Graph.hpp"

void Graph::addEdge(int u, int v, int w)
{   
    adj[u - 1].push_back({u, v, w});
    adj[v - 1].push_back({v, u, w});
}

bool Graph::removeEdge(int u, int v)
{
    // Get the initial size of the adjacency lists
    size_t initialSizeU = adj[u - 1].size();
    size_t initialSizeV = adj[v - 1].size();

    // Remove the edge (u, v) from the adjacency list of vertex u
    adj[u - 1].erase(remove_if(adj[u - 1].begin(), adj[u - 1].end(), [v](const Edge &e)
                               { return e.src == v || e.dest == v; }),
                     adj[u - 1].end());

    // Remove the edge (v, u) from the adjacency list of vertex v
    adj[v - 1].erase(remove_if(adj[v - 1].begin(), adj[v - 1].end(), [u](const Edge &e)
                               { return e.src == u || e.dest == u; }),
                     adj[v - 1].end());

    // Get the new sizes of the adjacency lists after removal
    size_t newSizeU = adj[u - 1].size();
    size_t newSizeV = adj[v - 1].size();

    // Return true if either of the adjacency lists changed, indicating successful removal
    if ((newSizeU < initialSizeU) || (newSizeV < initialSizeV))
    {
        E--;
        return true;
    }

    return false;
}
