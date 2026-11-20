#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace stui::native {

// Owns one bounded CPU/device worker. Destroy before the objects captured by tasks.
class NativeThumbnailWorker {
public:
    NativeThumbnailWorker() : m_thread([this](std::stop_token stop) {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(m_mutex);
                m_ready.wait(lock, [&] { return stop.stop_requested() || !m_tasks.empty(); });
                if (stop.stop_requested()) return;
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
            }
            // Tasks report failures at their typed completion boundary.
            try { task(); } catch (...) {}
        }
    }) {}
    ~NativeThumbnailWorker() { requestStop(); }
    NativeThumbnailWorker(const NativeThumbnailWorker&) = delete;
    NativeThumbnailWorker& operator=(const NativeThumbnailWorker&) = delete;

    void requestStop() noexcept {
        {
            std::scoped_lock lock(m_mutex);
            m_thread.request_stop();
        }
        m_ready.notify_all();
    }
    void submit(std::function<void()> task) {
        {
            std::scoped_lock lock(m_mutex);
            if (m_thread.get_stop_token().stop_requested() || m_tasks.size() >= 16)
                throw std::runtime_error("Thumbnail worker queue unavailable");
            m_tasks.push_back(std::move(task));
        }
        m_ready.notify_one();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<std::function<void()>> m_tasks;
    // Last member: joins before queue and synchronization members are destroyed.
    std::jthread m_thread;
};

}  // namespace stui::native
