#include "native/NativeThumbnailRuntime.h"

#include "repository/TattooCatalogLoader.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using stui::native::NativeThumbnailCancellationCheck;
using stui::native::NativeThumbnailFailure;
using stui::native::NativeThumbnailEpoch;
using stui::native::NativeThumbnailLoadResult;
using stui::native::NativeThumbnailRuntime;
using stui::native::NativeThumbnailStatus;
using stui::native::NativeThumbnailTask;
using stui::native::NativeThumbnailTextureSource;
using stui::repository::TattooCatalog;
using stui::repository::TattooCatalogSnapshot;
using stui::repository::TattooDefinition;
using stui::repository::TattooPage;
using stui::textures::D3D11Texture;
using stui::textures::TextureCacheTimePoint;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

TattooDefinition tattoo(std::string texturePath, std::size_t sourceIndex) {
    return TattooDefinition{
        .sourceId = "fixture.json",
        .sourceFile = "fixture.json",
        .packName = "Fixture Pack",
        .sourceIndex = sourceIndex,
        .name = "Entry " + std::to_string(sourceIndex),
        .section = "Marks",
        .texturePath = std::move(texturePath),
        .area = "Body",
    };
}

TattooPage pageOfSix(std::string_view prefix = "page-") {
    TattooPage page;
    page.entries.reserve(6);
    for (std::size_t index = 0; index < 6; ++index) {
        page.entries.push_back(tattoo(
            std::string(prefix) + std::to_string(index) + ".dds", index));
    }
    page.totalEntries = page.entries.size();
    page.matchedEntries = page.entries.size();
    page.pageSize = page.entries.size();
    page.pageCount = 1;
    return page;
}

TattooCatalogSnapshot snapshot(const TattooPage& page) {
    return std::make_shared<const TattooCatalog>(TattooCatalog{
        .repository = stui::repository::TattooRepository(page.entries),
        .sourceCount = 1,
    });
}

std::shared_ptr<D3D11Texture> readyTexture() {
    return std::make_shared<D3D11Texture>(D3D11Texture{.width = 64, .height = 64});
}

class FakeTextureSource final : public NativeThumbnailTextureSource {
public:
    std::shared_ptr<D3D11Texture> find(
        std::string_view texturePath,
        TextureCacheTimePoint now) override {
        findPaths.emplace_back(texturePath);
        findTimes.push_back(now);
        const auto found = cached.find(std::string(texturePath));
        return found == cached.end() ? nullptr : found->second;
    }

    NativeThumbnailLoadResult load(
        std::string_view texturePath,
        const NativeThumbnailCancellationCheck& isCurrent,
        TextureCacheTimePoint now) override {
        loadPaths.emplace_back(texturePath);
        loadTimes.push_back(now);
        if (throwOnLoad) {
            throwOnLoad = false;
            throw std::runtime_error("source failure");
        }
        const bool current = isCurrent();
        cancellationResults.push_back(current);
        if (!current) {
            return std::unexpected(NativeThumbnailFailure::cancelled);
        }
        return readyTexture();
    }

    void pruneExpired(TextureCacheTimePoint now) override {
        pruneTimes.push_back(now);
    }

    void clear() override {
        ++clearCount;
        cached.clear();
    }

    bool prepareDiskCache(std::string_view path) override {
        preparedPaths.emplace_back(path);
        return true;
    }
    std::uintmax_t diskCacheBytes() const override { return 65536; }

    bool available() const noexcept override {
        return isAvailable;
    }

    bool isAvailable{true};
    bool throwOnLoad{};
    std::unordered_map<std::string, std::shared_ptr<D3D11Texture>> cached;
    std::vector<std::string> findPaths;
    std::vector<TextureCacheTimePoint> findTimes;
    std::vector<std::string> loadPaths;
    std::vector<TextureCacheTimePoint> loadTimes;
    std::vector<bool> cancellationResults;
    std::vector<TextureCacheTimePoint> pruneTimes;
    std::size_t clearCount{};
    std::vector<std::string> preparedPaths;
};

struct RuntimeFixture {
    RuntimeFixture() {
        auto ownedSource = std::make_unique<FakeTextureSource>();
        source = ownedSource.get();
        runtime = std::make_unique<NativeThumbnailRuntime>(
            std::move(ownedSource),
            [this](NativeThumbnailTask task) {
                if (schedulerThrows) {
                    schedulerThrows = false;
                    throw std::runtime_error("scheduler failure");
                }
                scheduled.push_back(std::move(task));
            },
            [this] { return now; });
    }

