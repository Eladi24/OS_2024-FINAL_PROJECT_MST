#include "ActiveObject.hpp"

mutex ActiveObject::_outputMx;
ActiveObject::~ActiveObject()
{
    {
        lock_guard<mutex> lock(_outputMx);
        cout << "ActiveObject destructor" << endl;
    }
    // Let the worker thread know that it should stop
    _done.store(true);
    // Wake up the worker thread
    _cv.notify_all();
    // Wait for the worker thread to finish its previous task
    _worker.join();
}

void ActiveObject::start()
{
    
    _worker = thread(&ActiveObject::run, this);
}


void ActiveObject::run()
{   
    {
        lock_guard<mutex> outLock(_outputMx);
        cout << "Worker thread id: " << _worker.get_id() << endl;
    }
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<mutex> lock(_mx);
            _cv.wait(lock, [this] { return _done.load() || !_tasks.empty(); });
            if (_done && _tasks.empty()) return;
            task = std::move(_tasks.front());
            _tasks.pop();
        }
        task();
        {
            lock_guard<mutex> outLock(_outputMx);
            cout << "Worker thread id: " << _worker.get_id() << " Finished task" << endl;
        }
    }
}
