#include "Graph.hpp"

void Graph::addEdge(int u, int v, int w)
{
    adj[u - 1].push_back({u, v, w});
    adj[v - 1].push_back({v, u, w});
}
