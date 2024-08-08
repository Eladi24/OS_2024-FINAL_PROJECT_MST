#include "ActiveObject.hpp"

ActiveObject::ActiveObject() : _done(false) {
    _worker = std::thread(&ActiveObject::run, this);
}

ActiveObject::~ActiveObject() {
    {
        std::unique_lock<std::mutex> lock(_mx);
        _done = true;
    }
    _cv.notify_one();
    _worker.join();
}

void ActiveObject::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(_mx);
        _tasks.push(std::move(task));
    }
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
