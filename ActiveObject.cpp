#include "ActiveObject.hpp"

ActiveObject::ActiveObject()
    : _worker(&ActiveObject::run, this) {}

ActiveObject::~ActiveObject() {
    {
        std::unique_lock<std::mutex> lock(_mx);
        // Let the worker thread know that it should stop
        _done = true;
    }
    // Wake up the worker thread
    _cv.notify_one();
    // Wait for the worker thread to finish its previous task
    if (_worker.joinable()) {
        _worker.join();
    }
}

void ActiveObject::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(_mx);
        _tasks.push(std::move(task));
    }
    // Notify the worker thread that a new task is available
    _cv.notify_one();
}

void ActiveObject::run() {
    std::cout << "Worker thread id: " << _worker.get_id() << std::endl;
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(_mx);
            _cv.wait(lock, [this] { return _done || !_tasks.empty(); });
            if (_done && _tasks.empty()) return;
            task = std::move(_tasks.front());
            _tasks.pop();
        }
        task();
    }
}
