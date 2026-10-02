#include "repository/RecentTattooIdentity.h"
#include "runtime/PluginConfigFile.h"
#include "runtime/RecentTattooStore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

using stui::core::TattooArea;
using stui::repository::RecentTattooIdentity;
using stui::runtime::PluginConfigFile;
using stui::runtime::RecentTattooList;
using stui::runtime::RecentTattooStore;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() : path_(std::filesystem::temp_directory_path() /
            "SlaveTatsUIRecentTattooStoreTests") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

RecentTattooIdentity recent(
    std::string name,
    TattooArea area = TattooArea::body,
    std::string sourceId = "textures/actors/character/slavetats/marks.json",
    std::string domain = "default") {
    return RecentTattooIdentity{
        .domain = std::move(domain),
        .sourceId = std::move(sourceId),
        .section = "Marks",
        .name = std::move(name),
        .area = area,
    };
}

std::size_t countArea(const RecentTattooList& entries, TattooArea area) {
    return static_cast<std::size_t>(std::ranges::count_if(
        entries, [area](const auto& entry) { return entry.area == area; }));
}

void missingHistoryLoadsWithoutCreatingConfiguration() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    auto config = std::make_shared<PluginConfigFile>(path);
    RecentTattooStore store(config);

    const auto loaded = store.load();

    expect(loaded && loaded->empty(), "expected missing history to load empty");
    expect(!std::filesystem::exists(path), "expected load not to create configuration");
}

void persistsNewestFirstAndPromotesDuplicate() {
    TemporaryDirectory directory;
    auto config = std::make_shared<PluginConfigFile>(directory.path() / "SlaveTatsUI.json");
    RecentTattooStore store(config);
    const auto rose = recent("Rose");
    const auto ivy = recent("Ivy");

    expect(store.record(rose).has_value(), "expected first record");
    expect(store.record(ivy).has_value(), "expected second record");
    const auto promoted = store.record(rose);
    RecentTattooStore reopened(config);
    const auto loaded = reopened.load();

    const RecentTattooList expected{rose, ivy};
    expect(promoted && *promoted == expected,
        "expected repeated identity promoted without duplication");
    expect(loaded && *loaded == expected, "expected newest-first order to persist");
}

void evictsOnlyBeyondSixInRecordedArea() {
    TemporaryDirectory directory;
    auto config = std::make_shared<PluginConfigFile>(directory.path() / "SlaveTatsUI.json");
    RecentTattooStore store(config);
    expect(store.record(recent("Face Old", TattooArea::face)).has_value(),
        "expected independent Face history");
    for (int index = 0; index < 11; ++index) {
        expect(store.record(recent("Body " + std::to_string(index))).has_value(),
            "expected Body history record");
    }

    const auto loaded = store.load();

    expect(loaded && countArea(*loaded, TattooArea::body) == 6 &&
            countArea(*loaded, TattooArea::face) == 1,
        "expected six entries per area without cross-area eviction");
    expect(loaded->front().name == "Body 10" &&
            std::ranges::none_of(*loaded, [](const auto& entry) {
                return entry.name == "Body 0";
            }),
        "expected newest Body retained and oldest Body evicted");
}

void distinguishesSourceDomainAndArea() {
    TemporaryDirectory directory;
    auto config = std::make_shared<PluginConfigFile>(directory.path() / "SlaveTatsUI.json");
    RecentTattooStore store(config);

    expect(store.record(recent("Rose")).has_value(), "expected base identity");
    expect(store.record(recent("Rose", TattooArea::body, "other.json")).has_value(),
        "expected source distinction");
    expect(store.record(recent("Rose", TattooArea::body,
        "textures/actors/character/slavetats/marks.json", "custom")).has_value(),
        "expected domain distinction");
    const auto saved = store.record(recent("Rose", TattooArea::face));

    expect(saved && saved->size() == 4,
        "expected source domain and area to be exact identity fields");
}

