#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace stui::textures {

using TextureCacheClock = std::chrono::steady_clock;
using TextureCacheTimePoint = TextureCacheClock::time_point;
using TextureCacheDuration = TextureCacheClock::duration;

template <class Resource>
class TextureCache {
public:
    explicit TextureCache(
        std::size_t capacity,
        TextureCacheDuration idleTtl = TextureCacheDuration::max(),
        std::size_t maxBytes = SIZE_MAX,
        std::function<std::size_t(const Resource&)> resourceBytes = {})
        : m_capacity(capacity), m_idleTtl(idleTtl), m_maxBytes(maxBytes),
          m_resourceBytes(std::move(resourceBytes)) {}

    [[nodiscard]] std::shared_ptr<Resource> get(
        std::string_view path,
        TextureCacheTimePoint now = TextureCacheClock::now()) {
        const auto found = m_entries.find(std::string(path));
        if (found == m_entries.end()) {
            return nullptr;
        }

        m_recency.splice(m_recency.begin(), m_recency, found->second.recency);
        found->second.lastAccess = now;
        return found->second.resource;
    }

    template <class Loader>
    std::shared_ptr<Resource> getOrLoad(
        std::string_view path,
        Loader&& loader,
        TextureCacheTimePoint now = TextureCacheClock::now()) {
        if (auto cached = get(path, now)) {
            return cached;
        }

        auto resource = std::invoke(std::forward<Loader>(loader));
        if (!resource) {
            return nullptr;
        }
        const auto bytes = m_resourceBytes ? m_resourceBytes(*resource) : 0;
        if (m_capacity == 0 || bytes > m_maxBytes) return resource;
        m_recency.emplace_front(path);
        m_entries.emplace(
            m_recency.front(), Entry{resource, m_recency.begin(), now, bytes});
        m_bytes += bytes;
        while (m_entries.size() > m_capacity || m_bytes > m_maxBytes) {
            m_bytes -= m_entries.at(m_recency.back()).bytes;
            m_entries.erase(m_recency.back());
            m_recency.pop_back();
        }
        return resource;
    }

    void pruneExpired(TextureCacheTimePoint now = TextureCacheClock::now()) {
        if (m_idleTtl == TextureCacheDuration::max()) {
            return;
        }

        for (auto entry = m_entries.begin(); entry != m_entries.end();) {
            if (now - entry->second.lastAccess >= m_idleTtl) {
                m_bytes -= entry->second.bytes;
                m_recency.erase(entry->second.recency);
                entry = m_entries.erase(entry);
            } else {
                ++entry;
            }
        }
    }

    void clear() noexcept {
        m_entries.clear();
        m_recency.clear();
        m_bytes = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }
    [[nodiscard]] std::size_t bytes() const noexcept { return m_bytes; }

private:
    struct Entry {
        std::shared_ptr<Resource> resource;
        typename std::list<std::string>::iterator recency;
        TextureCacheTimePoint lastAccess;
        std::size_t bytes;
    };

    std::size_t m_capacity;
    TextureCacheDuration m_idleTtl;
    std::size_t m_maxBytes;
    std::function<std::size_t(const Resource&)> m_resourceBytes;
    std::size_t m_bytes{};
    std::list<std::string> m_recency;
    std::unordered_map<std::string, Entry> m_entries;
};

}  // namespace stui::textures
