#pragma once

#include "repository/RecentTattooIdentity.h"
#include "runtime/PluginConfigFile.h"

#include <cstddef>
#include <expected>
#include <memory>
#include <vector>

namespace stui::runtime {

inline constexpr std::size_t kRecentTattooLimitPerArea = 10;
using RecentTattooList = std::vector<repository::RecentTattooIdentity>;
using RecentTattooResult = std::expected<RecentTattooList, ConfigError>;

class RecentTattooStore {
public:
    explicit RecentTattooStore(std::shared_ptr<PluginConfigFile> file);

    [[nodiscard]] RecentTattooResult load();
    [[nodiscard]] RecentTattooResult record(
        const repository::RecentTattooIdentity& identity);

private:
    std::shared_ptr<PluginConfigFile> m_file;
};

}  // namespace stui::runtime
