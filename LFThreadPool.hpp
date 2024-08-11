#include <memory>
#include <iostream>
#include <vector>
#include "Reactor.hpp"
#include <thread>
#include <atomic>

class LFThreadPool {
public:
    LFThreadPool(unsigned long numThreads, std::shared_ptr<Reactor> reactor);
    ~LFThreadPool();

    void run();
    void stop(); // Add this method

private:
    void workerThread();
    std::vector<std::thread> workers;
    std::shared_ptr<Reactor> reactor;
    std::atomic_bool running; // Add a flag to control the loop in the worker threads
};
