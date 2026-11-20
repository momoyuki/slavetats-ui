#pragma once

#include "textures/TextureResolver.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace stui::textures {

// Only call read() from a worker: engine resource access stays on its scheduler.
class ScheduledTextureArchiveReader {
public:
    using Scheduler = std::function<void(std::function<void()>)>;
    ScheduledTextureArchiveReader(Scheduler schedule, TextureArchiveReader read,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
        : m_schedule(std::move(schedule)), m_read(std::move(read)), m_timeout(timeout) {}

    TextureBytesResult read(std::string_view path) const {
        struct Pending {
            std::mutex mutex;
            std::condition_variable ready;
            bool expired{};
            bool started{};
            bool complete{};
            TextureBytesResult result = std::unexpected(TextureResolveError::readFailed);
        };
        auto pending = std::make_shared<Pending>();
        try {
            m_schedule([pending, path = std::string(path), read = m_read] {
                {
                    std::scoped_lock lock(pending->mutex);
                    if (pending->expired) return;
                    pending->started = true;
                }
                pending->ready.notify_one();
                TextureBytesResult result = std::unexpected(TextureResolveError::readFailed);
                try { result = read(path); } catch (...) {}
                {
                    std::scoped_lock lock(pending->mutex);
                    pending->result = std::move(result);
                    pending->complete = true;
                }
                pending->ready.notify_one();
            });
        } catch (...) { return std::unexpected(TextureResolveError::readFailed); }
        std::unique_lock lock(pending->mutex);
        // Queue budget covers only admission delay. Once the engine task starts,
        // wait for the transfer so late bytes are not discarded and retried forever.
        if (!pending->ready.wait_for(lock, m_timeout,
                [&] { return pending->complete || pending->started; })) {
            if (!pending->started) {
                pending->expired = true;
                return std::unexpected(TextureResolveError::temporarilyUnavailable);
            }
        }
        if (!pending->complete) {
            pending->ready.wait(lock, [&] { return pending->complete; });
        }
        return std::move(pending->result);
    }
private:
    Scheduler m_schedule;
    TextureArchiveReader m_read;
    std::chrono::milliseconds m_timeout;
};

}  // namespace stui::textures
