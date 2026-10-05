#include "repository/FavoriteIdentity.h"
#include "repository/RecentTattooIdentity.h"
#include "runtime/AppearancePresetStore.h"
#include "runtime/FavoriteStore.h"
#include "runtime/HotkeyBinding.h"
#include "runtime/PluginConfigFile.h"
#include "runtime/RecentTattooStore.h"
#include "runtime/TattooLoadoutStore.h"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

using stui::core::TattooArea;
using stui::core::TattooLoadout;
using stui::core::TattooLoadoutEntry;
using stui::core::TattooLoadoutList;
using stui::runtime::PluginConfigFile;
using stui::runtime::TattooLoadoutStore;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() : path_(std::filesystem::temp_directory_path() /
            "SlaveTatsUITattooLoadoutStoreTests") {
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

TattooLoadoutEntry entry(
    const TattooArea area,
    const std::int32_t slot,
    std::string name) {
    return TattooLoadoutEntry{
        .area = area,
        .slot = slot,
        .domain = "default",
        .section = "Marks",
        .name = std::move(name),
        .color = 0x123456,
        .alpha = 0.25F,
        .glow = 0xABCDEF,
        .emissiveMult = 10.0F,
        .glossiness = 1000.0F,
        .specularStrength = 100.0F,
    };
}

TattooLoadout loadout(std::string name, std::vector<TattooLoadoutEntry> entries = {}) {
    return TattooLoadout{.name = std::move(name), .entries = std::move(entries)};
}

void writeDocument(const std::filesystem::path& path, const nlohmann::json& document) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << document.dump();
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void missingLoadoutsLoadEmptyWithoutCreatingFile() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    TattooLoadoutStore store(std::make_shared<PluginConfigFile>(path));

    const auto loaded = store.load();

    expect(loaded && loaded->empty(), "missing loadouts should load as empty");
    expect(!std::filesystem::exists(path), "load must not create configuration");
}

void roundTripsFourAreasAndSortsTattooEntries() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    auto config = std::make_shared<PluginConfigFile>(path);
    TattooLoadoutStore store(config);
    TattooLoadout expected = loadout("  Complete Set  ", {
        entry(TattooArea::feet, 2, "Feet"),
        entry(TattooArea::body, 5, "Body Five"),
        entry(TattooArea::hands, 1, "Hands"),
        entry(TattooArea::face, 3, "Face"),
        entry(TattooArea::body, 1, "Body One"),
    });

    const auto created = store.create(expected);
    const auto loaded = store.load();
    expected.name = "Complete Set";
    expected.entries = {
        entry(TattooArea::body, 1, "Body One"),
        entry(TattooArea::body, 5, "Body Five"),
        entry(TattooArea::face, 3, "Face"),
        entry(TattooArea::hands, 1, "Hands"),
        entry(TattooArea::feet, 2, "Feet"),
    };

    expect(created && created->size() == 1, "valid loadout should be created");
    expect(loaded && *loaded == TattooLoadoutList{expected},
        "load should preserve exact values in canonical entry order");
    const auto document = config->read();
    const auto& tattoos = document->at("tattooLoadouts").at("entries").at(0).at("tattoos");
    expect(tattoos.at(0).at("area") == "Body" && tattoos.at(0).at("slot") == 1,
        "serialization should sort Body by ascending slot");
    expect(tattoos.at(1).at("area") == "Body" && tattoos.at(1).at("slot") == 5,
        "serialization should retain later Body slot");
    expect(tattoos.at(2).at("area") == "Face" &&
            tattoos.at(3).at("area") == "Hands" && tattoos.at(4).at("area") == "Feet",
        "serialization should use deterministic area order");
}

void supportsEmptyLoadoutAndOrderedMutations() {
    TemporaryDirectory directory;
    auto config = std::make_shared<PluginConfigFile>(directory.path() / "SlaveTatsUI.json");
    TattooLoadoutStore store(config);

    expect(store.create(loadout("First")).has_value(), "empty loadout should be valid");
    expect(store.create(loadout("Second", {entry(TattooArea::body, 0, "Second")})).has_value(),
        "second loadout should append");
    expect(store.create(loadout("Third")).has_value(), "third loadout should append");
    expect(store.overwrite(loadout(" first ", {entry(TattooArea::face, 1, "Changed")}))->at(0)
            .entries.at(0).name == "Changed",
        "folded overwrite should retain index");
    expect(store.rename("FIRST", "Renamed")->at(0).name == "Renamed",
        "rename should retain index and spelling");
    const auto erased = store.erase("second");

    expect(erased && erased->size() == 2 && erased->at(0).name == "Renamed" &&
            erased->at(1).name == "Third",
        "delete should preserve remaining order");
}

