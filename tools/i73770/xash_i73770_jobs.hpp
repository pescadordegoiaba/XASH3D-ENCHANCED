#pragma once

/*
 * Job system C++11 header-only para futuras integrações.
 *
 * Uso planejado:
 *  - carregar recursos em paralelo;
 *  - preparar listas de render/culling em paralelo;
 *  - dividir partículas/efeitos em blocos.
 *
 * Não inclua em hot-path sem antes medir com perf.
 */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace xash_i73770
{
    class JobSystem
    {
    public:
        explicit JobSystem(std::size_t threads = std::thread::hardware_concurrency())
        {
            if (threads == 0)
                threads = 1;

            stop_.store(false, std::memory_order_relaxed);

            for (std::size_t i = 0; i < threads; ++i)
            {
                workers_.emplace_back([this]()
                {
                    for (;;)
                    {
                        std::function<void()> job;

                        {
                            std::unique_lock<std::mutex> lock(mutex_);
                            cv_.wait(lock, [this]()
                            {
                                return stop_.load(std::memory_order_relaxed) || !jobs_.empty();
                            });

                            if (stop_.load(std::memory_order_relaxed) && jobs_.empty())
                                return;

                            job = std::move(jobs_.front());
                            jobs_.pop();
                        }

                        job();
                    }
                });
            }
        }

        ~JobSystem()
        {
            stop_.store(true, std::memory_order_relaxed);
            cv_.notify_all();

            for (std::thread &worker : workers_)
            {
                if (worker.joinable())
                    worker.join();
            }
        }

        void enqueue(std::function<void()> job)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                jobs_.push(std::move(job));
            }

            cv_.notify_one();
        }

    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> jobs_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::atomic<bool> stop_{false};
    };
}
