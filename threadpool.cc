#include "threadpool.h"

ThreadPool::ThreadPool(size_t _n_threads)
        : stop_(false) {
    
    while (_n_threads--) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                size_t idx;
                int serial_tag;
                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    
                    this->cv_.wait(lock, [&, this] {
                        // see if there is any task to do.
                        for (idx = 0; idx < this->tasks_.size(); ++idx) {
                            serial_tag = this->tasks_[idx].first;
                            if (serial_tag < 0 || this->running_serial_tags_.find(serial_tag)
                                      == this->running_serial_tags_.end()) {
                                return true;
                            }
                        }
                        return this->stop_;
                    });
                    
                    if (this->stop_ && idx == this->tasks_.size()) { return; }
    
                    this->running_serial_tags_.insert(serial_tag);
                    task = std::move(this->tasks_[idx].second);
                    this->tasks_.erase(this->tasks_.begin() + idx);
                }
                task();
                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    this->running_serial_tags_.erase(serial_tag);
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (std::thread &thread : workers_) {
        thread.join();
    }
}
