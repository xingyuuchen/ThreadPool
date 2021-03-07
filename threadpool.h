#ifndef OI_SVR_THREADPOOL_H
#define OI_SVR_THREADPOOL_H

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <unordered_set>
#include <chrono>
#include <iomanip>


inline uint64_t gettickcount() {
    using namespace std::chrono;
    time_point<std::chrono::system_clock, milliseconds> tp =
            time_point_cast<milliseconds>(system_clock::now());
    return tp.time_since_epoch().count();
}

struct TaskProfile {
    enum TTiming {
        kImmediate,
        kAfter,
        kPeriodic,
    };
    TaskProfile(TTiming _timing, int _serial_tag, int _after, int _period)
            : type(_timing), serial_tag(_serial_tag), after(_after), period(_period), seq(__MakeSeq()) {
        if (type != kImmediate) { record = ::gettickcount(); }
    }
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
    
    
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    Execute(F&& _f, Args&&... _args) {
        TaskProfile *profile = new TaskProfile(TaskProfile::TTiming::kImmediate, -1, 0, 0);
        return __AddTask(profile, _f, _args...);
    }
    
    /**
     * @param _serial_tag: tasks with the same _serial_tag(>0) execute serially.
     */
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    Execute(int _serial_tag, F&& _f, Args&&... _args) {
        TaskProfile *profile = new TaskProfile(TaskProfile::TTiming::kImmediate, _serial_tag, 0, 0);
        return __AddTask(profile, _f, _args...);
    }
    
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    ExecuteAfter(int _after_millis, F&& _f, Args&&... _args) {
        TaskProfile *profile = new TaskProfile(TaskProfile::TTiming::kAfter, -1, _after_millis, 0);
        return __AddTask(profile, _f, _args...);
    }
    
    template<class F, class... Args>
    void ExecutePeriodic(int _period_millis, F&& _f, Args&&... _args) {
        TaskProfile *profile = new TaskProfile(TaskProfile::TTiming::kPeriodic, -1, 0, _period_millis);
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.push_back(std::make_pair(profile, [=] { _f(_args...); }));
        }
        cv_.notify_one();
    }
    
  private:

    using ScopeLock = std::unique_lock<std::mutex>;

    ThreadPool(size_t _n_threads = 4);

    /**
     *
     * @return:  the index of kImmediate task if exists, else the index of task with
     *           the minimum time to wait until its (next) execution if exists, else -1.
     */
    ssize_t __SelectTask();
    
    /**
     *
     * @return: true if a new task which is
     *                  1. kImmediate or
     *                  2. kAfter with less waiting time
     *          is added when a specific thread waits for the expiration of a kAfter task.
     */
    bool __IsHigherPriorityTaskAddWhenWaiting(TaskProfile* _lhs, size_t _old_n_tasks);

    void __CreateWorkerThread();

    /**
     * @param _now: if not given, it will be updated inside the function.
     */
    uint64_t __ComputeWaitTime(TaskProfile *_profile, uint64_t _now = 0);


    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type>
    __AddTask(TaskProfile *_profile, F&& _f, Args&&... _args) {
        using return_t = typename std::result_of<F(Args...)>::type;
        using pack_task_t = std::packaged_task<return_t(void)>;
        
        auto task = std::make_shared<pack_task_t>(std::bind(_f, _args...));
        std::future<return_t> ret = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.push_back(std::make_pair(_profile, [=] { (*task)(); }));
        }
        cv_.notify_one();
        return ret;
    }


  private:
    std::vector<std::thread>                                        workers_;
    std::vector<std::pair<TaskProfile*, std::function<void()>>>     tasks_;
    std::unordered_set<int>                                         running_serial_tags_;
    std::mutex                                                      mutex_;
    std::condition_variable                                         cv_;
    bool                                                            stop_;
    
};

#endif //OI_SVR_THREADPOOL_H
