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
    uintptr_t idValue = reinterpret_cast<uintptr_t>(id);
    ss << hex << setfill('0') << setw(4) << (idValue & 0xFFFF);
    return ss.str();
}

mutex LFThreadPool::_outputMx;
LFThreadPool::LFThreadPool(size_t numThreads, Reactor& reactor)
    : _followers(numThreads), _stop(false), _leaderChanged(false), _reactor(reactor)
{
    for (size_t i = 0; i < numThreads; ++i)
    {
        _followers[i] = make_shared<ThreadContext>();
        _followers[i]->createThread(bind(&LFThreadPool::followerLoop, this, i));
        {
            unique_lock<mutex> guard(_outputMx);
            cout << THREAD_GREEN << "🧵 [THREAD] " << THREAD_RESET 
                 << "Thread #" << (i+1) << " created " << THREAD_CYAN 
                 << "(ID: 0x" << formatThreadId(_followers[i]->getId()) << ")" 
                 << THREAD_RESET << endl;
        }
    }
   
    promoteNewLeader();
}

LFThreadPool::~LFThreadPool()
{
    {
        unique_lock<mutex> guard(_outputMx);
        cout << THREAD_YELLOW << "🗑️  [POOL] " << THREAD_RESET 
             << "Thread pool destructor called" << THREAD_RESET << endl;
    }
    
    if (!_stop.load(memory_order_acquire)) {
        stopPool();
    }
    
    // Clean the allocated resources (followers are already reset in join())
    _followers.clear();
    _leader.reset();
}

void LFThreadPool::promoteNewLeader()
{
    unique_lock<mutex> guard(_mx);
    
    if (_leader == nullptr)
    {
        {
            unique_lock<mutex> outputGuard(_outputMx);
            cout << THREAD_MAGENTA << "👑 [LEADER] " << THREAD_RESET 
                 << "Promoting initial leader " << THREAD_CYAN 
                 << "(ID: 0x" << formatThreadId(_followers.begin()->get()->getId()) << ")" 
                 << THREAD_RESET << endl;
        }
        _leader = *_followers.begin();
        shared_ptr<ThreadContext> newLeader = _leader;
        guard.unlock(); // Release lock before calling wakeUp to avoid holding lock during thread wakeup
        newLeader->wakeUp();
        return;
    }
    
    for (auto &follower : _followers)
    {
        if (*follower != *_leader && !follower->isAwake())
        {
            {
                unique_lock<mutex> outputGuard(_outputMx);
                cout << THREAD_MAGENTA << "👑 [LEADER] " << THREAD_RESET 
                     << "New leader promoted " << THREAD_CYAN 
                     << "(ID: 0x" << formatThreadId(follower->getId()) << ")" 
                     << THREAD_RESET << endl;
            }
            _leader = follower;
            shared_ptr<ThreadContext> newLeader = _leader;
            guard.unlock(); // Release lock before calling wakeUp to avoid holding lock during thread wakeup
            newLeader->wakeUp();
            return;
        }
    }
}


void LFThreadPool::followerLoop(int id)
{
    while (true)
    {
        _followers[id]->conditionWait(_stop);
        {
            unique_lock<mutex> guard(_outputMx);
            cout << THREAD_BLUE << "⏰ [WAKE] " << THREAD_RESET 
                 << "Thread " << THREAD_CYAN << "0x" << formatThreadId(_followers[id]->getId()) 
                 << THREAD_RESET << " woke up" << endl;
        }
        
        if (_stop.load(memory_order_acquire))
            break;


        _reactor.handleEvents();
        
        if (_stop.load(memory_order_acquire))
            break;
        
        shared_ptr<ThreadContext> currThread;
        {
            unique_lock<mutex> guard(_mx);
            currThread = _leader;
            guard.unlock();
        }
        
        if (!currThread) {
            break; 
        }
        promoteNewLeader();

        if (!_stop.load(memory_order_acquire)) {
            currThread->executeEvent();
        }
        {
            unique_lock<mutex> guard(_outputMx);
            cout << THREAD_YELLOW << "😴 [SLEEP] " << THREAD_RESET 
                 << "Thread " << THREAD_CYAN << "0x" << formatThreadId(currThread->getId()) 
                 << THREAD_RESET << " returning to sleep" << endl;
        }

        currThread->sleep();
    }
}


void LFThreadPool::addFd(int fd, function<void()> event)
{
    unique_lock<mutex> guard(_mx);
    shared_ptr<ThreadContext> currentLeader = _leader;
    guard.unlock(); 
    
    if (currentLeader) {
        currentLeader->addHandle(fd, event);
    }
}

void LFThreadPool::stopPool()
{

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
            continue;  
        }
        
        pthread_t id = follower->getId();
        {
            unique_lock<mutex> guard(_outputMx);
            cout << THREAD_YELLOW << "🛑 [JOIN] " << THREAD_RESET 
                 << "Joining thread " << THREAD_CYAN << "0x" << formatThreadId(id) 
                 << THREAD_RESET << endl;
        }
        try {
            follower->cancel();
            follower->join();
        } catch (...) {
            
        }
        follower.reset();
    }
}