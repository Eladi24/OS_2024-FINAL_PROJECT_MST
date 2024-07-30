#include "MSTFactory.hpp"

void MSTFactory::setStrategy(MSTStrategy* strategy)
{
    if (this->strategy != nullptr)
    {
        delete this->strategy;
    }
    this->strategy = strategy;
}

unique_ptr<Tree> MSTFactory::createMST(unique_ptr<Graph>& g)
{
    vector<Edge> edges = strategy->findMST(*g);
    return make_unique<Tree>(edges);
}