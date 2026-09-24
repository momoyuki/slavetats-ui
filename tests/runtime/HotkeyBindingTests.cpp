#include "runtime/HotkeyBinding.h"
#include "runtime/PluginConfigFile.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ranges>
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
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() / "SlaveTatsUIHotkeyBindingTests") {
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

void missingConfigurationDefaultsToNoHotkey() {
    TemporaryDirectory directory;
    const auto configPath = directory.path() / "SlaveTatsUI.json";
    stui::runtime::HotkeyBinding binding(configPath);

    expect(binding.load(), "expected missing configuration to be created");
    expect(!binding.key(), "expected no default hotkey");
    expect(binding.label() == "None", "expected None label");

    std::ifstream input(configPath);
    const auto json = nlohmann::json::parse(input);
    expect(json.at("hotkey").is_null(), "expected persisted null hotkey");
}

void dropdownOptionsStartWithNoneAndExposeKeyboardKeys() {
    const auto options = stui::runtime::hotkeyOptions();

    expect(!options.empty(), "expected hotkey dropdown options");
    expect(options.front().label == "None", "expected None to be the first option");
    expect(!options.front().key, "expected None to disable the hotkey");

    const auto f8 = std::ranges::find(options, std::string_view("F8"), &stui::runtime::HotkeyOption::label);
    expect(f8 != options.end() && f8->key == 0x42, "expected F8 DIK mapping");
    const auto a = std::ranges::find(options, std::string_view("A"), &stui::runtime::HotkeyOption::label);
    expect(a != options.end() && a->key == 0x1E, "expected A DIK mapping");
    const auto zero = std::ranges::find(options, std::string_view("0"), &stui::runtime::HotkeyOption::label);
    expect(zero != options.end() && zero->key == 0x0B, "expected 0 DIK mapping");
}

void selectedDropdownOptionIsSavedImmediately() {
    TemporaryDirectory directory;
    const auto configPath = directory.path() / "SlaveTatsUI.json";
    stui::runtime::HotkeyBinding binding(configPath);
    expect(binding.load(), "expected initial configuration");

    expect(binding.select(0x43), "expected selected F9 to save");
    expect(binding.key() == 0x43, "expected selected F9 DIK code");
    expect(binding.label() == "F9", "expected selected key label");

    stui::runtime::HotkeyBinding restored(configPath);
    expect(restored.load(), "expected saved configuration to load");
    expect(restored.key() == 0x43, "expected persisted F9 DIK code");

    expect(restored.select(std::nullopt), "expected None selection to save");
    expect(!restored.key(), "expected None to disable the hotkey");
}

void clearDisablesAndPersistsNoHotkey() {
    TemporaryDirectory directory;
    const auto configPath = directory.path() / "SlaveTatsUI.json";
    stui::runtime::HotkeyBinding binding(configPath);
    expect(binding.load(), "expected initial configuration");
    expect(binding.select(0x42), "expected F8 selection to save");
    expect(binding.matches(0x42), "expected configured F8 to match");

    expect(binding.select(std::nullopt), "expected None selection to save");
    expect(!binding.key(), "expected cleared hotkey");
    expect(!binding.matches(0x42), "expected F8 to be disabled after clear");

    std::ifstream input(configPath);
    const auto json = nlohmann::json::parse(input);
    expect(json.at("hotkey").is_null(), "expected clear to persist null hotkey");
}

void legacyNamedHotkeyLoadsAndUnrelatedSettingsSurviveClear() {
    TemporaryDirectory directory;
    const auto configPath = directory.path() / "SlaveTatsUI.json";
    {
        std::ofstream output(configPath);
        output << R"({"hotkey":"F8","anotherSetting":true})";
    }

    stui::runtime::HotkeyBinding binding(configPath);
    expect(binding.load(), "expected legacy named hotkey to load");
    expect(binding.key() == 0x42, "expected legacy F8 DIK code");
    expect(binding.select(std::nullopt), "expected legacy hotkey clear to save");

    std::ifstream input(configPath);
    const auto json = nlohmann::json::parse(input);
    expect(json.at("hotkey").is_null(), "expected legacy hotkey to clear");
    expect(json.at("anotherSetting") == true, "expected unrelated setting to survive clear");
}

void sharedConfigurationPreservesFavoritesWhenHotkeyChanges() {
    TemporaryDirectory directory;
    const auto configPath = directory.path() / "SlaveTatsUI.json";
    auto config = std::make_shared<stui::runtime::PluginConfigFile>(configPath);
    expect(config->update([](nlohmann::json& json) -> stui::runtime::ConfigUpdateResult {
        json["favorites"] = {
            {"version", 1},
            {"entries", {{{"domain", "default"}, {"sourceId", "marks.json"},
                          {"section", "Marks"}, {"name", "Rose"}}}},
        };
        return {};
    }).has_value(), "expected favorite fixture to save");

    stui::runtime::HotkeyBinding binding(config);
    expect(binding.load(), "expected shared configuration hotkey load");
    expect(binding.select(0x43), "expected hotkey update through shared configuration");

    const auto result = config->read();
    expect(result && result->at("hotkey") == 0x43 &&
            result->at("favorites").at("entries").size() == 1,
        "expected hotkey write to preserve favorites");
}

}  // namespace

int main() {
    try {
        missingConfigurationDefaultsToNoHotkey();
        std::cout << "PASS missing configuration defaults to no hotkey\n";
        dropdownOptionsStartWithNoneAndExposeKeyboardKeys();
        std::cout << "PASS dropdown options start with None and expose keyboard keys\n";
        selectedDropdownOptionIsSavedImmediately();
        std::cout << "PASS selected dropdown option is saved immediately\n";
        clearDisablesAndPersistsNoHotkey();
        std::cout << "PASS clear disables and persists no hotkey\n";
        legacyNamedHotkeyLoadsAndUnrelatedSettingsSurviveClear();
        std::cout << "PASS legacy named hotkey loads and unrelated settings survive clear\n";
        sharedConfigurationPreservesFavoritesWhenHotkeyChanges();
        std::cout << "PASS shared configuration preserves favorites when hotkey changes\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
