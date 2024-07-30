#ifndef TREE_HPP
#define TREE_HPP
#include <limits.h>

#include "Graph.hpp"
#include <queue>
#include <utility>

class Tree: public Graph
{
    private:
        vector<vector<int>> distanceMap;
        void dfs(int node, int parent, vector<int>& dist, vector<int>& parentTrack);
        vector<int> dijkstra(int src, vector<int>& parentTrack);
        string reconstructPath(int src, int dest, const vector<int>& parentTrack);
        void floydWarshall();
        void init(vector<Edge> edges);
        string printMST(int node, int parent, int level, vector<bool>& visited);

    public:    
        Tree(): Graph() {}
        Tree(vector<Edge> edges): Graph() { init(edges); }
        int totalWeight();
        float averageDistanceEdges();
        
        string shortestPath(int u, int v);
        string longestPath(int u, int v);
        void addEdge(int u, int v, int w) override;
        void removeEdge(int u, int v) override;
        string printMST();

};

#endif