#include "ThreadPool.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

ThreadPool::ThreadPool(std::size_t threadCount)
    : stopping_(false) {
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

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("cannot enqueue task after thread pool stop");
        }

        tasks_.push(std::move(task));
    }

    condition_.notify_one();
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
        } catch (...) {
            // Keep worker threads alive even if a task fails unexpectedly.
        }
    }
}
