#include <iostream>
#include <vector>
#include <queue>
using namespace std;
class Graph {
    private:
        int V;
        int E;
        vector<vector<int>> adj;
    public:
        Graph(): V(0), E(0) {}
        void initGraph();
        void addEdge(int u, int v, int w);
        void removeEdge(int u, int v);
};