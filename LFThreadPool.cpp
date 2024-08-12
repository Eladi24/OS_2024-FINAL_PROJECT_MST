#include "LFThreadPool.hpp"
#include <iostream> // For debugging

LFThreadPool::LFThreadPool(unsigned long numThreads, std::shared_ptr<Reactor> reactor)
    : reactor(reactor), running(true) {
    for (unsigned long i = 0; i < numThreads; ++i) {
        workers.emplace_back(&LFThreadPool::workerThread, this);
    }
}

LFThreadPool::~LFThreadPool() {
    stop();
}

void LFThreadPool::run() {
    // This method is not required to do anything special in this case
    // The worker threads are already running in the constructor.
}

void LFThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (!running) return;  // Ensure stop is called only once
        running = false;  // Set the running flag to false to stop the loop
    }

    reactor->notifyAll();  // Wake up all threads to ensure they exit

    for (auto &worker : workers) {
        if (worker.joinable()) {
            worker.join();  // Join all threads
        }
    }
    workers.clear();  // Clear the vector after all threads have been joined
}

void LFThreadPool::workerThread() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!running) break;  // Exit the loop if not running
        }
        reactor->handleEvents();  // Process events in the reactor
    }
}
