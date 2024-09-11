#include "ThreadContext.hpp"

pthread_t  ThreadContext::createThread(function<void()> func)
{
    // Allocate a new function on the heap so it can be passed to the thread
    _functUniquePtr = make_unique<function<void()>>(func);
    // Create the thread
    int result = pthread_create(&_thread, nullptr, [](void* arg) -> void* {
        // Convert the argument back to function pointer
        function<void()>* funcPtr = static_cast<function<void()>*>(arg);
        
        // Execute the function
        (*funcPtr)();
          
        return nullptr;
    }, _functUniquePtr.get());
    // Check for thread creation errors
    if (result != 0) {
        throw runtime_error("Failed to create thread");
    }

    return _thread;
}

void ThreadContext::wakeUp()
{
    unique_lock<mutex> lock(_thMx);
    // Change the state of the thread to awake and notify the condition variable
    _isAwake.store(true, memory_order_release);
    _thCv.notify_one();
}

void ThreadContext::sleep()
{
    unique_lock<mutex> lock(_thMx);
    _isAwake.store(false, memory_order_release);
}

void ThreadContext::conditionWait(const atomic<bool>& stopFlag)
{
    // Wait for the condition variable to be notified or the stop flag to be set
    unique_lock<mutex> lock(_thMx);
    _thCv.wait(lock, [&stopFlag, this] { return _isAwake.load(memory_order_acquire) || stopFlag.load(memory_order_acquire); });
}