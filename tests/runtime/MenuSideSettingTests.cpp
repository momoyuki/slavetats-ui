#include "runtime/MenuSideSetting.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void menuSideDefaultsAndPersistsWithoutChangingOtherSettings() {
    const auto path = std::filesystem::temp_directory_path() / "SlaveTatsUIMenuSideTests.json";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    auto config = std::make_shared<stui::runtime::PluginConfigFile>(path);
    stui::runtime::MenuSideSetting setting(config);
    expect(setting.load(), "missing side should load");
    expect(setting.side() == stui::runtime::MenuSide::left, "default side should be left");
    expect(config->update([](nlohmann::json& json) -> stui::runtime::ConfigUpdateResult {
        json["hotkey"] = 67;
        return {};
    }).has_value(), "hotkey fixture should save");
    expect(setting.select(stui::runtime::MenuSide::right), "right side should save");
    stui::runtime::MenuSideSetting restored(config);
    expect(restored.load(), "saved side should load");
    expect(restored.side() == stui::runtime::MenuSide::right, "saved right side should restore");
    const auto json = config->read();
    expect(json && json->at("hotkey") == 67, "side save should preserve hotkey");
    std::filesystem::remove(path, ignored);
}

void invalidValueKeepsSafeDefault() {
    const auto path = std::filesystem::temp_directory_path() / "SlaveTatsUIMenuSideInvalid.json";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    auto config = std::make_shared<stui::runtime::PluginConfigFile>(path);
    stui::runtime::MenuSideSetting setting(config);
    expect(setting.select(stui::runtime::MenuSide::right), "right-side fixture should save");
    { std::ofstream output(path); output << R"({"menuSide":"center"})"; }
    expect(!setting.load(), "invalid value should report failure");
    expect(setting.side() == stui::runtime::MenuSide::left, "invalid value should keep left side");
    std::filesystem::remove(path, ignored);
}

}  // namespace

int main() {
    try {
        menuSideDefaultsAndPersistsWithoutChangingOtherSettings();
        invalidValueKeepsSafeDefault();
        std::cout << "PASS menu side setting\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