    TextureCacheTimePoint now{};
    FakeTextureSource* source{};
    std::vector<NativeThumbnailTask> scheduled;
    bool schedulerThrows{};
    std::unique_ptr<NativeThumbnailRuntime> runtime;
};

void synchronizesPresentationNeutralVisiblePaths() {
    RuntimeFixture fixture;
    const NativeThumbnailEpoch epoch = std::make_shared<int>(1);
    const std::vector<std::string> paths{
        "slot/a.dds", "slot/b.dds", "slot/c.dds", "slot/d.dds",
        "slot/e.dds", "slot/f.dds", "slot/ignored.dds"};

    fixture.runtime->synchronize(epoch, paths);

    const auto views = fixture.runtime->views();
    expect(views.size() == paths.size() && views.front().texturePath == "slot/a.dds" &&
            views.back().texturePath == "slot/ignored.dds",
        "expected generic runtime synchronization to cover multiple visible gallery rows");
    expect(fixture.source->findPaths.size() == paths.size(),
        "expected generic runtime to share the existing texture source");
}

void schedulesOnlyOneRequestAtATime() {
    RuntimeFixture fixture;
    const auto page = pageOfSix();

    fixture.runtime->synchronize(snapshot(page), page);
    fixture.runtime->pump();
    expect(fixture.scheduled.size() == 1, "expected one scheduled thumbnail task");

    fixture.runtime->pump();
    expect(fixture.scheduled.size() == 1, "expected no second task while one is active");

    fixture.scheduled.front()();
    fixture.runtime->pump();
    expect(fixture.scheduled.size() == 2, "expected next request after completion");
}

void doesNotScheduleCacheHits() {
    RuntimeFixture fixture;
    const auto page = pageOfSix("cached-");
    const auto texture = readyTexture();
    for (const auto& entry : page.entries) {
        fixture.source->cached.emplace(entry.texturePath, texture);
    }

    fixture.runtime->synchronize(snapshot(page), page);
    fixture.runtime->pump();

    expect(fixture.scheduled.empty(), "expected no work for cache hits");
    const auto views = fixture.runtime->views();
    expect(views.size() == 6, "expected six cache-hit views");
    for (const auto& view : views) {
        expect(view.status == NativeThumbnailStatus::ready && view.texture == texture,
            "expected cache hit to publish ready texture");
    }
}

void prunesAtMostOncePerSecond() {
    RuntimeFixture fixture;

    fixture.runtime->pump();
    expect(fixture.source->pruneTimes.size() == 1, "expected initial expiry prune");

    fixture.now += 999ms;
    fixture.runtime->pump();
    expect(fixture.source->pruneTimes.size() == 1, "expected no prune before one second");

    fixture.now += 1ms;
    fixture.runtime->pump();
    expect(fixture.source->pruneTimes.size() == 2, "expected prune at one second");
}

void resetClearsControllerAndSource() {
    RuntimeFixture fixture;
    const auto page = pageOfSix("reset-");
    fixture.runtime->synchronize(snapshot(page), page);

    fixture.runtime->reset();

    expect(fixture.source->clearCount == 1, "expected reset to clear texture source");
    expect(fixture.runtime->views().empty(), "expected reset to clear thumbnail views");
}

void leavesPlaceholdersWhenSourceIsUnavailable() {
    RuntimeFixture fixture;
    fixture.source->isAvailable = false;
    const auto page = pageOfSix("unavailable-");

    fixture.runtime->synchronize(snapshot(page), page);
    fixture.runtime->pump();

    expect(fixture.scheduled.empty(), "expected no scheduled work without a device");
    expect(fixture.source->findPaths.empty(), "expected no cache access without a device");
    for (const auto& view : fixture.runtime->views()) {
        expect(view.status == NativeThumbnailStatus::placeholder && !view.texture,
            "expected unavailable source placeholder");
    }
}

