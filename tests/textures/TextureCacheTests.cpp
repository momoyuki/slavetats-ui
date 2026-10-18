#include "textures/TextureCache.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void run(std::string_view name, void (*test)()) {
    try {
        test();
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        std::exit(1);
    }
}

void reusesCachedTextureWithoutLoadingAgain() {
    stui::textures::TextureCache<int> cache(2);
    int loadCount = 0;
    const auto loader = [&]() {
        ++loadCount;
        return std::make_shared<int>(42);
    };

    const auto first = cache.getOrLoad("pack\\tattoo.dds", loader);
    const auto second = cache.getOrLoad("pack\\tattoo.dds", loader);

    expect(first == second, "expected the cached resource instance");
    expect(loadCount == 1, "expected one resource load");
    expect(cache.size() == 1, "expected one cache entry");
}

void evictsLeastRecentlyUsedTextureAtCapacity() {
    stui::textures::TextureCache<int> cache(2);
    int nextValue = 0;
    const auto load = [&]() {
        return std::make_shared<int>(++nextValue);
    };

    const auto firstA = cache.getOrLoad("a.dds", load);
    const auto firstB = cache.getOrLoad("b.dds", load);
    const auto recentA = cache.getOrLoad("a.dds", load);
    cache.getOrLoad("c.dds", load);
    const auto secondB = cache.getOrLoad("b.dds", load);

    expect(firstA == recentA, "expected cache hit to refresh recency");
    expect(firstB != secondB, "expected least recently used texture eviction");
    expect(nextValue == 4, "expected evicted texture to load again");
    expect(cache.size() == 2, "expected cache to remain within capacity");
}

void doesNotCacheFailedTextureLoad() {
    stui::textures::TextureCache<int> cache(2);
    int loadCount = 0;
    const auto fail = [&]() -> std::shared_ptr<int> {
        ++loadCount;
        return nullptr;
    };

    const auto first = cache.getOrLoad("broken.dds", fail);
    const auto second = cache.getOrLoad("broken.dds", fail);
    expect(!first && !second, "expected texture load failure to reach caller");

    expect(loadCount == 2, "expected failed texture load to be retried");
    expect(cache.size() == 0, "expected failed texture to stay out of cache");
}

void expiresIdleEntryAtTtlWhileVisibleReferenceRetainsResource() {
    using namespace std::chrono_literals;
    const auto start = stui::textures::TextureCacheTimePoint{};
    stui::textures::TextureCache<int> cache(2, 2min);
    auto visible = cache.getOrLoad(
        "visible.dds", [] { return std::make_shared<int>(7); }, start);
    std::weak_ptr<int> resource = visible;

    cache.pruneExpired(start + 2min);
    expect(cache.size() == 0, "expected idle entry removed at TTL");
    expect(!resource.expired(), "expected visible reference to retain resource");

    visible.reset();
    expect(resource.expired(), "expected final visible release to destroy resource");
}

void refreshesIdleExpiryWhenTextureIsAccessed() {
    using namespace std::chrono_literals;
    const auto start = stui::textures::TextureCacheTimePoint{};
    stui::textures::TextureCache<int> cache(2, 2min);
    cache.getOrLoad("visible.dds", [] { return std::make_shared<int>(7); }, start);

    const auto refreshed = cache.get("visible.dds", start + 90s);
    cache.pruneExpired(start + 3min + 29s);

    expect(refreshed != nullptr, "expected cache access to return resource");
    expect(cache.size() == 1, "expected access to refresh idle expiry");

    cache.pruneExpired(start + 3min + 30s);
    expect(cache.size() == 0, "expected entry to expire from refreshed access time");
}

void clearsLookupState() {
    stui::textures::TextureCache<int> cache(2);
    cache.getOrLoad("first.dds", [] { return std::make_shared<int>(1); });
    cache.getOrLoad("second.dds", [] { return std::make_shared<int>(2); });

    cache.clear();

    expect(cache.size() == 0, "expected clear to empty lookup state");
    expect(cache.get("first.dds") == nullptr, "expected cleared entry lookup to miss");
}

void preservesLegacyNoExpiryBehaviorByDefault() {
    using namespace std::chrono_literals;
    const auto start = stui::textures::TextureCacheTimePoint{};
    stui::textures::TextureCache<int> cache(2, stui::textures::TextureCacheDuration::max());
    cache.getOrLoad("legacy.dds", [] { return std::make_shared<int>(7); }, start);

    cache.pruneExpired(start + std::chrono::hours(24 * 365));

    expect(cache.size() == 1, "expected maximum TTL to preserve legacy no-expiry behavior");
}

void limitsCachedBytesAndKeepsRecentEntries() {
    stui::textures::TextureCache<int> cache(10,
        stui::textures::TextureCacheDuration::max(), 7,
        [](const int& value) { return static_cast<std::size_t>(value); });
    cache.getOrLoad("a", [] { return std::make_shared<int>(3); });
    cache.getOrLoad("b", [] { return std::make_shared<int>(4); });
    expect(cache.get("a") != nullptr, "recent entry should be readable");
    cache.getOrLoad("c", [] { return std::make_shared<int>(3); });
    expect(cache.get("b") == nullptr, "byte budget should evict least recent entry");
    expect(cache.bytes() == 6 && cache.size() == 2,
        "cached bytes should account for retained entries only");
}

void doesNotRetainOversizedEntryAndResetsByteAccounting() {
    using namespace std::chrono_literals;
    const auto start = stui::textures::TextureCacheTimePoint{};
    stui::textures::TextureCache<int> cache(10, 2min, 7,
        [](const int& value) { return static_cast<std::size_t>(value); });
    auto large = cache.getOrLoad("large", [] { return std::make_shared<int>(8); }, start);
    expect(large && cache.size() == 0 && cache.bytes() == 0,
        "oversized image should be available without entering cache");
    cache.getOrLoad("small", [] { return std::make_shared<int>(4); }, start);
    cache.pruneExpired(start + 2min);
    expect(cache.bytes() == 0, "expiry should release byte accounting");
    cache.getOrLoad("small", [] { return std::make_shared<int>(4); }, start);
    cache.clear();
    expect(cache.bytes() == 0, "clear should reset byte accounting");
}

}  // namespace

int main() {
    run("cache hit skips duplicate texture load", reusesCachedTextureWithoutLoadingAgain);
    run("capacity evicts least recently used texture", evictsLeastRecentlyUsedTextureAtCapacity);
    run("failed texture load is not cached", doesNotCacheFailedTextureLoad);
    run("idle TTL removes cache ownership", expiresIdleEntryAtTtlWhileVisibleReferenceRetainsResource);
    run("access refreshes idle TTL", refreshesIdleExpiryWhenTextureIsAccessed);
    run("clear removes cache lookup state", clearsLookupState);
    run("maximum TTL disables idle expiry", preservesLegacyNoExpiryBehaviorByDefault);
    run("cache honors byte budget and recency", limitsCachedBytesAndKeepsRecentEntries);
    run("oversized and expired entries do not consume cache budget",
        doesNotRetainOversizedEntryAndResetsByteAccounting);
    return 0;
}
