#include "LFThreadPool.hpp"
#include <iomanip>
#include <sstream>

// ANSI color codes for thread logs
const string THREAD_RESET = "\033[0m";
const string THREAD_CYAN = "\033[36m";
const string THREAD_GREEN = "\033[32m";
const string THREAD_YELLOW = "\033[33m";
const string THREAD_BLUE = "\033[34m";
const string THREAD_MAGENTA = "\033[35m";
const string THREAD_BOLD = "\033[1m";

// Helper function to format thread ID (show last 4 hex digits)
string formatThreadId(pthread_t id) {
    stringstream ss;
    // Convert pthread_t to uintptr_t for formatting (works on both Linux and macOS)
    uintptr_t idValue = reinterpret_cast<uintptr_t>(id);
    ss << hex << setfill('0') << setw(4) << (idValue & 0xFFFF);
    return ss.str();
}

mutex LFThreadPool::_outputMx;
LFThreadPool::LFThreadPool(size_t numThreads, Reactor& reactor)
    : _followers(numThreads), _stop(false), _leaderChanged(false), _reactor(reactor)
{
    // Start the follower threads
    for (size_t i = 0; i < numThreads; ++i)
    {
        // Create a new thread context object
        _followers[i] = make_shared<ThreadContext>();
        // Create a new thread and bind the follower loop function
        _followers[i]->createThread(bind(&LFThreadPool::followerLoop, this, i));
        {
            unique_lock<mutex> guard(_outputMx);
            cout << THREAD_GREEN << "🧵 [THREAD] " << THREAD_RESET 
                 << "Thread #" << (i+1) << " created " << THREAD_CYAN 
                 << "(ID: 0x" << formatThreadId(_followers[i]->getId()) << ")" 
                 << THREAD_RESET << endl;
        }
    }
    // Promote the initial leader
    promoteNewLeader();
}

LFThreadPool::~LFThreadPool()
{
    {
        unique_lock<mutex> guard(_outputMx);
        cout << THREAD_YELLOW << "🗑️  [POOL] " << THREAD_RESET 
             << "Thread pool destructor called" << THREAD_RESET << endl;
    }
    
    // Only stop if not already stopped
    if (!_stop.load(memory_order_acquire)) {
        stopPool();
    }
    
    // Clean the allocated resources (followers are already reset in join())
    _followers.clear();
    _leader.reset();
}

void LFThreadPool::promoteNewLeader()
{
    // If there is no leader, promote the first follower in the list
    if (_leader == nullptr)
    {
        {
            unique_lock<mutex> guard(_outputMx);
            cout << THREAD_MAGENTA << "👑 [LEADER] " << THREAD_RESET 
                 << "Promoting initial leader " << THREAD_CYAN 
                 << "(ID: 0x" << formatThreadId(_followers.begin()->get()->getId()) << ")" 
                 << THREAD_RESET << endl;
        }
        _leader = *_followers.begin();
        // Wake up the new leader to handle events
        _leader->wakeUp();
        return;
    }
    
    for (auto &follower : _followers)
    {
        // If the follower is not the current leader and is not awake, promote it
        if (*follower != *_leader && !follower->isAwake())
        {
            {
                unique_lock<mutex> guard(_outputMx);
                cout << THREAD_MAGENTA << "👑 [LEADER] " << THREAD_RESET 
                     << "New leader promoted " << THREAD_CYAN 
                     << "(ID: 0x" << formatThreadId(follower->getId()) << ")" 
                     << THREAD_RESET << endl;
            }
            _leader = follower;
            // Wake up the new leader to handle events
            _leader->wakeUp();
            return;
        }
    }
}


void LFThreadPool::followerLoop(int id)
{
    while (true)
    {
        // Wait until the follower is promoted to be the leader or the thread pool is stopped
        _followers[id]->conditionWait(_stop);
        {
            unique_lock<mutex> guard(_outputMx);
            cout << THREAD_BLUE << "⏰ [WAKE] " << THREAD_RESET 
                 << "Thread " << THREAD_CYAN << "0x" << formatThreadId(_followers[id]->getId()) 
                 << THREAD_RESET << " woke up" << endl;
        }
        
        // If stop then the program is shutting down
        if (_stop.load(memory_order_acquire))
            break;

        // Handle events in the reactor
        _reactor.handleEvents();
        
        // Check again after handleEvents (might have been set during select)
        if (_stop.load(memory_order_acquire))
            break;
        
        // Promote a new leader and execute the event
        shared_ptr<ThreadContext> currThread = _leader;
        if (!currThread) {
            break;  // No leader available, exit
        }
        promoteNewLeader();

        // Execute events in the thread context (only if not shutting down)
        if (!_stop.load(memory_order_acquire)) {
            currThread->executeEvent();
        }
        {
            unique_lock<mutex> guard(_outputMx);
            cout << THREAD_YELLOW << "😴 [SLEEP] " << THREAD_RESET 
                 << "Thread " << THREAD_CYAN << "0x" << formatThreadId(currThread->getId()) 
                 << THREAD_RESET << " returning to sleep" << endl;
        }
        // Put the thread to sleep
        currThread->sleep();
    }
}


void LFThreadPool::addFd(int fd, function<void()> event)
{
    // Add the file descriptor to the leader
    _leader->addHandle(fd, event);
}

void LFThreadPool::stopPool()
{
    // Stop all worker threads
    _stop.store(true, memory_order_release);
    for (auto & follower : _followers)
    {
        follower->notify();
    }
    join();
}

void LFThreadPool::join()
{   
    for (auto & follower : _followers)
    {
        if (!follower) {
            continue;  // Skip if already reset
        }
        
        pthread_t id = follower->getId();
        {
            unique_lock<mutex> guard(_outputMx);
            cout << THREAD_YELLOW << "🛑 [JOIN] " << THREAD_RESET 
                 << "Joining thread " << THREAD_CYAN << "0x" << formatThreadId(id) 
                 << THREAD_RESET << endl;
        }
        // Cancel the thread and join it
        try {
            follower->cancel();
            follower->join();
        } catch (...) {
            // Ignore exceptions during shutdown
        }
        follower.reset();
    }
}