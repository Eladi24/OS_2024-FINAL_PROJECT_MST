#ifndef GRAPH_HPP
#define GRAPH_HPP
#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
#include <sstream>
using namespace std;

typedef struct Edge {
    int src;
    int dest;
    int weight;
}Edge;

class Graph {
    protected:
        int V;
        int E;
        vector<vector<Edge>> adj;
    public:
        Graph(): V(0), E(0) {}
        Graph(int V, int E): V(V), E(E) { adj.resize(V); }
        virtual ~Graph() = default;
        virtual void init();
        virtual void addEdge(int u, int v, int w);
        virtual void removeEdge(int u, int v);
        int getVerticesNumber() const { return V; }
        const vector<vector<Edge>>& getAdj() const { return adj; }
        vector<vector<int>> getAdjMatrix() const;
        
};

#endif