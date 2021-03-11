#ifndef THREADPOOL_H
#define THREADPOOL_H

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
        return __AddTask(TaskProfile::TTiming::kImmediate, -1, 0, 0, _f, _args...);
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
        return __AddTask(TaskProfile::TTiming::kAfter, -1, _after_millis, 0, _f, _args...);
    }
    
    template<class F, class... Args>
    void ExecutePeriodic(int _period_millis, F&& _f, Args&&... _args) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.push_back(new std::pair<TaskProfile, std::function<void()>>(
                    TaskProfile(TaskProfile::TTiming::kPeriodic, -1,
                                0, _period_millis), [=] { _f(_args...); }));
        }
        cv_.notify_one();
    }
    
  private:

    using ScopeLock = std::unique_lock<std::mutex>;
    using TaskPairPtr = std::pair<TaskProfile, std::function<void()>> *;

    explicit ThreadPool(size_t _n_threads = 4);

    /**
     *
     * A task is considered Faster than the given {@param _old},
     * only if the task meets either of the following conditions:
     *          1. The task is kImmediate, or
     *          2. The task is kAfter or kPeriodic and
     *             expires earlier than the given {@param _old}.
     *
     * If a faster task is found, it will be picked out from the task queue,
     * while the {@param _old} will be put back to the queue if it is not NULL.
     *
     * @param _old: Such task will be compared to the others in the task queue.
     *              If it is NULL, any task is faster than the given {@param _old}.
     * @return: if {@param _old} is kImmediate, return NULL, because no task is faster than a kImmediate one,
     *          else return pointer of the kImmediate task if exists,
     *          else return the pointer of task with the minimum time to wait until its (next) execution if exists,
     *          else return NULL, indicating that there is no task faster.
     */
    TaskPairPtr __PickOutTaskFasterThan(TaskPairPtr _old = NULL);
    

    void __CreateWorkerThread();

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
            std::unique_lock<std::mutex> lock(mutex_);
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
    std::mutex                                                      mutex_;
    std::condition_variable                                         cv_;
    bool                                                            stop_;
    static uint64_t const                                           kUInt64MaxValue;
};

#endif //THREADPOOL_H
