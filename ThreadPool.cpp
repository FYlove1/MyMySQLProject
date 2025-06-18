#include "ThreadPool.h"
#include <iostream>

// 初始化静态成员
ThreadPool* ThreadPool::instance = nullptr;
std::mutex ThreadPool::instanceMutex;

ThreadPool& ThreadPool::getInstance(size_t numThreads) {
    if (instance == nullptr) {
        std::lock_guard<std::mutex> lock(instanceMutex);
        if (instance == nullptr) {
            instance = new ThreadPool(numThreads);
        }
    }
    return *instance;
}

ThreadPool::ThreadPool(size_t numThreads)
    : stopFlag(false), maxQueueSize(std::numeric_limits<size_t>::max()) {
    for(size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] {
            for(;;) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(this->queueMutex);
                    this->condition.wait(lock, [this]{ return this->stopFlag || !this->tasks.empty(); });

                    if(this->stopFlag && this->tasks.empty()) {
                        return;
                    }

                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }

                try {
                    task();
                } catch(const std::exception& e) {
                    std::cerr << "ThreadPool task exception: " << e.what() << std::endl;
                } catch(...) {
                    std::cerr << "ThreadPool task unknown exception" << std::endl;
                }
            }
        });
    }

    std::cout << "ThreadPool initialized with " << numThreads << " threads" << std::endl;
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stopFlag = true;
    }
    condition.notify_all();

    for(std::thread &worker: workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }

    std::cout << "ThreadPool stopped" << std::endl;
}

size_t ThreadPool::getQueueSize() const {
    std::unique_lock<std::mutex> lock(queueMutex);
    return tasks.size();
}

size_t ThreadPool::getMaxQueueSize() const {
    return maxQueueSize;
}

void ThreadPool::setMaxQueueSize(size_t size) {
    maxQueueSize = size;
    std::cout << "任务队列最大容量设置为: " << size << std::endl;
}