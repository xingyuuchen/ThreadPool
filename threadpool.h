#ifndef OI_SVR_THREADPOOL_H
#define OI_SVR_THREADPOOL_H

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <unordered_set>

class ThreadPool {
  public:
    void operator=(ThreadPool const &) = delete;
    ThreadPool(ThreadPool const &) = delete;
    ~ThreadPool();
    
    static ThreadPool &Instance() {
        static ThreadPool instance;
        return instance;
    }
    
    
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    Execute(F&& _f, Args&&... _args) {
        return Execute(-1, _f, _args...);
    }
    
    /**
     * @param _serial_tag: tasks with the same _serial_tag(>0) execute serially.
     */
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    Execute(int _serial_tag, F&& _f, Args&&... _args) {
        using return_t = typename std::result_of<F(Args...)>::type;
        using pack_task_t = std::packaged_task<return_t(void)>;
        
        auto task = std::make_shared<pack_task_t>(std::bind(_f, _args...));
        std::future<return_t> ret = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.push_back(std::make_pair(_serial_tag, [=] { (*task)(); }));
        }
        cv_.notify_one();
        return ret;
    }

  private:
    ThreadPool(size_t _n_threads = 4);

  private:
    std::vector<std::thread>                            workers_;
    std::vector<std::pair<int, std::function<void()>>>  tasks_;
    std::unordered_set<int>                             running_serial_tags_;
    std::mutex                                          mutex_;
    std::condition_variable                             cv_;
    bool                                                stop_;
    
};

#endif //OI_SVR_THREADPOOL_H
