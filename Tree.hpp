#ifndef TREE_HPP
#define TREE_HPP

#include <limits.h>
#include "Graph.hpp"
#include <queue>
#include <utility>
#include <vector>

class Tree : public Graph
{
    private:
        vector<vector<int>> distanceMap; ///< Stores distances between all pairs of vertices for the Floyd-Warshall algorithm.

        /**
         * @brief Performs a depth-first search (DFS) to calculate distances from a node.
         * 
         * This function calculates the distance of all nodes from a given
         * starting node using DFS and updates the distance and parent tracking vectors.
         * 
         * @param node The current node being visited.
         * @param parent The parent node of the current node.
         * @param dist A reference to a vector that stores distances from the start node.
         * @param parentTrack A reference to a vector that tracks the parents of each vertex.
         */
        void dfs(int node, int parent, vector<int>& dist, vector<int>& parentTrack);

        /**
         * @brief Finds the farthest node from a given starting node.
         * 
         * This function performs a DFS from a given starting node and returns
         * the farthest node found and the distance to that node.
         * 
         * @param start The starting node.
         * @return pair<int, int> A pair containing the farthest node and its distance.
         */
        pair<int, int> farthestNode(int start);

        /**
         * @brief Implements Dijkstra's algorithm to find the shortest paths from a source vertex.
         * 
         * This function computes the shortest path from a given source vertex to all other vertices
         * using Dijkstra's algorithm. It returns the distance vector and updates the parent tracking vector.
         * 
         * @param src The source vertex.
         * @param parentTrack A reference to a vector that tracks the parents of each vertex.
         * @return vector<int> The vector of shortest distances from the source vertex.
         */
        vector<int> dijkstra(int src, vector<int> &parentTrack);

        /**
         * @brief Implements Floyd-Warshall algorithm to compute all-pairs shortest paths.
         * 
         * This function calculates the shortest paths between all pairs of vertices in the tree
         * and stores the results in a distance matrix.
         */
        void floydWarshall();

        /**
         * @brief Initializes the Tree with a given set of edges.
         * 
         * This function sets up the tree by adding all the edges provided
         * in the input vector to the adjacency list.
         * 
         * @param edges A vector of edges to initialize the tree.
         */
        void init(vector<Edge> edges);

        /**
         * @brief Helper function to recursively print the MST.
         * 
         * This function recursively traverses the tree to generate a structured representation
         * of the MST, with indentation to show the levels of the tree.
         * 
         * @param node The current node being visited.
         * @param parent The parent node of the current node.
         * @param level The current depth level in the tree.
         * @param visited A reference to a vector tracking visited nodes.
         * @return string A formatted string representing the MST.
         */
        string printMST(int node, int parent, int level, vector<bool>& visited);

    public:
        /**
         * @brief Default constructor for the Tree class.
         */
        Tree(): Graph() {}

        /**
         * @brief Parameterized constructor for the Tree class.
         * 
         * Initializes the tree with a given set of edges.
         * 
         * @param edges A vector of edges to initialize the tree.
         */
        Tree(vector<Edge> edges): Graph() { init(edges); }

        /**
         * @brief Calculates the total weight of the tree.
         * 
         * This function sums the weights of all edges in the tree and returns
         * the total weight. The sum is divided by 2 since each edge is counted twice
         * in the adjacency list.
         * 
         * @return int The total weight of the tree.
         */
        int totalWeight();

        /**
         * @brief Calculates the average distance between all pairs of vertices in the tree.
         * 
         * This function uses the results from the Floyd-Warshall algorithm to calculate
         * the average distance between all pairs of vertices that are reachable from each other.
         * 
         * @return float The average distance between all pairs of vertices.
         */
        float averageDistanceEdges();

        /**
         * @brief Calculates the diameter of the tree.
         * 
         * The diameter of the tree is the longest shortest path between any two vertices.
         * This function calculates the diameter by finding the farthest node from an arbitrary
         * starting node, and then finding the farthest node from that node.
         * 
         * @return int The diameter of the tree.
         */
        int diameter();

  /**
 * @brief Finds the shortest path based on the least weighted edge in the tree.
 * 
 * This function automatically finds the least weighted edge in the tree and returns 
 * a formatted string representing the path between the two vertices connected by the 
 * least weighted edge and its total weight.
 * 
 * @return string A formatted string representing the shortest path and its total weight.
 */
string shortestPath();


        /**
         * @brief Reconstructs the shortest path from source to destination.
         * 
         * This function backtracks using the parent tracking vector to reconstruct the shortest
         * path from the source to the destination vertex. It returns a formatted string that
         * includes the path and its total weight.
         * 
         * @param src The source vertex.
         * @param dest The destination vertex.
         * @param parentTrack The vector tracking the parent of each vertex.
         * @param totalWeight The total weight of the path.
         * @return string A formatted string representing the shortest path and its total weight.
         */
        string reconstructPath(int src, int dest, const vector<int> &parentTrack, int totalWeight);

        /**
         * @brief Adds an edge to the tree.
         * 
         * This function adds an edge between two vertices in the tree. It validates that the
         * vertices are within the correct range and that the number of edges does not exceed V-1.
         * 
         * @param u The source vertex.
         * @param v The destination vertex.
         * @param w The weight of the edge.
         */
        void addEdge(int u, int v, int w) override;

        /**
         * @brief Throws an error if an attempt is made to remove an edge.
         * 
         * Removing edges is not allowed in a tree, so this function always throws an exception.
         * 
         * @param u The source vertex.
         * @param v The destination vertex.
         */
        void removeEdge(int u, int v);

        /**
         * @brief Prints the minimum spanning tree (MST) in a structured format.
         * 
         * This function returns a string representation of the MST, where each node and its
         * connections are indented to reflect the tree structure.
         * 
         * @return string A formatted string representing the MST.
         */
        string printMST();
};

#endif
