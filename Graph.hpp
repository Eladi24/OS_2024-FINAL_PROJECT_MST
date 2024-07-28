#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
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
        virtual void init();
        virtual void addEdge(int u, int v, int w);
        virtual void removeEdge(int u, int v);
        int getVerticesNumber() const { return V; }
        vector<vector<Edge>> getAdj() const { return adj; }
        vector<vector<int>> getAdjMatrix() const;
        
};