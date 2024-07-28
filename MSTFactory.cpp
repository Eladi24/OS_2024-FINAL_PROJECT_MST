#include "MSTFactory.hpp"

void MSTFactory::setStrategy(MSTStrategy* strategy)
{
    if (this->strategy != nullptr)
    {
        delete this->strategy;
    }
    this->strategy = strategy;
}

Tree* MSTFactory::createMST(Graph& g)
{
    vector<Edge> edges = strategy->findMST(g);
    Tree* mst = new Tree(edges);
    return mst;
}