void rejectsInvalidMutationsWithoutWriting() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    TattooLoadoutStore store(std::make_shared<PluginConfigFile>(path));

    expect(!store.create(loadout(" \t\r\n ")), "blank name should be rejected");
    expect(!store.create(loadout("Bad Slot", {entry(TattooArea::body, -1, "Bad")})),
        "negative slot should be rejected");
    auto duplicate = loadout("Duplicate", {
        entry(TattooArea::body, 1, "First"), entry(TattooArea::body, 1, "Second")});
    expect(!store.create(duplicate), "duplicate area and slot should be rejected");
    auto invalidNumber = loadout("Invalid Number", {entry(TattooArea::face, 0, "Bad")});
    invalidNumber.entries[0].alpha = std::numeric_limits<float>::quiet_NaN();
    expect(!store.create(invalidNumber), "non-finite appearance should be rejected");
    expect(!std::filesystem::exists(path), "invalid first mutations must not write");

    expect(store.create(loadout("Valid")).has_value(), "valid loadout should write");
    expect(!store.create(loadout(" valid ")), "case-folded duplicate should be rejected");
    expect(!store.overwrite(loadout("Missing")), "missing overwrite should be rejected");
    expect(!store.rename("Missing", "Other"), "missing rename should be rejected");
    expect(!store.erase("Missing"), "missing delete should be rejected");
}

void enforcesTwentyEntryLimitButAllowsOtherMutations() {
    TemporaryDirectory directory;
    TattooLoadoutStore store(std::make_shared<PluginConfigFile>(
        directory.path() / "SlaveTatsUI.json"));

    for (int index = 0; index < 20; ++index) {
        expect(store.create(loadout("Loadout " + std::to_string(index))).has_value(),
            "create should succeed through limit");
    }
    expect(!store.create(loadout("Twenty First")), "twenty-first loadout should be rejected");
    expect(store.overwrite(loadout("loadout 0", {entry(TattooArea::body, 0, "Changed")}))
            .has_value(),
        "overwrite should remain available at limit");
    expect(store.rename("LOADOUT 1", "Replacement").has_value(),
        "rename should remain available at limit");
    expect(store.erase("replacement").has_value(), "delete should remain available at limit");
}

void rejectsMalformedSchemaWithoutOverwritingBytes() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    const std::vector<nlohmann::json> invalid{
        42,
        {{"version", 99}, {"entries", nlohmann::json::array()}},
        {{"version", 1}},
        {{"version", 1}, {"entries", 42}},
        {{"version", 1}, {"entries", nlohmann::json::array({42})}},
        {{"version", 1}, {"entries", nlohmann::json::array({
            {{"name", "Missing tattoos"}}
        })}},
    };

    for (const auto& payload : invalid) {
        writeDocument(path, {{"tattooLoadouts", payload}, {"keep", true}});
        const auto before = readBytes(path);
        TattooLoadoutStore store(std::make_shared<PluginConfigFile>(path));
        expect(!store.load(), "invalid schema load should fail");
        expect(!store.create(loadout("New")), "invalid schema mutation should fail");
        expect(readBytes(path) == before, "invalid schema bytes must remain unchanged");
    }
}

void rejectsPersistedPayloadAboveLimitWithoutRepairingIt() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    auto entries = nlohmann::json::array();
    for (int index = 0; index < 21; ++index) {
        entries.push_back({
            {"name", "Loadout " + std::to_string(index)},
            {"tattoos", nlohmann::json::array()},
        });
    }
    writeDocument(path, {{"tattooLoadouts", {{"version", 1}, {"entries", entries}}}});
    const auto before = readBytes(path);
    TattooLoadoutStore store(std::make_shared<PluginConfigFile>(path));

    expect(!store.load(), "persisted twenty-first loadout should be rejected");
    expect(!store.erase("Loadout 0"), "mutation must not repair over-limit data");
    expect(readBytes(path) == before, "over-limit payload must remain byte-identical");
}

