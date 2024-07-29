#include "ActiveObject.hpp"

ActiveObject::~ActiveObject()
{
    {
        unique_lock<mutex> lock(_mx);
        _done = true;
    }
    _cv.notify_one();
    _worker.join();
}

template<class F>
void ActiveObject::enqueue(F task)
{
    unique_lock<mutex> lock(_mx);
    _tasks.emplace(task);
    lock.unlock();
    _cv.notify_one();
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