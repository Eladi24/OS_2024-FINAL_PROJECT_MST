#include "MSTFactory.hpp"

void MSTFactory::setStrategy(MSTStrategy* strategy)
{
    if (this->_strategy != nullptr)
    {
        this->_strategy.reset();
    }
    this->_strategy = unique_ptr<MSTStrategy>(strategy);
}

unique_ptr<Tree> MSTFactory::createMST(unique_ptr<Graph>& g)
{
    vector<Edge> edges = _strategy->findMST(*g);
    return make_unique<Tree>(edges);
}