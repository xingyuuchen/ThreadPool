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

                        size_t curr_size = tasks_.size();
                        bool is_new = this->cv_.wait_for(lock, std::chrono::milliseconds(wait_time),
                                [&, this] { return this->__IsHigherPriorityTaskAddWhenWaiting(profile, curr_size); });
                        if (is_new) {  // Comes a task that needs to be done earlier
                            continue;
                        }
                        break;
                    }
                    // no task needs to be executed or will be executed
                    return;
                }
    
                this->running_serial_tags_.insert(profile->serial_tag);
                task = std::move(this->tasks_[idx].second);
                if (profile->type == TaskProfile::kPeriodic) {
                    profile->record = ::gettickcount();
                    profile->seq = TaskProfile::__MakeSeq();
                } else {
                    tasks_.erase(tasks_.begin() + idx);
                }
            }
            task();
            {
                ScopeLock lock(this->mutex_);
                this->running_serial_tags_.erase(profile->serial_tag);
                if (profile->type == TaskProfile::kPeriodic) {
                    tasks_[idx].second = std::move(task);
                } else {
                    delete profile;
                }
            }
        }
    });
}

ssize_t ThreadPool::__SelectTask() {
    static uint64_t last_selected_seq = TaskProfile::kInvalidSeq;
    
    uint64_t now = ::gettickcount();
    uint64_t min_wait_time = 0xffffffffffffffff;
    ssize_t min_wait_time_task_idx = -1;
    
    for (size_t idx = 0; idx < tasks_.size(); ++idx) {
        TaskProfile *profile = tasks_[idx].first;

        if (profile->type == TaskProfile::kImmediate) {
            int serial_tag = profile->serial_tag;
            if (serial_tag < 0 || running_serial_tags_.find(serial_tag)
                      == running_serial_tags_.end()) {
                if (profile->seq != last_selected_seq) {
                    last_selected_seq = profile->seq;
                    return idx;
                }
            }

        } else {
            uint64_t wait_time = __ComputeWaitTime(profile, now);
    
            if (wait_time == 0) {
                if (last_selected_seq != idx) {
                    last_selected_seq = idx;
                    return idx;
                }
                continue;
            }
            if (wait_time < min_wait_time) {
                min_wait_time = wait_time;
                min_wait_time_task_idx = idx;
            }
        }
    }
    if (min_wait_time_task_idx != -1) {
        if (last_selected_seq == tasks_[min_wait_time_task_idx].first->seq) {
            return -1;
        }
        last_selected_seq = tasks_[min_wait_time_task_idx].first->seq;
    }
    return min_wait_time_task_idx;
}

bool ThreadPool::__IsHigherPriorityTaskAddWhenWaiting(TaskProfile *_lhs, size_t _old_n_tasks)  {
    if (tasks_.size() == _old_n_tasks) {
        return false;
    }
    TaskProfile *rhs = tasks_.back().first;
    if (rhs->type == TaskProfile::kImmediate) {
        return true;
    }
    uint64_t now = ::gettickcount();
    return __ComputeWaitTime(rhs, now) < __ComputeWaitTime(_lhs, now);
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
        for (auto & task : tasks_) {
            TaskProfile *profile = task.first;
            if (profile) {
                delete profile;
                task.first = NULL;
            }
        }
    }
}
