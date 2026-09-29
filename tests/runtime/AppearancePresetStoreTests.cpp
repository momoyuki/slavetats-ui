#include "repository/FavoriteIdentity.h"
#include "repository/RecentTattooIdentity.h"
#include "runtime/AppearancePresetStore.h"
#include "runtime/FavoriteStore.h"
#include "runtime/HotkeyBinding.h"
#include "runtime/PluginConfigFile.h"
#include "runtime/RecentTattooStore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

using stui::runtime::AppearancePreset;
using stui::runtime::AppearancePresetList;
using stui::runtime::AppearancePresetStore;
using stui::runtime::PluginConfigFile;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() : path_(std::filesystem::temp_directory_path() /
            "SlaveTatsUIAppearancePresetStoreTests") {
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

AppearancePreset preset(std::string name, float alpha = 1.0F) {
    return AppearancePreset{
        .name = std::move(name),
        .color = 0xFFFFFF,
        .alpha = alpha,
        .glow = 0x102030,
        .emissiveMult = 2.0F,
        .glossiness = 250.0F,
        .specularStrength = 10.0F,
    };
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeDocument(const std::filesystem::path& path, const nlohmann::json& document) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << document.dump();
}

void missingPresetsLoadEmptyWithoutCreatingConfiguration() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    AppearancePresetStore store(std::make_shared<PluginConfigFile>(path));

    const auto loaded = store.load();

    expect(loaded && loaded->empty(), "expected missing presets to load empty");
    expect(!std::filesystem::exists(path), "expected load not to create configuration");
}

void roundTripsExactValuesAndUnicodeName() {
    TemporaryDirectory directory;
    auto config = std::make_shared<PluginConfigFile>(directory.path() / "SlaveTatsUI.json");
    AppearancePresetStore store(config);
    AppearancePreset expected{
        .name = "  Warm Glow â  ",
        .color = 0x123456,
        .alpha = 0.25F,
        .glow = 0xABCDEF,
        .emissiveMult = 10.0F,
        .glossiness = 1000.0F,
        .specularStrength = 100.0F,
    };

    const auto created = store.create(expected);
    AppearancePresetStore reopened(config);
    const auto loaded = reopened.load();
    expected.name = "Warm Glow â";

    expect(created && *created == AppearancePresetList{expected},
        "expected create to trim and preserve exact values");
    expect(loaded && *loaded == AppearancePresetList{expected},
        "expected exact values and Unicode name to survive reopen");
}

void mutationsPreserveStoredOrderAndSpelling() {
    TemporaryDirectory directory;
    auto config = std::make_shared<PluginConfigFile>(directory.path() / "SlaveTatsUI.json");
    AppearancePresetStore store(config);

    expect(store.create(preset("First"))->at(0).name == "First", "create appends first");
    expect(store.create(preset("Second"))->at(1).name == "Second", "create appends second");
    expect(store.create(preset("Third"))->at(2).name == "Third", "create appends third");
    expect(store.overwrite(preset("first", 0.5F))->at(0).alpha == 0.5F,
        "folded overwrite retains index");
    expect(store.rename("FIRST", "Renamed")->at(0).name == "Renamed",
        "rename retains index and requested spelling");
    const auto erased = store.erase("second");
    AppearancePresetStore reopened(config);
    const auto loaded = reopened.load();

    const AppearancePresetList expected{preset("Renamed", 0.5F), preset("Third")};
    expect(erased && *erased == expected, "delete preserves remaining order");
    expect(loaded && *loaded == expected, "mutation order persists across restart");
}

