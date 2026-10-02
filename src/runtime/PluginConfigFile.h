#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace stui::runtime {

struct ConfigError {
    std::string message;
};

using ConfigUpdateResult = std::expected<void, ConfigError>;
using ConfigEdit = std::function<ConfigUpdateResult(nlohmann::json&)>;

class PluginConfigFile {
public:
    explicit PluginConfigFile(std::filesystem::path path);

    [[nodiscard]] std::expected<nlohmann::json, ConfigError> read() const;
    [[nodiscard]] ConfigUpdateResult update(const ConfigEdit& edit);

private:
    [[nodiscard]] std::expected<nlohmann::json, ConfigError> readUnlocked() const;

    std::filesystem::path m_path;
    mutable std::mutex m_mutex;
};

}  // namespace stui::runtime
