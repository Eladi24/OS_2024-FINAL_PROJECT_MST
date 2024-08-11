#include "LFThreadPool.hpp"

LFThreadPool::LFThreadPool(unsigned long numThreads, std::shared_ptr<Reactor> reactor)
    : reactor(reactor), running(true) { // Initialize the running flag
    for (unsigned long i = 0; i < numThreads; ++i) {
        workers.emplace_back(&LFThreadPool::workerThread, this);
    }
}

LFThreadPool::~LFThreadPool() {
    stop(); // Ensure stop is called to clean up threads
    for (auto &worker : workers) {
        if (worker.joinable()) {
            worker.join(); // Wait for all threads to finish
        }
    }
}

void LFThreadPool::run() {
    for (auto &worker : workers) {
        if (worker.joinable()) {
            worker.join(); // Wait for all threads to finish
        }
    }
}

void LFThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        running = false; // Set the running flag to false to stop the loop
        reactor->notifyAll(); // Ensure all threads are woken up to exit
    }

    for (auto &worker : workers) {
        if (worker.joinable()) {
            worker.join(); // Join all threads
        }
    }
}


void LFThreadPool::workerThread() {
    while (running) {
        reactor->handleEvents(); // Process events in the reactor
    }
}
