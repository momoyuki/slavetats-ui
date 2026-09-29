#pragma once

#include "runtime/PluginConfigFile.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace stui::runtime {

inline constexpr std::size_t kAppearancePresetLimit = 20;

struct AppearancePreset {
    std::string name;
    std::uint32_t color{};
    float alpha{1.0F};
    std::uint32_t glow{};
    float emissiveMult{1.0F};
    float glossiness{};
    float specularStrength{};

    bool operator==(const AppearancePreset&) const = default;
};

using AppearancePresetList = std::vector<AppearancePreset>;
using AppearancePresetResult = std::expected<AppearancePresetList, ConfigError>;

class AppearancePresetStore {
public:
    explicit AppearancePresetStore(std::shared_ptr<PluginConfigFile> file);

    [[nodiscard]] AppearancePresetResult load();
    [[nodiscard]] AppearancePresetResult create(AppearancePreset preset);
    [[nodiscard]] AppearancePresetResult overwrite(AppearancePreset preset);
    [[nodiscard]] AppearancePresetResult rename(
        std::string_view oldName,
        std::string newName);
    [[nodiscard]] AppearancePresetResult erase(std::string_view name);

private:
    std::shared_ptr<PluginConfigFile> m_file;
};

}  // namespace stui::runtime
