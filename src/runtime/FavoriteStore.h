#pragma once

#include "repository/FavoriteIdentity.h"
#include "runtime/PluginConfigFile.h"

#include <expected>
#include <memory>
#include <vector>

namespace stui::runtime {

using FavoriteList = std::vector<repository::FavoriteIdentity>;
using FavoriteResult = std::expected<FavoriteList, ConfigError>;

class FavoriteStore {
public:
    explicit FavoriteStore(std::shared_ptr<PluginConfigFile> file);

    [[nodiscard]] FavoriteResult load();
    [[nodiscard]] FavoriteResult setFavorite(
        const repository::FavoriteIdentity& identity,
        bool enabled);

private:
    std::shared_ptr<PluginConfigFile> m_file;
};

}  // namespace stui::runtime
