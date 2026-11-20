#include "repository/TattooCatalogStore.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace fs = std::filesystem;
using stui::repository::TattooCatalogStore;

class TemporarySourceDirectory {
public:
    TemporarySourceDirectory() {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() /
            ("slavetats-ui-catalog-store-" + std::to_string(unique));
        fs::create_directories(m_path);
    }

    ~TemporarySourceDirectory() {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    void write(std::string_view filename, std::string_view json) const {
        std::ofstream stream(m_path / filename);
        stream << json;
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return m_path;
    }

private:
    fs::path m_path;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void publishesImmutableSnapshotAfterRefresh() {
    TemporarySourceDirectory directory;
    directory.write(
        "Marks.json",
        R"([{"name":"Rose","section":"Flowers","texture":"marks/rose.dds","area":"BODY"}])");
    TattooCatalogStore store;

    expect(!store.snapshot(), "expected no snapshot before first refresh");
    const auto refresh = store.refresh(directory.path());
    const auto snapshot = store.snapshot();

    expect(refresh.has_value(), "expected refresh to succeed");
    expect(snapshot != nullptr, "expected published snapshot");
    expect(snapshot->sourceCount == 1, "expected source count available to readers");
    expect(snapshot->repository.query().totalEntries == 1,
        "expected repository available to readers");
}

void failedRefreshPreservesPublishedSnapshot() {
    TemporarySourceDirectory directory;
    directory.write(
        "Marks.json",
        R"([{"name":"Rose","section":"Flowers","texture":"marks/rose.dds","area":"BODY"}])");
    TattooCatalogStore store;
    expect(store.refresh(directory.path()).has_value(), "expected initial refresh");
    const auto original = store.snapshot();

    const auto refresh = store.refresh(directory.path() / "missing");
    const auto retained = store.snapshot();

    expect(!refresh.has_value(), "expected missing source directory to fail refresh");
    expect(retained == original, "expected failed refresh to retain exact snapshot");
    expect(retained->repository.query().entries.front().name == "Rose",
        "expected retained snapshot to remain queryable");
}

void successfulRefreshPreservesExistingReaders() {
    TemporarySourceDirectory firstDirectory;
    firstDirectory.write(
        "First.json",
        R"([{"name":"First","section":"Marks","texture":"first.dds","area":"BODY"}])");
    TemporarySourceDirectory secondDirectory;
    secondDirectory.write(
        "Second.json",
        R"([{"name":"Second","section":"Marks","texture":"second.dds","area":"BODY"}])");
    TattooCatalogStore store;
    expect(store.refresh(firstDirectory.path()).has_value(), "expected first refresh");
    const auto first = store.snapshot();

    expect(store.refresh(secondDirectory.path()).has_value(), "expected second refresh");
    const auto second = store.snapshot();

    expect(first != second, "expected successful refresh to publish a new snapshot");
    expect(first->repository.query().entries.front().name == "First",
        "expected existing reader to retain the old snapshot");
    expect(second->repository.query().entries.front().name == "Second",
        "expected new readers to receive the refreshed snapshot");
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

void scheduledRefreshPublishesOnlyAfterWorkerExecutes() {
    TemporarySourceDirectory directory;
    directory.write("Marks.json", R"([{"name":"Rose","section":"Flowers","texture":"rose.dds","area":"BODY"}])");
    TattooCatalogStore store;
    std::function<void()> scheduled;
    bool completed = false;
    store.refreshAsync(directory.path(), [&](auto task) { scheduled = std::move(task); },
        [&](auto result) { completed = result.has_value(); });
    expect(!store.snapshot() && !completed && bool(scheduled), "startup must return before parsing/publishing catalog");
    scheduled();
    expect(completed && store.snapshot()->repository.query().totalEntries == 1,
        "worker must publish complete catalog atomically");
    const auto old = store.snapshot();
    store.refreshAsync(directory.path() / "missing", [&](auto task) { scheduled = std::move(task); },
        [&](auto result) { completed = result.has_value(); });
    scheduled();
    expect(!completed && store.snapshot() == old, "failed async refresh preserves last catalog");
}

int main() {
    int failures = 0;
    failures += run("startup schedules catalog refresh", scheduledRefreshPublishesOnlyAfterWorkerExecutes);
    failures += run(
        "publishes immutable snapshot after refresh",
        publishesImmutableSnapshotAfterRefresh);
    failures += run(
        "failed refresh preserves published snapshot",
        failedRefreshPreservesPublishedSnapshot);
    failures += run(
        "successful refresh preserves existing readers",
        successfulRefreshPreservesExistingReaders);
    return failures == 0 ? 0 : 1;
}
