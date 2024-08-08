#include "MSTFactory.hpp"
#include "MSTStrategy.hpp"

void MSTFactory::setStrategy(std::unique_ptr<MSTStrategy> strategy)
{
    this->strategy = std::move(strategy);
}

std::unique_ptr<Tree> MSTFactory::createMST(std::unique_ptr<Graph>& g)
{
    std::vector<Edge> edges = strategy->findMST(*g);
    return std::make_unique<Tree>(edges);
}
