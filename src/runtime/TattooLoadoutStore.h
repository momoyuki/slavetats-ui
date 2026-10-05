#pragma once

#include "core/TattooLoadoutModels.h"
#include "runtime/PluginConfigFile.h"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace stui::runtime {

using TattooLoadoutResult = std::expected<core::TattooLoadoutList, ConfigError>;

class TattooLoadoutStore {
public:
    explicit TattooLoadoutStore(std::shared_ptr<PluginConfigFile> file);

    [[nodiscard]] TattooLoadoutResult load();
    [[nodiscard]] TattooLoadoutResult create(core::TattooLoadout loadout);
    [[nodiscard]] TattooLoadoutResult overwrite(core::TattooLoadout loadout);
    [[nodiscard]] TattooLoadoutResult rename(
        std::string_view oldName,
        std::string newName);
    [[nodiscard]] TattooLoadoutResult erase(std::string_view name);

private:
    std::shared_ptr<PluginConfigFile> m_file;
};

}  // namespace stui::runtime
