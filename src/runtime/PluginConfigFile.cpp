#include "runtime/PluginConfigFile.h"

#include <atomic>
#include <fstream>
#include <utility>

#include <Windows.h>
#include <nlohmann/json.hpp>

namespace stui::runtime {
namespace {

std::atomic_uint64_t g_temporaryFileSequence{};

ConfigError error(std::string message) {
    return ConfigError{.message = std::move(message)};
}

std::filesystem::path temporaryPathFor(const std::filesystem::path& path) {
    const auto sequence = g_temporaryFileSequence.fetch_add(1, std::memory_order_relaxed);
    return path.parent_path() /
        (path.filename().wstring() + L".tmp-" + std::to_wstring(sequence));
}

}  // namespace

PluginConfigFile::PluginConfigFile(std::filesystem::path path) : m_path(std::move(path)) {}

std::expected<nlohmann::json, ConfigError> PluginConfigFile::read() const {
    const std::scoped_lock lock(m_mutex);
    return readUnlocked();
}

ConfigUpdateResult PluginConfigFile::update(const ConfigEdit& edit) {
    if (!edit) {
        return std::unexpected(error("Configuration update callback is missing."));
    }

    const std::scoped_lock lock(m_mutex);
    auto config = readUnlocked();
    if (!config) {
        return std::unexpected(std::move(config.error()));
    }
    if (auto edited = edit(*config); !edited) {
        return std::unexpected(std::move(edited.error()));
    }
    if (m_path.empty() || !m_path.is_absolute()) {
        return std::unexpected(error("Configuration path must be absolute."));
    }

    const auto temporaryPath = temporaryPathFor(m_path);
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            return std::unexpected(error("Unable to create temporary configuration file."));
        }
        output << config->dump(2);
        output.flush();
        if (!output) {
            output.close();
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            return std::unexpected(error("Unable to write temporary configuration file."));
        }
    }

    if (!MoveFileExW(
            temporaryPath.c_str(),
            m_path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryPath, cleanupError);
        return std::unexpected(error("Unable to replace configuration file."));
    }
    return {};
}

std::expected<nlohmann::json, ConfigError> PluginConfigFile::readUnlocked() const {
    if (m_path.empty() || !m_path.is_absolute()) {
        return std::unexpected(error("Configuration path must be absolute."));
    }
    if (!std::filesystem::exists(m_path)) {
        return nlohmann::json::object();
    }

    std::ifstream input(m_path, std::ios::binary);
    if (!input) {
        return std::unexpected(error("Unable to read configuration file."));
    }
    auto config = nlohmann::json::parse(input, nullptr, false);
    if (config.is_discarded() || !config.is_object()) {
        return std::unexpected(error("Configuration file must contain a JSON object."));
    }
    return config;
}

}  // namespace stui::runtime
