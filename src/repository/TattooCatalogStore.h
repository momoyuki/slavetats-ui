#pragma once

#include "repository/TattooCatalogLoader.h"

#include <atomic>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <system_error>

namespace stui::repository {

using TattooCatalogSnapshot = std::shared_ptr<const TattooCatalog>;
using TattooCatalogRefreshResult =
    std::expected<TattooCatalogSnapshot, std::error_code>;

class TattooCatalogStore {
public:
    [[nodiscard]] TattooCatalogRefreshResult refresh(
        const std::filesystem::path& sourceDirectory);
    // The scheduler owner must join its work before this store is destroyed.
    void refreshAsync(std::filesystem::path sourceDirectory,
        const std::function<void(std::function<void()>)>& scheduler,
        std::function<void(TattooCatalogRefreshResult)> complete);
    [[nodiscard]] TattooCatalogSnapshot snapshot() const noexcept;

private:
    std::atomic<TattooCatalogSnapshot> m_snapshot;
};

}  // namespace stui::repository
