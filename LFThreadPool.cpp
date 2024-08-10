#include "LFThreadPool.hpp"
#include <iostream>

LFThreadPool::LFThreadPool(size_t numThreads, std::shared_ptr<Reactor> reactor)
    : _reactor(std::move(reactor)), _stop(false) {
    for (size_t i = 1; i < numThreads; ++i) {
        std::thread t(&LFThreadPool::followerLoop, this);
        _followersState[t.get_id()] = false;
        _followers.push_back(std::move(t));
    }
}

LFThreadPool::~LFThreadPool() {
    std::cout << "LFThreadPool destructor" << std::endl;
    {
        std::unique_lock<std::mutex> lock(_mx);
        _stop = true;
    }
    _condition.notify_all();
    join();
}

void LFThreadPool::promoteNewLeader() {
    for (auto &follower : _followers) {
        if (follower.get_id() != _leader && !_followersState[follower.get_id()]) {
            std::cout << "Promoting new leader: " << follower.get_id() << std::endl;
            _leader = follower.get_id();
            _followersState[_leader] = true;
            _condition.notify_one();
            return;
        }
    }
}

void LFThreadPool::join() {
    for (auto &follower : _followers) {
        if (follower.joinable()) {
            follower.join();
        }
    }
}

void LFThreadPool::followerLoop() {
    std::cout << "Follower thread " << std::this_thread::get_id() << " started" << std::endl;
    while (true) {
        std::unique_lock<std::mutex> lock(_mx);

        _condition.wait(lock, [this] {
            return _followersState[std::this_thread::get_id()] || _stop;
        });

        if (_stop) break;

        _reactor->handleEvents();

        _followersState[std::this_thread::get_id()] = false;
        promoteNewLeader();
    }
}

void LFThreadPool::run() {
    _leader = std::this_thread::get_id();
    _followersState[_leader] = true;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(_mx);
            if (_stop) break;
        }

        std::cout << "Leader thread " << _leader << " handling events" << std::endl;
        _reactor->handleEvents();

        _followersState[_leader] = false;
        promoteNewLeader();
    }
}