void passesStaleCancellationToSource() {
    RuntimeFixture fixture;
    const auto firstPage = pageOfSix("first-");
    const auto secondPage = pageOfSix("second-");
    const auto catalog = snapshot(firstPage);

    fixture.runtime->synchronize(catalog, firstPage);
    fixture.runtime->pump();
    fixture.runtime->synchronize(catalog, secondPage);
    fixture.scheduled.front()();

    expect(fixture.source->cancellationResults.size() == 1 &&
            !fixture.source->cancellationResults.front(),
        "expected source cancellation check to reject stale request");
    for (const auto& view : fixture.runtime->views()) {
        expect(view.texturePath.starts_with("second-") && !view.texture,
            "expected stale completion hidden from current page");
    }
}

void schedulerFailureCompletesRequestAndAllowsQueueProgress() {
    RuntimeFixture fixture;
    const auto page = pageOfSix("scheduler-");
    fixture.schedulerThrows = true;

    fixture.runtime->synchronize(snapshot(page), page);
    try {
        fixture.runtime->pump();
    } catch (...) {
        throw std::runtime_error("expected scheduler failure contained by runtime");
    }

    const auto failedViews = fixture.runtime->views();
    expect(failedViews.front().status == NativeThumbnailStatus::broken,
        "expected scheduler failure to mark first request broken");

    fixture.runtime->pump();
    expect(fixture.scheduled.size() == 1,
        "expected queue to advance after scheduler failure");
}

void sourceFailureInTaskCompletesRequestAndAllowsQueueProgress() {
    RuntimeFixture fixture;
    const auto page = pageOfSix("source-");
    fixture.source->throwOnLoad = true;

    fixture.runtime->synchronize(snapshot(page), page);
    fixture.runtime->pump();
    try {
        fixture.scheduled.front()();
    } catch (...) {
        throw std::runtime_error("expected source failure contained by runtime task");
    }

    const auto failedViews = fixture.runtime->views();
    expect(failedViews.front().status == NativeThumbnailStatus::broken,
        "expected source failure to mark first request broken");

    fixture.runtime->pump();
    expect(fixture.scheduled.size() == 2,
        "expected queue to advance after source failure");
}

void buildsDiskCacheOneEntryPerPump() {
    RuntimeFixture fixture;
    fixture.runtime->startDiskCacheBuild({"a.dds", "a.dds", "A.DDS",
        "b\\part.dds", "B/part.dds"});
    expect(fixture.runtime->diskCacheBuildStatus().total == 2,
        "build should deduplicate texture paths");
    fixture.runtime->pumpDiskCacheBuild();
    fixture.runtime->pumpDiskCacheBuild();
    expect(fixture.scheduled.size() == 1,
        "only one cache build task should be in flight");
    fixture.scheduled.front()();
    fixture.runtime->pumpDiskCacheBuild();
    expect(fixture.scheduled.size() == 2,
        "second entry should schedule after first completes");
    fixture.scheduled.back()();
    const auto status = fixture.runtime->diskCacheBuildStatus();
    expect(status.completed == 2 && !status.active && status.failed == 0,
        "build should report completed progress");
    expect(fixture.source->preparedPaths ==
            std::vector<std::string>{"a.dds", "b\\part.dds"},
        "build should process each unique path once");
    expect(fixture.runtime->diskCacheBytes() == 65536,
        "runtime should expose disk usage");
}

template <class Test>
int run(std::string_view name, Test&& test) {
    try {
        std::forward<Test>(test)();
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

int main() {
    int failures = 0;
    failures += run("synchronizes presentation-neutral visible paths", synchronizesPresentationNeutralVisiblePaths);
    failures += run("schedules only one request at a time", schedulesOnlyOneRequestAtATime);
    failures += run("does not schedule cache hits", doesNotScheduleCacheHits);
    failures += run("prunes at most once per second", prunesAtMostOncePerSecond);
    failures += run("reset clears controller and source", resetClearsControllerAndSource);
    failures += run("unavailable source leaves placeholders", leavesPlaceholdersWhenSourceIsUnavailable);
    failures += run("stale request is cancelled in source", passesStaleCancellationToSource);
    failures += run(
        "scheduler failure completes request and allows queue progress",
        schedulerFailureCompletesRequestAndAllowsQueueProgress);
    failures += run(
        "source failure in task completes request and allows queue progress",
        sourceFailureInTaskCompletesRequestAndAllowsQueueProgress);
    failures += run("builds disk cache one entry per pump", buildsDiskCacheOneEntryPerPump);
    return failures == 0 ? 0 : 1;
}
