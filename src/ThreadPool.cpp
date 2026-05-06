#include "ThreadPool.h"

#include "Logger.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

ThreadPool::ThreadPool(std::size_t threadCount, std::size_t maxQueueSize)
    : maxQueueSize_(std::max<std::size_t>(1, maxQueueSize)),
      stopping_(false) {
    const std::size_t count = std::max<std::size_t>(1, threadCount);
    workers_.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        workers_.emplace_back([this]() {
            workerLoop();
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }

    condition_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

bool ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || tasks_.size() >= maxQueueSize_) {
            return false;
        }

        tasks_.push(std::move(task));
    }

    condition_.notify_one();
    return true;
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() {
                return stopping_ || !tasks_.empty();
            });

            if (stopping_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try {
            task();
        } catch (const std::exception& exception) {
            Logger::error("thread pool task failed: " + std::string(exception.what()));
        } catch (...) {
            Logger::error("thread pool task failed with unknown exception");
        }
    }
}
