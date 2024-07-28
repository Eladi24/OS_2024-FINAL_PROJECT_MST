#include <iostream>
#include <string>
#include <sstream>
#include "Tree.hpp"
#include "MSTStrategy.hpp"
#include "MSTFactory.hpp"

void handleCommands(Graph* g, MSTFactory& factory, Tree* mst)
{
    string command;
    while (getline(cin, command))
    {
        if (command.empty())
            continue;

        stringstream ss(command);
        string cmd;
        ss >> cmd;

        if (cmd == "Newgraph")
        {   
            int n, m;
            
            if (!(ss >> n >> m) || n <= 0 || m < 0)
            {
                cerr << "Invalid graph input" << endl;
                continue;
            }
        
            g = new Graph(n, m);
            cout << "Graph created with " << n << " vertices and " << m << " edges." << endl;
        
        }

        else if (cmd == "Prim")
        {
            if (g->getAdj().empty())
            {
                cerr << "Graph not initialized" << endl;
                continue;
            }
            if (mst != nullptr)
            {
                delete mst;
                mst = nullptr;
            }
            
            factory.setStrategy(new PrimStrategy());
            mst = factory.createMST(*g);
            cout << "MST created using Prim's algorithm." << endl;
            mst->printMST();
            
        }
        else if (cmd == "Kruskal")
        
        {
            if (g->getAdj().empty())
            {
                cerr << "Graph not initialized" << endl;
                continue;
            }
            if (mst != nullptr)
            {
                delete mst;
                mst = nullptr;
            }

            factory.setStrategy(new KruskalStrategy());
            mst = factory.createMST(*g);
            cout << "MST created using Kruskal's algorithm." << endl;
            mst->printMST();
        
        }

        else if (cmd == "MSTweight")
        {
            if (mst == nullptr)
            {
                cerr << "MST not created" << endl;
                continue;
            }
            cout << "Total weight of the MST is " << mst->totalWeight() << endl;
        }

        else if (cmd == "Shortestpath")
        {
            if (mst == nullptr)
            {
                cerr << "MST not created" << endl;
                continue;
            }

            int src, dest;
            if (!(ss >> src >> dest) || src < 0 || src >= g->getVerticesNumber() || dest < 0 || dest >= g->getVerticesNumber())
            {
                cerr << "Invalid source or destination" << endl;
                continue;
            }

            cout << "Shortest path from " << src << " to " << dest << " is " << mst->shortestPath(src, dest) << endl;
        }
        else if (cmd == "Longestpath")
        {
            if (mst == nullptr)
            {
                cerr << "MST not created" << endl;
                continue;
            }

            int src, dest;
            if (!(ss >> src >> dest) || src < 0 || src >= g->getVerticesNumber() || dest < 0 || dest >= g->getVerticesNumber())
            {
                cerr << "Invalid source or destination" << endl;
                continue;
            }

            cout << "Longest path from " << src << " to " << dest << " is " << mst->longestPath(src, dest) << endl;
        }
        else if (cmd == "Avaragedistance")
        {
            if (mst == nullptr)
            {
                cerr << "MST not created" << endl;
                continue;
            }

            cout << "Avarage distance is " << mst->averageDistanceEdges() << endl;
        }
        else if (cmd == "Exit")
        {
            delete g;
            delete mst;
            break;
        }
        else
        {
            cerr << "Unknown command: " << cmd << endl;
        }
    }
}


int main()
{
    Graph* g = nullptr;
    MSTFactory factory;
    Tree* mst = nullptr;
    handleCommands(g, factory, mst);
    return 0;
}