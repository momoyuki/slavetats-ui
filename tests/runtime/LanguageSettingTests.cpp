#include "runtime/LanguageSetting.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace stui;
void expect(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    const auto dir = std::filesystem::temp_directory_path() / "stui-language-setting-tests";
    std::filesystem::create_directories(dir);
    try {
        std::ofstream(dir / "fr.tsv") << "# language-name: French\nSave\tEnregistrer\n";
        for (const auto code : {"en", "zh-CN", "ko", "ja"}) std::ofstream(dir / (std::string(code) + ".tsv")) << "Save\tSave\n";
        expect(localization::catalog().load(dir), "test locales discovered");
        const auto path = dir / "config.json";
        std::filesystem::remove(path);
        auto config = std::make_shared<runtime::PluginConfigFile>(path);
        runtime::LanguageSetting setting(config);
        expect(setting.load() && setting.language() == "en", "missing locale defaults to English");
        std::ofstream(path) << R"({"hotkey":"F8","menuSide":"right","favorites":["keep"]})";
        for (const auto language : {"zh-CN", "ko", "ja", "fr"}) {
            expect(setting.select(language), "save language");
            runtime::LanguageSetting reloaded(config);
            expect(reloaded.load() && reloaded.language() == language, "language persists after reload");
        }
        auto json = nlohmann::json::parse(std::ifstream(path));
        expect(json["hotkey"] == "F8" && json["menuSide"] == "right" && json["favorites"][0] == "keep", "other configuration preserved");
        std::ofstream(path) << R"({"language":"unknown"})";
        expect(!setting.load() && setting.language() == "en", "unknown locale falls back");
        std::ofstream(path) << R"({"language":42})";
        expect(!setting.load(), "non-string language rejected");
        std::ofstream(path) << R"({"language":"ko"})";
        expect(setting.load(), "known locale loads");
        std::ofstream(path) << "broken JSON";
        expect(!setting.select(localization::Language::japanese), "bad config not overwritten");
        expect(setting.language() == "ko", "failed save keeps active language");
        runtime::LanguageSetting disabled(nullptr);
        expect(!disabled.select(localization::Language::japanese), "persistence unavailable reported");
        std::filesystem::remove_all(dir);
        std::cout << "PASS language persistence, fallback and failed-write behavior\n";
    } catch (const std::exception& e) { std::filesystem::remove_all(dir); std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