void rejectsBlankDuplicateMissingAndOverLimitMutationsWithoutWriting() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    auto config = std::make_shared<PluginConfigFile>(path);
    AppearancePresetStore store(config);

    expect(!store.create(preset(" \t\r\n ")), "expected whitespace-only name rejected");
    expect(!std::filesystem::exists(path), "expected invalid first create not to write");
    expect(store.create(preset("Warm Glow")).has_value(), "expected valid create");
    expect(!store.create(preset(" warm glow ")), "expected folded duplicate rejected");
    expect(!store.overwrite(preset("Missing")), "expected missing overwrite rejected");
    expect(!store.rename("Missing", "Other"), "expected missing rename rejected");
    expect(!store.erase("Missing"), "expected missing delete rejected");
    expect(store.create(preset("Other")).has_value(), "expected second preset");
    expect(!store.rename("Other", "WARM GLOW"), "expected rename collision rejected");

    for (int index = 2; index < 20; ++index) {
        expect(store.create(preset("Preset " + std::to_string(index))).has_value(),
            "expected create through limit");
    }
    expect(!store.create(preset("Twenty First")), "expected twenty-first name rejected");
    expect(store.overwrite(preset("warm glow", 0.75F)).has_value(),
        "expected overwrite allowed at limit");
    expect(store.rename("OTHER", "Replacement").has_value(),
        "expected rename allowed at limit");
    expect(store.erase("replacement").has_value(), "expected delete allowed at limit");
}

void rejectsMalformedUnsupportedAndDuplicateSchemasWithoutOverwrite() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    const std::vector<nlohmann::json> invalidPayloads{
        42,
        {{"version", 99}, {"entries", nlohmann::json::array()}},
        {{"version", 1}},
        {{"version", 1}, {"entries", 42}},
        {{"version", 1}, {"entries", nlohmann::json::array({42})}},
        {{"version", 1}, {"entries", nlohmann::json::array({{{"name", "Only name"}}})}},
        {{"version", 1}, {"entries", nlohmann::json::array({
            {{"name", "Overflow"}, {"color", UINT64_C(0x100000000)},
             {"alpha", 1.0}, {"glow", 0}, {"emissiveMult", 1.0},
             {"glossiness", 0.0}, {"specularStrength", 0.0}},
        })}},
        {{"version", 1}, {"entries", nlohmann::json::array({
            {{"name", "First"}, {"color", 0}, {"alpha", 1.0}, {"glow", 0},
             {"emissiveMult", 1.0}, {"glossiness", 0.0}, {"specularStrength", 0.0}},
            {{"name", " first "}, {"color", 0}, {"alpha", 1.0}, {"glow", 0},
             {"emissiveMult", 1.0}, {"glossiness", 0.0}, {"specularStrength", 0.0}},
        })}},
    };

    for (const auto& payload : invalidPayloads) {
        const nlohmann::json document{{"appearancePresets", payload}, {"keep", true}};
        writeDocument(path, document);
        const auto before = readBytes(path);
        AppearancePresetStore store(std::make_shared<PluginConfigFile>(path));

        expect(!store.load(), "expected invalid schema load rejected");
        expect(!store.create(preset("New")), "expected invalid schema mutation rejected");
        expect(readBytes(path) == before, "expected invalid schema bytes preserved");
    }
}

void rejectsInvalidNumbersWithoutPartialPersistence() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    AppearancePresetStore store(std::make_shared<PluginConfigFile>(path));
    const std::vector<AppearancePreset> invalid{
        {.name = "Color", .color = 0x1000000, .alpha = 1.0F},
        {.name = "Alpha Low", .alpha = -0.01F},
        {.name = "Alpha High", .alpha = 1.01F},
        {.name = "Alpha NaN", .alpha = std::numeric_limits<float>::quiet_NaN()},
        {.name = "Emission", .alpha = 1.0F, .emissiveMult = 10.01F},
        {.name = "Gloss", .alpha = 1.0F, .glossiness = 1000.01F},
        {.name = "Specular", .alpha = 1.0F, .specularStrength = 100.01F},
        {.name = "Infinity", .alpha = 1.0F,
         .emissiveMult = std::numeric_limits<float>::infinity()},
    };

    for (const auto& candidate : invalid) {
        expect(!store.create(candidate), "expected invalid numeric value rejected");
        expect(!std::filesystem::exists(path), "expected invalid value not to create file");
    }
}

