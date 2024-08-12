#include <memory>
#include "Tree.hpp"
#include "MSTStrategy.hpp"
#include <mutex>

class MSTFactory {
public:
    void setStrategy(std::unique_ptr<MSTStrategy> strategy);
    std::unique_ptr<Tree> createMST(std::shared_ptr<Graph>& g);

private:
    std::unique_ptr<MSTStrategy> strategy;
     std::mutex cerrMutex;
};
