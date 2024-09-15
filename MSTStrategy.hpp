#ifndef MSTSTRATEGY_HPP
#define MSTSTRATEGY_HPP
#include <iostream>
#include "Tree.hpp"

/*
    MSTStrategy is an abstract class that defines the interface for the strategy pattern.
    The algorithms that will be used to find the minimum spanning tree will be implemented
    in the concrete classes that inherit from this class.
*/
class MSTStrategy {
    public:
        /*
        * @brief This method will find the minimum spanning tree of the graph g.
        * It is a pure virtual method, so it must be implemented by the concrete classes.
        * @param g The graph that will be used to find the minimum spanning tree.
        * @return vector<Edge> The edges that form the minimum spanning tree.
        */
        vector<Edge> virtual findMST(const Graph& g) = 0;
};

/*
    PrimStrategy is a concrete class that inherits from MSTStrategy.
    It implements the Prim's algorithm to find the minimum spanning tree of a graph.
*/
class PrimStrategy: public MSTStrategy {
    private:
        /*
        * @brief This method will find the vertex with the minimum key value,
        * from the set of vertices not yet included in the minimum spanning tree.
        * @param key The array that stores the key values of the vertices.
        * @param mstSet The array that stores the vertices that are already included in the minimum spanning tree.
        * @param V The number of vertices in the graph.
        * @return The index of the vertex with the minimum key value.
        */
        int minKey(vector<int> key, vector<bool> mstSet, int V);

    public:
        /*
        * @brief This method will find the minimum spanning tree of the graph g using Prim's algorithm.
        * @param g The graph that will be used to find the minimum spanning tree.
        * @ return vector<Edge> The edges that form the minimum spanning tree.
        */
        vector<Edge> findMST(const Graph& g) override;
};

/*
    DSU is a class that implements the Disjoint Set Union data structure.
    It is used in the Kruskal's algorithm to find the minimum spanning tree.
*/
class DSU {
    private:
        vector<int> parent;
        vector<int> rank;
    public:
        DSU(int n): parent(n, -1), rank(n, 0) {}
        
        /*
        * @brief This method will find the representative of the set that contains the element u.
        * @param u The element whose representative will be found.
        * @return The representative of the set that contains the element u.
        */
        int find(int u);

        /*
        * @brief This method will unite the sets that contain the elements u and v.
        * @param u The first element.
        * @param v The second element.
        * @return void
        */
        void unite(int u, int v);
};

/*
    KruskalStrategy is a concrete class that inherits from MSTStrategy.
    It implements the Kruskal's algorithm to find the minimum spanning tree of a graph.
*/
class KruskalStrategy: public MSTStrategy {
    public:
        /*
        * @brief This method will find the minimum spanning tree of the graph g using Kruskal's algorithm.
        * @param g The graph that will be used to find the minimum spanning tree.
        * @ return vector<Edge> The edges that form the minimum spanning tree.
        */
        vector<Edge> findMST(const Graph& g) override;
};

#endif