void rejectsMoreThanTwentyPersistedEntries() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    nlohmann::json entries = nlohmann::json::array();
    for (int index = 0; index < 21; ++index) {
        entries.push_back({
            {"name", "Preset " + std::to_string(index)}, {"color", 0}, {"alpha", 1.0},
            {"glow", 0}, {"emissiveMult", 1.0}, {"glossiness", 0.0},
            {"specularStrength", 0.0},
        });
    }
    writeDocument(path, {{"appearancePresets", {{"version", 1}, {"entries", entries}}}});
    const auto before = readBytes(path);
    AppearancePresetStore store(std::make_shared<PluginConfigFile>(path));

    expect(!store.load(), "expected over-limit persisted entries rejected");
    expect(!store.erase("Preset 0"), "expected mutation not to repair invalid payload");
    expect(readBytes(path) == before, "expected over-limit payload preserved");
}

void preservesUnknownMembersAndManualJsonOrder() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    writeDocument(path, {
        {"hotkey", 67},
        {"favorites", {{"version", 1}, {"entries", nlohmann::json::array()}}},
        {"recentlyUsed", {{"version", 1}, {"entries", nlohmann::json::array()}}},
        {"appearancePresets", {
            {"version", 1},
            {"entries", nlohmann::json::array({
                {{"name", "Third"}, {"color", 0}, {"alpha", 1.0}, {"glow", 0},
                 {"emissiveMult", 1.0}, {"glossiness", 0.0}, {"specularStrength", 0.0}},
                {{"name", "First"}, {"color", 1}, {"alpha", 0.5}, {"glow", 2},
                 {"emissiveMult", 2.0}, {"glossiness", 3.0}, {"specularStrength", 4.0}},
            })},
            {"future", "keep"},
        }},
        {"other", {{"keep", true}}},
    });
    auto config = std::make_shared<PluginConfigFile>(path);
    AppearancePresetStore store(config);

    const auto loaded = store.load();
    const auto overwritten = store.overwrite(preset("first", 0.75F));
    const auto document = config->read();

    expect(loaded && loaded->at(0).name == "Third" && loaded->at(1).name == "First",
        "expected manually edited JSON order loaded exactly");
    expect(overwritten && overwritten->at(0).name == "Third" &&
            overwritten->at(1).name == "first",
        "expected overwrite to retain manual index");
    expect(document && document->at("hotkey") == 67 &&
            document->at("appearancePresets").at("future") == "keep" &&
            document->at("other").at("keep") == true,
        "expected unknown root and supported preset members preserved");
}

void interleavedSharedWritersPreserveEveryFeature() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    writeDocument(path, {{"unknown", {{"keep", true}}}});
    auto config = std::make_shared<PluginConfigFile>(path);
    stui::runtime::HotkeyBinding hotkey(config);
    stui::runtime::FavoriteStore favorites(config);
    stui::runtime::RecentTattooStore recent(config);
    AppearancePresetStore presets(config);
    const stui::repository::FavoriteIdentity favorite{
        .domain = "default", .sourceId = "marks.json", .section = "Marks", .name = "Rose"};
    const stui::repository::RecentTattooIdentity history{
        .domain = "default", .sourceId = "marks.json", .section = "Marks",
        .name = "Rose", .area = stui::core::TattooArea::body};

    expect(hotkey.select(67), "expected hotkey write");
    expect(favorites.setFavorite(favorite, true).has_value(), "expected favorite write");
    expect(recent.record(history).has_value(), "expected recent write");
    expect(presets.create(preset("Shared")).has_value(), "expected preset write");

    stui::runtime::HotkeyBinding reopenedHotkey(config);
    expect(reopenedHotkey.load() && reopenedHotkey.key() == 67,
        "expected hotkey preserved after preset write");
    expect(favorites.load() && favorites.load()->at(0) == favorite,
        "expected favorite preserved after preset write");
    expect(recent.load() && recent.load()->at(0) == history,
        "expected recent history preserved after preset write");
    const auto document = config->read();
    expect(document && document->at("unknown").at("keep") == true,
        "expected unknown root member preserved after interleaved writes");
}