void preservesOtherConfigurationAndUnknownHistoryMembers() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    {
        std::ofstream output(path);
        output << R"({"hotkey":67,"favorites":{"version":1,"entries":[]},"recentlyUsed":{"version":1,"entries":[],"future":"keep"},"other":{"keep":true}})";
    }
    auto config = std::make_shared<PluginConfigFile>(path);
    RecentTattooStore store(config);

    const auto saved = store.record(recent("Rose"));
    const auto document = config->read();

    expect(saved && document && document->at("hotkey") == 67 &&
            document->at("favorites").at("version") == 1 &&
            document->at("recentlyUsed").at("future") == "keep" &&
            document->at("other").at("keep") == true,
        "expected shared configuration members preserved");
}

void loadsLegacyTenAndCanonicalizesOnNextRecord() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    nlohmann::json entries = nlohmann::json::array();
    for (int index = 0; index < 10; ++index) {
        entries.push_back({
            {"domain", "default"},
            {"sourceId", "marks.json"},
            {"section", "Marks"},
            {"name", "Body " + std::to_string(index)},
            {"area", "Body"},
        });
    }
    {
        std::ofstream output(path);
        output << nlohmann::json{
            {"recentlyUsed", {{"version", 1}, {"entries", entries}}},
        }.dump();
    }
    auto config = std::make_shared<PluginConfigFile>(path);
    RecentTattooStore store(config);

    const auto loaded = store.load();
    const auto recorded = store.record(recent("New", TattooArea::body, "marks.json"));
    RecentTattooStore reopened(config);
    const auto persisted = reopened.load();

    expect(loaded && loaded->size() == 6 && loaded->front().name == "Body 0" &&
            loaded->back().name == "Body 5",
        "expected legacy history to expose only the six newest entries");
    expect(recorded && persisted && *recorded == *persisted && persisted->size() == 6 &&
            persisted->front().name == "New" && persisted->back().name == "Body 4",
        "expected next record to persist the canonical six-entry history");
}

void rejectsInvalidOrOverflowingSchemaWithoutOverwrite() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    nlohmann::json document{
        {"hotkey", 67},
        {"recentlyUsed", {{"version", 1}, {"entries", nlohmann::json::array()}}},
    };
    for (int index = 0; index < 11; ++index) {
        document["recentlyUsed"]["entries"].push_back({
            {"domain", "default"},
            {"sourceId", "marks.json"},
            {"section", "Marks"},
            {"name", "Body " + std::to_string(index)},
            {"area", "Body"},
        });
    }
    {
        std::ofstream output(path);
        output << document.dump();
    }
    std::ifstream beforeInput(path);
    const std::string before{std::istreambuf_iterator<char>(beforeInput), {}};
    auto config = std::make_shared<PluginConfigFile>(path);
    RecentTattooStore store(config);

    const auto loaded = store.load();
    const auto updated = store.record(recent("New"));
    std::ifstream afterInput(path);
    const std::string after{std::istreambuf_iterator<char>(afterInput), {}};

    expect(!loaded && !updated, "expected overflowing area history rejected");
    expect(before == after, "expected rejected history schema not overwritten");
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
    failures += run("missing history does not create configuration",
        missingHistoryLoadsWithoutCreatingConfiguration);
    failures += run("persists newest first and promotes duplicate",
        persistsNewestFirstAndPromotesDuplicate);
    failures += run("evicts only beyond six in recorded area",
        evictsOnlyBeyondSixInRecordedArea);
    failures += run("distinguishes source domain and area",
        distinguishesSourceDomainAndArea);
    failures += run("preserves shared configuration members",
        preservesOtherConfigurationAndUnknownHistoryMembers);
    failures += run("loads legacy ten and canonicalizes on next record",
        loadsLegacyTenAndCanonicalizesOnNextRecord);
    failures += run("rejects overflowing schema without overwrite",
        rejectsInvalidOrOverflowingSchemaWithoutOverwrite);
    return failures == 0 ? 0 : 1;
}
