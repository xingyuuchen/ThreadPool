#pragma once

#include <vector>
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <unordered_set>
#include <chrono>
#include <iomanip>


struct TaskProfile {
    enum TTiming {
        kImmediate,
        kAfter,
        kPeriodic,
    };
    TaskProfile(TTiming _timing, int _serial_tag, int _after, int _period);
    static uint64_t __MakeSeq() {
        static uint64_t seq = 0;
        return ++seq;
    }
    TTiming     type;
    int         serial_tag;
    int         after;
    int         period;
    uint64_t    record; // for kAfter:creating ts ; for kPeriodic:last running ts.
    uint64_t    seq;
    static const uint64_t kInvalidSeq = 0;
};

class ThreadPool {
  public:
    void operator=(ThreadPool const &) = delete;
    ThreadPool(ThreadPool const &) = delete;
    ~ThreadPool();
    
    static ThreadPool &Instance() {
        static ThreadPool instance;
        return instance;
    }
    
    /**
     * Initializes ThreadPool singleton in advance,
     * instead of lazy loading when first used.
     */
    void Init();
    
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    Execute(F&& _f, Args&&... _args) {
        return __AddTask(TaskProfile::TTiming::kImmediate, kNoSerialTag, 0, 0, _f, _args...);
    }
    
    /**
     * @param _serial_tag: tasks with the same _serial_tag(>0) execute serially.
     */
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    Execute(int _serial_tag, F&& _f, Args&&... _args) {
        return __AddTask(TaskProfile::TTiming::kImmediate, _serial_tag, 0, 0, _f, _args...);
    }
    
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    ExecuteAfter(int _after_millis, F&& _f, Args&&... _args) {
        return __AddTask(TaskProfile::TTiming::kAfter, kNoSerialTag, _after_millis, 0, _f, _args...);
    }
    
    template<class F, class... Args>
    void ExecutePeriodic(int _period_millis, F&& _f, Args&&... _args) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.push_back(new std::pair<TaskProfile, std::function<void()>>(
                    TaskProfile(TaskProfile::TTiming::kPeriodic, kNoSerialTag,
                                0, _period_millis), [=] { _f(_args...); }));
        }
        cv_.notify_one();
    }
    
  private:

    using UniqueLock = std::unique_lock<std::mutex>;
    using LockGuard = std::lock_guard<std::mutex>;
    using TaskPairPtr = std::pair<TaskProfile, std::function<void()>> *;

    explicit ThreadPool(size_t _n_threads = 4);

    /**
     *
     * A task is considered Faster than another,
     * only if the task meets either of the following conditions:
     *          1. The task is kImmediate and the other is not, or
     *          2. The task is kAfter or kPeriodic and expires earlier than the other.
     *
     * If a faster task is found, it will be picked out from the task queue,
     * while the {@param _old} will be push back to the queue if it is not NULL.
     *
     * @param _old: Such task will be compared to the others in the task queue.
     *              If it is NULL, any task is faster than the given {@param _old}.
     * @return: if {@param _old} is kImmediate, return NULL, because no task is faster than a kImmediate one,
     *          else return pointer of the kImmediate task if exists,
     *          else return the pointer of the task with the minimum time to wait
     *          until its (next) execution if exists,
     *          else return NULL, indicating that there is no task faster.
     */
    TaskPairPtr __PickOutTaskFasterThan(TaskPairPtr _old = nullptr);
    

    void __WorkerEntry();

    /**
     * @param _now: if not given, it will be updated inside the function.
     */
    uint64_t __ComputeWaitTime(TaskProfile *_profile, uint64_t _now = 0);


    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    __AddTask(TaskProfile::TTiming _timing, int _serial_tag, int _after, int _period, F&& _f, Args&&... _args) {
        using return_t = typename std::result_of<F(Args...)>::type;
        using pack_task_t = std::packaged_task<return_t(void)>;
        
        auto task = std::make_shared<pack_task_t>(std::bind(_f, _args...));
        std::future<return_t> ret = task->get_future();
        {
            LockGuard lock(mutex_);
            tasks_.push_back(new std::pair<TaskProfile, std::function<void()>>(
                    TaskProfile(_timing, _serial_tag, _after, _period), [=] { (*task)(); }));
        }
        cv_.notify_one();
        return ret;
    }


  private:
    std::list<std::pair<TaskProfile, std::function<void()>>*>       tasks_;
    std::vector<std::thread>                                        workers_;
    std::unordered_set<int>                                         running_serial_tags_;
    const static int                                                kNoSerialTag;
    std::mutex                                                      mutex_;
    std::condition_variable                                         cv_;
    bool                                                            stop_;
};