void nonPresetWritersPreserveSupportedAndUnsupportedPresetPayloads() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    auto config = std::make_shared<PluginConfigFile>(path);
    AppearancePresetStore presets(config);
    stui::runtime::HotkeyBinding hotkey(config);
    stui::runtime::FavoriteStore favorites(config);
    stui::runtime::RecentTattooStore recent(config);
    const stui::repository::FavoriteIdentity favorite{
        .domain = "default", .sourceId = "marks.json", .section = "Marks", .name = "Rose"};
    const stui::repository::RecentTattooIdentity history{
        .domain = "default", .sourceId = "marks.json", .section = "Marks",
        .name = "Rose", .area = stui::core::TattooArea::face};

    expect(presets.create(preset("First")).has_value(), "expected preset-first write");
    expect(hotkey.select(68), "expected hotkey after preset");
    expect(favorites.setFavorite(favorite, true).has_value(), "expected favorite after preset");
    expect(recent.record(history).has_value(), "expected recent after preset");
    expect(presets.load() && presets.load()->at(0).name == "First",
        "expected supported preset payload preserved by other writers");

    nlohmann::json unsupported{
        {"version", 99}, {"entries", nlohmann::json::array()}, {"future", "keep"}};
    auto document = *config->read();
    document["appearancePresets"] = unsupported;
    writeDocument(path, document);
    expect(hotkey.select(69), "expected hotkey to preserve unsupported preset payload");
    expect(favorites.setFavorite(favorite, false).has_value(),
        "expected favorite to preserve unsupported preset payload");
    expect(recent.record(history).has_value(),
        "expected recent to preserve unsupported preset payload");
    const auto after = config->read();
    expect(after && after->at("appearancePresets") == unsupported,
        "expected unsupported preset payload unchanged by non-preset writers");
}

void unavailableStoreRejectsEveryOperation() {
    AppearancePresetStore store(nullptr);

    expect(!store.load(), "expected unavailable load rejected");
    expect(!store.create(preset("Create")), "expected unavailable create rejected");
    expect(!store.overwrite(preset("Overwrite")), "expected unavailable overwrite rejected");
    expect(!store.rename("Old", "New"), "expected unavailable rename rejected");
    expect(!store.erase("Delete"), "expected unavailable delete rejected");
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
    failures += run("missing presets do not create configuration",
        missingPresetsLoadEmptyWithoutCreatingConfiguration);
    failures += run("round trips exact values and Unicode name",
        roundTripsExactValuesAndUnicodeName);
    failures += run("mutations preserve stored order", mutationsPreserveStoredOrderAndSpelling);
    failures += run("rejects invalid mutations without writing",
        rejectsBlankDuplicateMissingAndOverLimitMutationsWithoutWriting);
    failures += run("rejects invalid schema without overwrite",
        rejectsMalformedUnsupportedAndDuplicateSchemasWithoutOverwrite);
    failures += run("rejects invalid numbers without persistence",
        rejectsInvalidNumbersWithoutPartialPersistence);
    failures += run("rejects over-limit persisted entries", rejectsMoreThanTwentyPersistedEntries);
    failures += run("preserves unknown members and manual order",
        preservesUnknownMembersAndManualJsonOrder);
    failures += run("interleaved shared writers preserve every feature",
        interleavedSharedWritersPreserveEveryFeature);
    failures += run("other writers preserve preset payloads",
        nonPresetWritersPreserveSupportedAndUnsupportedPresetPayloads);
    failures += run("unavailable store rejects operations", unavailableStoreRejectsEveryOperation);
    return failures == 0 ? 0 : 1;
}
