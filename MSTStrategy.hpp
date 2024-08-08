#ifndef MSTSTRATEGY_HPP
#define MSTSTRATEGY_HPP

#include <vector>
#include "Graph.hpp"

class MSTStrategy {
public:
    virtual ~MSTStrategy() = default;
    virtual std::vector<Edge> findMST(const Graph& g) = 0;
};

class PrimStrategy : public MSTStrategy {
public:
    std::vector<Edge> findMST(const Graph& g) override;

private:
    int minKey(const std::vector<int>& key, const std::vector<char>& mstSet, int V);
};

class KruskalStrategy : public MSTStrategy {
public:
    std::vector<Edge> findMST(const Graph& g) override;
};

#endif // MSTSTRATEGY_HPP
