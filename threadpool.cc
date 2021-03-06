#include "threadpool.h"

ThreadPool::ThreadPool(size_t _n_threads)
        : stop_(false) {
    while (_n_threads--) {
        __CreateWorkerThread();
    }
}

void ThreadPool::__CreateWorkerThread() {
    workers_.emplace_back([this] {
        while (true) {
            std::function<void()> task;
            TaskProfile *profile;
            ssize_t idx;
            {
                ScopeLock lock(this->mutex_);
                while (true) {
                    this->cv_.wait(lock, [&, this] {
                        return (idx = this->__SelectTask()) >= 0 || this->stop_;
                    });
                    
                    if (idx >= 0) {
                        profile = this->tasks_[idx].first;

                        uint64_t wait_time = __ComputeWaitTime(profile);

                        if (wait_time == 0) { break; }

                        size_t old_idx = idx;
                        bool another = this->cv_.wait_for(lock, std::chrono::milliseconds(wait_time),
                                [&, this] { idx = this->__SelectTask(); return idx != old_idx; });
                        if (another) { continue; }
                        break;
                    }
                    // no task needs to be executed or will be executed
                    if (this->stop_) { return; }
                }
    
                this->running_serial_tags_.insert(profile->serial_tag);
                task = std::move(this->tasks_[idx].second);
                if (profile->type == TaskProfile::kPeriodic) { profile->record = ::gettickcount(); }
                else { tasks_.erase(tasks_.begin() + idx); }
            }
            task();
            {
                ScopeLock lock(this->mutex_);
                this->running_serial_tags_.erase(profile->serial_tag);
                if (profile->type != TaskProfile::kPeriodic) { delete profile; }
            }
        }
    });
}

ssize_t ThreadPool::__SelectTask() {
    int serial_tag;
    TaskProfile *profile;
    
    uint64_t now = ::gettickcount();
    uint64_t min_wait_time = 0xffffffffffffffff;
    ssize_t min_wait_time_task_idx = -1;
    
    for (size_t idx = 0; idx < tasks_.size(); ++idx) {
        profile = tasks_[idx].first;

        if (profile->type == TaskProfile::kImmediate) {
            serial_tag = profile->serial_tag;
            if (serial_tag < 0 || running_serial_tags_.find(serial_tag)
                      == running_serial_tags_.end()) {
                return idx;
            }

        } else {
            uint64_t wait_time = __ComputeWaitTime(profile, now);
    
            if (wait_time == 0) { return idx; }
            printf("oh %llu\n", wait_time);
            if (wait_time < min_wait_time) {
                min_wait_time = wait_time;
                min_wait_time_task_idx = idx;
            }
        }
    }
    return min_wait_time_task_idx;
}

void ThreadPool::__DeleteTask(size_t _idx) {
    if (_idx < 0 || _idx >= tasks_.size()) { return; }
    

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
        ScopeLock lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (std::thread &thread : workers_) {
        thread.join();
        printf("joined\n");
    }
    {
        ScopeLock lock(mutex_);
        for (size_t i = 0; i < tasks_.size(); ++i) {
            TaskProfile *profile = tasks_[i].first;
            if (profile) {
                delete profile;
                tasks_[i].first = NULL;
            }
        }
    }
}