void preservesUnknownMembersAndManualOuterOrder() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    auto empty = [](std::string name) {
        return nlohmann::json{{"name", std::move(name)}, {"tattoos", nlohmann::json::array()}};
    };
    writeDocument(path, {
        {"hotkey", 67},
        {"tattooLoadouts", {
            {"version", 1},
            {"entries", nlohmann::json::array({empty("Third"), empty("First")})},
            {"future", "keep"},
        }},
        {"other", {{"keep", true}}},
    });
    auto config = std::make_shared<PluginConfigFile>(path);
    TattooLoadoutStore store(config);

    const auto loaded = store.load();
    const auto overwritten = store.overwrite(loadout("first", {
        entry(TattooArea::body, 0, "Changed")}));
    const auto document = config->read();

    expect(loaded && loaded->at(0).name == "Third" && loaded->at(1).name == "First",
        "manual outer order should load exactly");
    expect(overwritten && overwritten->at(0).name == "Third" &&
            overwritten->at(1).name == "first",
        "overwrite should retain manual index");
    expect(document && document->at("hotkey") == 67 &&
            document->at("tattooLoadouts").at("future") == "keep" &&
            document->at("other").at("keep") == true,
        "unknown root and supported-object members should survive");
}

void interleavedFeatureWritersPreserveLoadoutsAndSiblingData() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    writeDocument(path, {{"unknown", {{"keep", true}}}});
    auto config = std::make_shared<PluginConfigFile>(path);
    TattooLoadoutStore loadouts(config);
    stui::runtime::HotkeyBinding hotkey(config);
    stui::runtime::FavoriteStore favorites(config);
    stui::runtime::RecentTattooStore recent(config);
    stui::runtime::AppearancePresetStore presets(config);
    const stui::repository::FavoriteIdentity favorite{
        .domain = "default", .sourceId = "marks.json", .section = "Marks", .name = "Rose"};
    const stui::repository::RecentTattooIdentity history{
        .domain = "default", .sourceId = "marks.json", .section = "Marks",
        .name = "Rose", .area = TattooArea::body};
    const stui::runtime::AppearancePreset preset{
        .name = "Shared", .color = 0xFFFFFF, .alpha = 1.0F, .glow = 0,
        .emissiveMult = 1.0F, .glossiness = 0.0F, .specularStrength = 0.0F};

    expect(loadouts.create(loadout("Outfit")).has_value(), "loadout write should succeed");
    expect(hotkey.select(67), "hotkey write should succeed");
    expect(favorites.setFavorite(favorite, true).has_value(), "favorite write should succeed");
    expect(recent.record(history).has_value(), "recent write should succeed");
    expect(presets.create(preset).has_value(), "preset write should succeed");
    expect(loadouts.overwrite(loadout("outfit", {entry(TattooArea::face, 1, "Rose")}))
            .has_value(),
        "loadout overwrite should succeed after sibling writes");

    const auto document = config->read();
    expect(loadouts.load() && loadouts.load()->at(0).entries.at(0).name == "Rose",
        "sibling writers should preserve loadout payload");
    expect(document && document->at("hotkey") == 67 &&
            document->at("favorites").at("entries").size() == 1 &&
            document->at("recentlyUsed").at("entries").size() == 1 &&
            document->at("appearancePresets").at("entries").size() == 1 &&
            document->at("unknown").at("keep") == true,
        "loadout mutation should preserve every sibling feature and unknown root member");
}

void unavailableStoreRejectsEveryOperation() {
    TattooLoadoutStore store(nullptr);
    expect(!store.load(), "unavailable load should fail");
    expect(!store.create(loadout("Create")), "unavailable create should fail");
    expect(!store.overwrite(loadout("Overwrite")), "unavailable overwrite should fail");
    expect(!store.rename("Old", "New"), "unavailable rename should fail");
    expect(!store.erase("Delete"), "unavailable delete should fail");
}

template <class Test>
int run(const std::string_view name, Test&& test) {
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
    failures += run("missing loadouts do not create configuration",
        missingLoadoutsLoadEmptyWithoutCreatingFile);
    failures += run("round trips four areas and deterministic serialization",
        roundTripsFourAreasAndSortsTattooEntries);
    failures += run("supports empty loadout and ordered mutations",
        supportsEmptyLoadoutAndOrderedMutations);
    failures += run("rejects invalid mutations without writing",
        rejectsInvalidMutationsWithoutWriting);
    failures += run("enforces twenty entry limit", enforcesTwentyEntryLimitButAllowsOtherMutations);
    failures += run("rejects malformed schema without overwrite",
        rejectsMalformedSchemaWithoutOverwritingBytes);
    failures += run("rejects persisted data above limit",
        rejectsPersistedPayloadAboveLimitWithoutRepairingIt);
    failures += run("preserves unknown members and manual order",
        preservesUnknownMembersAndManualOuterOrder);
    failures += run("interleaved writers preserve all features",
        interleavedFeatureWritersPreserveLoadoutsAndSiblingData);
    failures += run("unavailable store rejects operations",
        unavailableStoreRejectsEveryOperation);
    return failures == 0 ? 0 : 1;
}
