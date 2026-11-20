#include "repository/TattooCatalogStore.h"

#include <utility>

namespace stui::repository {

TattooCatalogRefreshResult TattooCatalogStore::refresh(
    const std::filesystem::path& sourceDirectory) {
    auto catalog = loadTattooCatalog(sourceDirectory);
    if (!catalog) {
        return std::unexpected(catalog.error());
    }

    TattooCatalogSnapshot next =
        std::make_shared<TattooCatalog>(std::move(*catalog));
    m_snapshot.store(next, std::memory_order_release);
    return next;
}

void TattooCatalogStore::refreshAsync(std::filesystem::path sourceDirectory,
    const std::function<void(std::function<void()>)>& scheduler,
    std::function<void(TattooCatalogRefreshResult)> complete) {
    scheduler([this, path = std::move(sourceDirectory), complete = std::move(complete)] {
        TattooCatalogRefreshResult result = std::unexpected(std::make_error_code(std::errc::io_error));
        try { result = refresh(path); } catch (...) {
            // Report worker-side scan/parser exceptions through the same completion path.
        }
        if (complete) complete(std::move(result));
    });
}

TattooCatalogSnapshot TattooCatalogStore::snapshot() const noexcept {
    return m_snapshot.load(std::memory_order_acquire);
}

}  // namespace stui::repository
