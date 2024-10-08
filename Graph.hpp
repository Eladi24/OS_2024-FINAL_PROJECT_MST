#ifndef GRAPH_HPP
#define GRAPH_HPP

#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
#include <sstream>
using namespace std;

/**
 * @struct Edge
 * 
 * @brief Represents an edge in the graph.
 * 
 * This structure stores the source vertex, destination vertex, and the weight of the edge.
 */
typedef struct Edge {
    int src;    ///< Source vertex of the edge
    int dest;   ///< Destination vertex of the edge
    int weight; ///< Weight of the edge
} Edge;

/**
 * @class Graph
 * 
 * @brief Represents a graph using an adjacency list.
 * 
 * This class provides basic functionalities to create a graph, add edges, 
 * and access the graph's adjacency list.
 */
class Graph {
    protected:
        int V; ///< Number of vertices in the graph
        int E; ///< Number of edges in the graph
        vector<vector<Edge>> adj; ///< Adjacency list representing the graph
    
    public:
        /**
         * @brief Default constructor for the Graph class.
         * 
         * Initializes an empty graph with 0 vertices and 0 edges.
         */
        Graph(): V(0), E(0) {}

        /**
         * @brief Parameterized constructor for the Graph class.
         * 
         * Initializes a graph with a given number of vertices and edges.
         * 
         * @param V Number of vertices
         * @param E Number of edges
         */
        Graph(int V, int E): V(V), E(E) { adj.resize(V); }

        /**
         * @brief Virtual destructor for the Graph class.
         * 
         * This is defined as `default` and is virtual to allow proper cleanup 
         * in derived classes.
         */
        virtual ~Graph() = default;
        
        /**
         * @brief Adds an edge to the graph.
         * 
         * This function adds an edge with a given source, destination, and weight 
         * to the graph's adjacency list.
         * 
         * @param u Source vertex of the edge
         * @param v Destination vertex of the edge
         * @param w Weight of the edge
         */
        virtual void addEdge(int u, int v, int w);

        /*
            * @brief Removes an edge from the graph.
            * 
            * This function removes an edge with a given source and destination from 
            * the graph's adjacency list.
            * 
            * @param u Source vertex of the edge
            * @param v Destination vertex of the edge
            
        */
        bool removeEdge(int u, int v);

        /**
         * @brief Returns the number of vertices in the graph.
         * 
         * @return int The number of vertices.
         */
        int getVerticesNumber() const { return V; }

        /**
         * @brief Returns the adjacency list of the graph.
         * 
         * @return const vector<vector<Edge>>& The adjacency list.
         */
        const vector<vector<Edge>>& getAdj() const { return adj; }
};

#endif
