#ifndef TREE_HPP
#define TREE_HPP

#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
#include <string>
#include "Edge.hpp" // Include the Edge header file

using namespace std;

class Tree {
public:
    Tree() = default;
    Tree(const vector<Edge>& edges) { init(edges); } // Constructor that takes a vector of edges
    void init(vector<Edge> edges);
    int totalWeight();
    float averageDistanceEdges();
    ~Tree();  // Destructor declaration
    
    // Method for calculating the shortest path in the MST
    int diameter(); // Method for calculating the diameter of the MST
    void clear();
    void addEdge(int u, int v, int w);
    void removeEdge(int u, int v);
    string printMST();
    string printMST(int node, int parent, vector<bool> &visited);
    std::pair<int, std::string> shortestPath();
    int getVerticesNumber() const { return V; }
    const vector<vector<Edge>>& getAdj() const { return adj; 
    }
    


private:
    int V; // Number of vertices
    int E; // Number of edges
    vector<vector<Edge>> adj;
    vector<vector<int>> distanceMap;

    vector<int> dijkstra(int src, vector<int> &parentTrack);
    void dfs(int node, int parent, vector<int> &dist, vector<int> &parentTrack);
    void floydWarshall();
    string reconstructPath(int src, int dest, const vector<int> &parentTrack);
};  

#endif // TREE_HPP
