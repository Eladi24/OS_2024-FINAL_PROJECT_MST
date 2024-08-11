#include "ActiveObject.hpp"

ActiveObject::ActiveObject() : _done(false) {
    _worker = std::thread(&ActiveObject::run, this);
}
ActiveObject::~ActiveObject() {
    {
        std::unique_lock<std::mutex> lock(_mx);
        _done = true;
        _cv.notify_all();  // Ensure the mutex is held when notifying
    }
    _worker.join();  // Wait for the worker thread to finish
}


void ActiveObject::enqueue(std::function<void()> task) {
    std::unique_lock<std::mutex> lock(_mx);  // Lock the mutex
    _tasks.push(std::move(task));
    _cv.notify_one();  // Notify while still holding the lock
    // Mutex automatically unlocks when lock goes out of scope
}


void ActiveObject::run() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(_mx);
            _cv.wait(lock, [this] { return _done || !_tasks.empty(); });

            if (_done && _tasks.empty()) {
                return;  // Exit the thread if done and no tasks are left
            }

            task = std::move(_tasks.front());
            _tasks.pop();
        }

        task();  // Execute the task
    }
}
