#include "Graph.hpp"

void Graph::initGraph()
{
    cin >> V >> E;
    if (V <= 0 || E <= 0)
    {
        cerr << "Invalid number of vertices or edges" << endl;
        exit(1);
    }

    adj.resize(V);

    for (int i = 0; i < E; i++)
    {
        int u, v, w;
        cin >> u >> v >> w;
        if (u < 0 || u >= V || v < 0 || v >= V)
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
    adj[u - 1][v - 1] = w;
    adj[v - 1][u - 1] = w;
}

void Graph::removeEdge(int u, int v)
{
    adj[u - 1][v - 1] = 0;
    adj[v - 1][u - 1] = 0;
}
