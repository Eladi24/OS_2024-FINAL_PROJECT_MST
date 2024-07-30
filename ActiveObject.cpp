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

template<class F>
function<F()> ActiveObject::dequeue()
{
    unique_lock<mutex> lock(_mx);
    _cv.wait(lock, [this] { return _done || !_tasks.empty(); });

    if (_done && _tasks.empty())
        return nullptr;

    function<F()> task = move(_tasks.front());
    _tasks.pop();
    return task;
}
