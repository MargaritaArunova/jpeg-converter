#include "jpeg/thread_pool.h"

namespace jpeg {

ThreadPool::ThreadPool(size_t num_threads) {
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            // Цикл рабочего потока: спим до появления задачи или команды остановки.
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    // Ждём, пока очередь не станет непустой или не придёт сигнал завершения.
                    condition_.wait(lock, [this] {
                        return stop_ || !tasks_.empty();
                    });

                    if (stop_ && tasks_.empty()) return; // чистое завершение: все задачи выполнены

                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task(); // выполняем задачу вне блокировки, чтобы не мешать другим потокам
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    // Будим все спящие потоки, чтобы они увидели stop_ == true и завершились.
    condition_.notify_all();
    for (auto& w : workers_) w.join();
}

} // namespace jpeg
