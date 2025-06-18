#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>
#include <limits>

class ThreadPool {
public:
    static ThreadPool& getInstance(size_t numThreads = std::thread::hardware_concurrency());

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // 修改返回类型，使用C++14语法
    template<typename F, typename... Args>
    std::pair<bool, std::future<typename std::decay<decltype(std::declval<F>()(std::declval<Args>()...))>::type>>
    enqueue(F&& f, Args&&... args);

    void stop();
    size_t getQueueSize() const;
    size_t getMaxQueueSize() const;
    void setMaxQueueSize(size_t size);

private:
    ThreadPool(size_t numThreads);
    ~ThreadPool();

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    mutable std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stopFlag;
    size_t maxQueueSize;

    static ThreadPool* instance;
    static std::mutex instanceMutex;
};

// 模板实现
template<typename F, typename... Args>
std::pair<bool, std::future<typename std::decay<decltype(std::declval<F>()(std::declval<Args>()...))>::type>>
ThreadPool::enqueue(F&& f, Args&&... args) {
    using return_type = typename std::decay<decltype(std::declval<F>()(std::declval<Args>()...))>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queueMutex);

        if(stopFlag) {
            return std::make_pair(false, std::future<return_type>());
        }

        if(tasks.size() >= maxQueueSize) {
            return std::make_pair(false, std::move(res));
        }

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return std::make_pair(true, std::move(res));
}

#endif // THREADPOOL_H