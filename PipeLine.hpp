#ifndef _PIPELINE_HPP
#define _PIPELINE_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <memory>
#include "ActiveObject.hpp"
using namespace std;

template<typename Input, typename Output>
class PipelineStage
{
    public:
        using Task = function<void(Input, function<void(Output)>)>;
        PipelineStage(Task task) : _task(task), _activeObject(make_shared<ActiveObject<function<void()>>>()) {} 
        void setNextStage(shared_ptr<PipelineStage<Output, void>> nextStage) { _nextStage = nextStage; }
        void process(Input input);
        
    private:
        Task _task;
        shared_ptr<ActiveObject<function<void()>>> _activeObject;
        shared_ptr<PipelineStage<Output, void>> _nextStage;

};

template<typename Input>
class Pipeline {
    private:
        shared_ptr<PipelineStage<Input, void>> _firstStage;
        
    public:
        Pipeline(shared_ptr<PipelineStage<Input, void>> firstStage) : _firstStage(firstStage) {}
        void process(Input input) { _firstStage->process(input); } 
};



#endif