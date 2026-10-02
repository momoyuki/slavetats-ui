#include "repository/FavoriteIdentity.h"
#include "runtime/FavoriteStore.h"
#include "runtime/PluginConfigFile.h"

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

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() : path_(std::filesystem::temp_directory_path() / "SlaveTatsUIFavoriteStoreTests") {
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

const stui::repository::FavoriteIdentity kRose{
    .domain = "default",
    .sourceId = "textures/actors/character/slavetats/marks.json",
    .section = "Marks",
    .name = "Rose",
};

void persistsFullIdentityAcrossStoreInstances() {
    TemporaryDirectory directory;
    auto config = std::make_shared<stui::runtime::PluginConfigFile>(
        directory.path() / "SlaveTatsUI.json");
    stui::runtime::FavoriteStore store(config);

    const auto saved = store.setFavorite(kRose, true);
    stui::runtime::FavoriteStore reopened(config);
    const auto restored = reopened.load();

    expect(saved && saved->size() == 1 && saved->front() == kRose,
        "expected first full identity favorite to save");
    expect(restored && *restored == stui::runtime::FavoriteList{kRose},
        "expected full identity favorite to survive a new store instance");
}

void missingFavoritesLoadWithoutCreatingConfiguration() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    auto config = std::make_shared<stui::runtime::PluginConfigFile>(path);
    stui::runtime::FavoriteStore store(config);

    const auto loaded = store.load();

    expect(loaded && loaded->empty(), "expected missing configuration to mean no favorites");
    expect(!std::filesystem::exists(path), "expected loading favorites not to create configuration");
}

void preservesLegacyAndSupportedUnknownFavoriteProperties() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    {
        std::ofstream output(path);
        output << R"({"hotkey":67,"favorites":{"version":1,"entries":[],"future":"keep"},"other":{"keep":true}})";
    }
    auto config = std::make_shared<stui::runtime::PluginConfigFile>(path);
    stui::runtime::FavoriteStore store(config);

    const auto saved = store.setFavorite(kRose, true);
    const auto document = config->read();

    expect(saved && saved->size() == 1, "expected favorite update to succeed for legacy config");
    expect(document && document->at("hotkey") == 67 &&
            document->at("other").at("keep") == true &&
            document->at("favorites").at("future") == "keep",
        "expected unrelated and supported unknown properties to survive favorite update");
}

void canonicalizesDuplicateFavoritesInStableOrder() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    {
        std::ofstream output(path);
        output << R"({"favorites":{"version":1,"entries":[{"domain":"default","sourceId":"z.json","section":"Marks","name":"Zulu"},{"domain":"default","sourceId":"a.json","section":"Marks","name":"Sakura"},{"domain":"default","sourceId":"z.json","section":"Marks","name":"Zulu"}]}})";
    }
    auto config = std::make_shared<stui::runtime::PluginConfigFile>(path);
    stui::runtime::FavoriteStore store(config);

    const auto loaded = store.load();
    const auto saved = store.setFavorite(kRose, true);
    const auto reread = config->read();

    expect(loaded && loaded->size() == 2, "expected load to deduplicate favorite identities");
    expect(saved && saved->size() == 3 && saved->front().sourceId == "a.json",
        "expected committed favorites to use stable tuple ordering");
    expect(reread && reread->at("favorites").at("entries").at(0).at("sourceId") == "a.json" &&
            reread->at("favorites").at("entries").at(2).at("sourceId") == "z.json",
        "expected canonicalized entries to persist in stable order");
}

void usesExactSourceAndDomainToDistinguishSameNamedTattoos() {
    TemporaryDirectory directory;
    auto config = std::make_shared<stui::runtime::PluginConfigFile>(
        directory.path() / "SlaveTatsUI.json");
    stui::runtime::FavoriteStore store(config);
    auto sameNameOtherSource = kRose;
    sameNameOtherSource.sourceId = "textures/actors/character/slavetats/other.json";
    auto sameNameOtherDomain = kRose;
    sameNameOtherDomain.domain = "custom";

    expect(store.setFavorite(kRose, true).has_value(), "expected first favorite save");
    expect(store.setFavorite(sameNameOtherSource, true).has_value(), "expected source variant save");
    const auto saved = store.setFavorite(sameNameOtherDomain, true);

    expect(saved && saved->size() == 3,
        "expected same-named tattoos from different source/domain to remain distinct");
}

void rejectsUnsupportedSchemaWithoutOverwritingIt() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    {
        std::ofstream output(path);
        output << R"({"favorites":{"version":99,"entries":[]},"hotkey":67})";
    }
    auto config = std::make_shared<stui::runtime::PluginConfigFile>(path);
    stui::runtime::FavoriteStore store(config);

    const auto loaded = store.load();
    const auto updated = store.setFavorite(kRose, true);
    std::ifstream input(path);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};

    expect(!loaded && !updated, "expected unsupported schema to reject Favorites operations");
    expect(bytes == R"({"favorites":{"version":99,"entries":[]},"hotkey":67})",
        "expected unsupported schema to remain unchanged");
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
    failures += run("persists full identity across store instances", persistsFullIdentityAcrossStoreInstances);
    failures += run("missing favorites do not create configuration", missingFavoritesLoadWithoutCreatingConfiguration);
    failures += run("preserves legacy and unknown properties", preservesLegacyAndSupportedUnknownFavoriteProperties);
    failures += run("canonicalizes duplicate favorites", canonicalizesDuplicateFavoritesInStableOrder);
    failures += run("distinguishes same named tattoos", usesExactSourceAndDomainToDistinguishSameNamedTattoos);
    failures += run("rejects unsupported schema without overwriting", rejectsUnsupportedSchemaWithoutOverwritingIt);
    return failures == 0 ? 0 : 1;
}
