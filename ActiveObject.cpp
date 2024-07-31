#include "ActiveObject.hpp"

ActiveObject::~ActiveObject()
{
    {
        unique_lock<mutex> lock(_mx);
        // Let the worker thread know that it should stop
        _done = true;
    }
    // Wake up the worker thread
    _cv.notify_one();
    // Wait for the worker thread to finish its previous task
    _worker.join();
}


void ActiveObject::run()
{
    while (true)
    {
        function<void()> task;
        {
            unique_lock<mutex> lock(_mx);
            _cv.wait(lock, [this] { return _done || !_tasks.empty(); });
            if (_done && _tasks.empty()) return;
            task = move(_tasks.front());
            _tasks.pop();
        }
        task();
    }
}
