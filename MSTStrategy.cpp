#include "MSTStrategy.hpp"

int PrimStrategy::minKey(vector<int> key, vector<bool> mstSet, int V)
{
    int min = INT_MAX, min_index;
    for (int v = 0; v < V; v++)
    {
        if (mstSet[v] == false && key[v] < min)
        {
            min = key[v];
            min_index = v;
        }
    }
    return min_index;
}

vector<Edge> PrimStrategy::findMST(const Graph& g)
{
    int V = g.getVerticesNumber();
    vector<Edge> result;
    // Array to store the constructed MST
    vector<int> parent(V);
    // Key values used to pick minimum weight edge in cut
    vector<int> key(V, INT_MAX);
    // To represent set of vertices not yet included in MST
    vector<bool> mstSet(V, false);

    // Always include first 1st vertex in MST.
    key[0] = 0;
    // First node is always root of MST
    parent[0] = -1;

    // The MST will have V vertices
    for (int count = 0; count < V - 1; count++)
    {
        // Pick the minimum key vertex from the set of vertices not yet included in MST
        int u = minKey(key, mstSet, V);

        // Add the picked vertex to the MST Set
        mstSet[u] = true;

        // Update key value and parent index of the adjacent vertices of the picked vertex.
        // Consider only those vertices which are not yet included in MST
        for (Edge e : g.getAdj()[u])
        {
            int v = e.dest;
            int weight = e.weight;
            if (mstSet[v] == false && weight < key[v])
            {
                parent[v] = u;
                key[v] = weight;
            }
        }
    }

    for (int i = 1; i < V; i++)
    {
        result.push_back({parent[i], i, key[i]});
    }

    return result;
}

int DSU::find(int i)
{
    if (parent[i] == -1)
    {
        return i;
    }
    return find(parent[i]);
}

void DSU::unite(int x, int y)
{
    int s1 = find(x);
    int s2 = find(y);

    if (s1 != s2)
    {
        if (rank[s1] < rank[s2])
        {
            parent[s1] = s2;
        }
        else if (rank[s1] > rank[s2])
        {
            parent[s2] = s1;
        }
        else
        {
            parent[s1] = s2;
            rank[s2]++;
        }
    }
}


vector<Edge> KruskalStrategy::findMST(const Graph& g) {
    int V = g.getVerticesNumber();
    vector<Edge> result;
    int e = 0; // Number of edges in the MST
    int i = 0; // Index used for sorted edges

    // Get all edges from the graph
    vector<Edge> edges;
    for (int u = 0; u < V; u++) {
        for (const Edge& edge : g.getAdj()[u]) {
            if (u < edge.dest) { // Ensure each edge is added only once
                edges.push_back(edge);
            }
        }
    }

    // Sort all the edges in non-decreasing order of their weight
    sort(edges.begin(), edges.end(), [](Edge a, Edge b) {
        return a.weight < b.weight;
    });

    DSU dsu(V);

    // Iterate through sorted edges and apply union-find
    while (e < V - 1 && i < edges.size()) {
        Edge next_edge = edges[i++];

        int x = dsu.find(next_edge.src);
        int y = dsu.find(next_edge.dest);

        if (x != y) {
            result.push_back(next_edge);
            dsu.unite(x, y);
            e++;
        }
    }

    // Print the MST
    cout << "Edges in the constructed MST\n";
    for (const auto& edge : result) {
        cout << edge.src << " -- " << edge.dest << " == " << edge.weight << endl;
    }
    return result;
}