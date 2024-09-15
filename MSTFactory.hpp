#ifndef MSTFACTORY_HPP
#define MSTFACTORY_HPP
#include <memory>
#include "MSTStrategy.hpp"

class MSTFactory
{
    private:
        unique_ptr<MSTStrategy> _strategy;
        
    public:
        /*
        * @brief This method will set the strategy that will be used to find the minimum spanning tree.
        * @param strategy The strategy that will be used to find the minimum spanning tree.
        * @return void
        */
        void setStrategy(MSTStrategy* strategy);

        /*
        * @brief This method will create the minimum spanning tree of the graph g using the strategy set.
        * @param g The graph that will be used to find the minimum spanning tree.
        * @return Tree* The minimum spanning tree of the graph g.
        */
        unique_ptr<Tree> createMST(unique_ptr<Graph>& g);
        void destroyStrategy(){_strategy.reset();}
};

#endif