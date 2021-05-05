#include "threadpool.h"
#include <chrono>


static uint64_t gettickcount() {
    using namespace std::chrono;
    time_point<std::chrono::system_clock, milliseconds> tp =
            time_point_cast<milliseconds>(system_clock::now());
    return tp.time_since_epoch().count();
}

TaskProfile::TaskProfile(TTiming _timing, int _serial_tag, int _after, int _period)
        : type(_timing), serial_tag(_serial_tag), after(_after)
        , period(_period), seq(__MakeSeq()) {
    if (type != kImmediate) {
        record = ::gettickcount();
    }
}


const int ThreadPool::kNoSerialTag = -1;

ThreadPool::ThreadPool(size_t _n_threads)
        : stop_(false) {
    while (_n_threads--) {
        workers_.emplace_back(std::thread(&ThreadPool::__WorkerEntry, this));
    }
}

void ThreadPool::Init() {}

void ThreadPool::__WorkerEntry() {
    while (true) {
        TaskPairPtr task_pair = nullptr;
        TaskProfile *profile = nullptr;
        
        UniqueLock lock(this->mutex_);
        uint64_t wait_time = 10000;
        bool is_waiting_timed_task = false;
        while (true) {
            bool pred = this->cv_.wait_for(lock,
                                           std::chrono::milliseconds(wait_time),
                                        [&, this] {
                    if (this->stop_) {
                        if (task_pair != nullptr) {
                            delete task_pair, task_pair = nullptr;
                        }
                        return true;
                    }
                    /*
                     * If task_pair is NULL, indicating it has not been chosen, then choose the fastest task.
                     * If task_pair is not NULL, indicating it has already been chosen,
                     * see if there is any faster task added while waiting for the expiration of current timed task.
                     */
                    TaskPairPtr faster = __PickOutTaskFasterThan(task_pair);
                    if (faster) {
                        task_pair = faster;
                        return true;
                    }
                    return false;
            });
            
            if (this->stop_) { return; }
            
            if (!pred && !is_waiting_timed_task) { continue; }
            
            if (task_pair) {
                profile = &task_pair->first;
                uint64_t wait = __ComputeWaitTime(profile);
                if (wait > 0) {
                    wait_time = wait;
                    is_waiting_timed_task = true;
                    continue;
                }
                break;
            }
        }
        this->running_serial_tags_.insert(profile->serial_tag);
        lock.unlock();
        
        task_pair->second();   // executing task...
    
        lock.lock();
        this->running_serial_tags_.erase(profile->serial_tag);
        if (profile->type == TaskProfile::kPeriodic) {
            profile->record = ::gettickcount();
            tasks_.push_back(task_pair);
        } else {
            delete task_pair, task_pair = nullptr;
        }
        
    }
}


ThreadPool::TaskPairPtr ThreadPool::__PickOutTaskFasterThan(TaskPairPtr _old/* = nullptr*/) {
    uint64_t now = ::gettickcount();
    
    auto old_wait = (uint64_t) - 1;
    if (_old) {
        old_wait = __ComputeWaitTime(&_old->first, now);
        if (old_wait <= 0) {
            return nullptr;
        }
    }
    
    auto it = tasks_.begin();
    auto last = tasks_.end();
    auto min_wait_time_iter = last;
    uint64_t min_wait_time = old_wait;
    
    while (it != last) {
        TaskProfile *profile = &(*it)->first;
        uint64_t wait = __ComputeWaitTime(profile, now);
        if (wait == 0) {
            int serial_tag = profile->serial_tag;
            if (serial_tag == kNoSerialTag || running_serial_tags_.find(serial_tag)
                        == running_serial_tags_.end()) {
                min_wait_time_iter = it;
                break;
            }
        } else if (wait < min_wait_time) {
            min_wait_time = wait;
            min_wait_time_iter = it;
        }
        ++it;
    }
    if (min_wait_time_iter != last) {
        tasks_.erase(min_wait_time_iter);
        if (_old) {
            tasks_.push_back(_old);
        }
        return *min_wait_time_iter;
    }
    return nullptr;
}

uint64_t ThreadPool::__ComputeWaitTime(TaskProfile *_profile, uint64_t _now) {
    if (!_profile) { return -1; }
    
    int64_t ret = 0;
    _now = _now == 0 ? ::gettickcount() : _now;
    
    if (_profile->type == TaskProfile::kAfter) {
        ret = _profile->record + _profile->after - _now;
    } else if (_profile->type == TaskProfile::kPeriodic) {
        ret = _profile->record + _profile->period - _now;
    }
    return ret > 0 ? ret : 0;
}

ThreadPool::~ThreadPool() {
    {
        LockGuard lock(mutex_);
        stop_ = true;
        for (TaskPairPtr &task_pair : tasks_) {
            delete task_pair, task_pair = nullptr;
        }
        tasks_.clear();
    }
    cv_.notify_all();
    for (std::thread &thread : workers_) {
        thread.join();
    }
}
