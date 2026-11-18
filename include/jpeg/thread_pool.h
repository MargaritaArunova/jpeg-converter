#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <future>
#include <stdexcept>

namespace jpeg {

// Пул потоков фиксированного размера, реализующий паттерн producer-consumer.
//
// Рабочие потоки спят на condition_variable до появления задачи в очереди.
// Деструктор устанавливает флаг остановки, будит всех рабочих и дожидается
// завершения каждого потока — нет detached-потоков, нет утечек ресурсов.
//
// Используемые примитивы синхронизации:
//   std::mutex              -- защищает очередь задач
//   std::condition_variable -- уведомляет рабочих о появлении новой задачи
//   std::thread             -- рабочие потоки уровня ОС
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // Помещает вызываемый объект с аргументами в очередь и возвращает future для результата.
    // Потокобезопасен: можно вызывать из любого потока.
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    size_t size() const noexcept { return workers_.size(); }

    // Не копируется и не перемещается: владеет потоками ОС.
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    std::vector<std::thread>        workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                      queue_mutex_;
    std::condition_variable         condition_;
    bool                            stop_ = false;
};

// Шаблонная реализация должна быть в заголовочном файле.
template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using R = std::invoke_result_t<F, Args...>;

    // Оборачиваем вызов в packaged_task, чтобы получить future для результата.
    auto task = std::make_shared<std::packaged_task<R()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    std::future<R> fut = task->get_future();

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_) throw std::runtime_error("ThreadPool: submit после завершения пула");
        tasks_.push([task]() { (*task)(); });
    }
    condition_.notify_one();  // будим ровно одного спящего рабочего
    return fut;
}

} // namespace jpeg
