#include "runtime/PluginConfigFile.h"

#include <filesystem>
#include <fstream>
#include <iostream>
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
    TemporaryDirectory() : path_(std::filesystem::temp_directory_path() / "SlaveTatsUIPluginConfigFileTests") {
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

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void missingFileReadsAsEmptyObjectWithoutCreatingIt() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    stui::runtime::PluginConfigFile file(path);

    const auto result = file.read();

    expect(result && result->is_object() && result->empty(),
        "expected missing configuration to read as an empty object");
    expect(!std::filesystem::exists(path),
        "expected read-only missing configuration lookup to leave no file behind");
}

void updatesPreserveExistingAndUnknownKeys() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    stui::runtime::PluginConfigFile file(path);

    const auto first = file.update([](nlohmann::json& config) -> stui::runtime::ConfigUpdateResult {
        config["hotkey"] = 67;
        config["nested"] = {{"keep", true}};
        return {};
    });
    const auto second = file.update([](nlohmann::json& config) -> stui::runtime::ConfigUpdateResult {
        config["favorites"] = {{"version", 1}, {"entries", nlohmann::json::array()}};
        return {};
    });
    const auto result = file.read();

    expect(first && second, "expected both configuration updates to commit");
    expect(result && result->at("hotkey") == 67 && result->at("nested").at("keep") == true &&
            result->at("favorites").at("version") == 1,
        "expected independent updates to preserve existing and unknown keys");
}

void malformedFileIsReportedWithoutOverwritingBytes() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "SlaveTatsUI.json";
    constexpr std::string_view malformed = "{ definitely not json";
    {
        std::ofstream output(path, std::ios::binary);
        output << malformed;
    }
    stui::runtime::PluginConfigFile file(path);

    const auto readResult = file.read();
    const auto updateResult = file.update([](nlohmann::json& config) -> stui::runtime::ConfigUpdateResult {
        config["hotkey"] = 67;
        return {};
    });

    expect(!readResult && !updateResult,
        "expected malformed configuration to reject read and update");
    expect(readBytes(path) == malformed,
        "expected malformed configuration bytes to remain recoverable");
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
    failures += run("missing file reads without creating", missingFileReadsAsEmptyObjectWithoutCreatingIt);
    failures += run("updates preserve existing and unknown keys", updatesPreserveExistingAndUnknownKeys);
    failures += run("malformed file preserves bytes", malformedFileIsReportedWithoutOverwritingBytes);
    return failures == 0 ? 0 : 1;
}
