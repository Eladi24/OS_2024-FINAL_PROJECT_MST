#include "ActiveObject.hpp"
#include "ServerLogger.hpp"
#include <sstream>
#include <iomanip>
#include <thread>

mutex ActiveObject::_outputMx;
ActiveObject::~ActiveObject()
{
    _done.store(true, memory_order_release);
    {
        lock_guard<mutex> lock(_mx);
        _cv.notify_all();
    }
    _worker.join();
}

void ActiveObject::run()
{   
    while (true)
    {
        function<void()> task;
        {
            unique_lock<mutex> lock(_mx);
            _cv.wait(lock, [this] { return _done.load(memory_order_acquire) || !_tasks.empty(); });
            if (_done.load(memory_order_acquire) && _tasks.empty()) return;
            
            if (_stageId >= 0) {
                stringstream ss;
                ss << hex << this_thread::get_id();
                {
                    unique_lock<mutex> logLock(_outputMx);
                    cout << ServerLogger::YELLOW << "⏰ [WAKE] " << ServerLogger::RESET 
                         << "Pipeline Stage " << _stageId 
                         << " worker thread woke up (ID: 0x" << ss.str() << ")" 
                         << ServerLogger::RESET << endl;
                }
            }
            
            task = std::move(_tasks.front());
            _tasks.pop();
        }
        task();
        
        if (_stageId >= 0) {
            stringstream ss;
            ss << hex << this_thread::get_id();
            {
                unique_lock<mutex> logLock(_outputMx);
                cout << ServerLogger::CYAN << "😴 [SLEEP] " << ServerLogger::RESET 
                     << "Pipeline Stage " << _stageId 
                     << " worker thread returning to sleep (ID: 0x" << ss.str() << ")" 
                     << ServerLogger::RESET << endl;
            }
        }
    }
}
