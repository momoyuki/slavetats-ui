#pragma once

#include "runtime/PluginConfigFile.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace stui::runtime {

struct HotkeyOption {
    std::string_view label;
    std::optional<std::uint32_t> key;
};

[[nodiscard]] std::span<const HotkeyOption> hotkeyOptions() noexcept;

class HotkeyBinding {
public:
    explicit HotkeyBinding(std::filesystem::path configPath);
    explicit HotkeyBinding(std::shared_ptr<PluginConfigFile> config);

    [[nodiscard]] bool load();
    [[nodiscard]] bool select(std::optional<std::uint32_t> key);
    [[nodiscard]] bool matches(std::uint32_t key) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> key() const noexcept;
    [[nodiscard]] std::string label() const;

private:
    [[nodiscard]] bool save() const;

    std::shared_ptr<PluginConfigFile> m_config;
    mutable std::mutex mutex_;
    std::optional<std::uint32_t> key_;
};

}  // namespace stui::runtime
