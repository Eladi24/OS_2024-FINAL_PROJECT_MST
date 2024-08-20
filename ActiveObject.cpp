#include "ActiveObject.hpp"

ActiveObject::~ActiveObject()
{
    cout << "ActiveObject destructor" << endl;
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
    cout << "Worker thread id: " << _worker.get_id() << endl;
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
        
        cout << "Worker thread id: " << _worker.get_id() << "Finished task" << endl;

    }
}
