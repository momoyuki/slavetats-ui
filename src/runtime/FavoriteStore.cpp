#include "runtime/FavoriteStore.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

namespace stui::runtime {
namespace {

constexpr int kFavoritesVersion = 1;
constexpr std::array<std::string_view, 4> kIdentityFields{
    "domain", "sourceId", "section", "name"};

ConfigError error(std::string message) {
    return ConfigError{.message = std::move(message)};
}

bool hasCompleteIdentity(const repository::FavoriteIdentity& identity) {
    return !identity.domain.empty() && !identity.sourceId.empty() &&
        !identity.section.empty() && !identity.name.empty();
}

bool identityLess(
    const repository::FavoriteIdentity& left,
    const repository::FavoriteIdentity& right) {
    return std::tie(left.domain, left.sourceId, left.section, left.name) <
        std::tie(right.domain, right.sourceId, right.section, right.name);
}

void normalize(FavoriteList& favorites) {
    std::sort(favorites.begin(), favorites.end(), identityLess);
    favorites.erase(std::unique(favorites.begin(), favorites.end()), favorites.end());
}

FavoriteResult parseEntries(const nlohmann::json& entries) {
    if (!entries.is_array()) {
        return std::unexpected(error("Favorites entries must be an array."));
    }

    FavoriteList favorites;
    favorites.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!entry.is_object()) {
            return std::unexpected(error("Each favorite entry must be an object."));
        }
        for (const auto field : kIdentityFields) {
            const auto iterator = entry.find(field);
            if (iterator == entry.end() || !iterator->is_string() || iterator->get_ref<const std::string&>().empty()) {
                return std::unexpected(error("Favorite entries require non-empty string identity fields."));
            }
        }
        favorites.push_back(repository::FavoriteIdentity{
            .domain = entry.at("domain").get<std::string>(),
            .sourceId = entry.at("sourceId").get<std::string>(),
            .section = entry.at("section").get<std::string>(),
            .name = entry.at("name").get<std::string>(),
        });
    }
    normalize(favorites);
    return favorites;
}

FavoriteResult readFavorites(const nlohmann::json& config) {
    const auto iterator = config.find("favorites");
    if (iterator == config.end()) {
        return FavoriteList{};
    }
    if (!iterator->is_object()) {
        return std::unexpected(error("Favorites configuration must be an object."));
    }
    const auto version = iterator->find("version");
    if (version == iterator->end() || !version->is_number_integer() ||
        version->get<int>() != kFavoritesVersion) {
        return std::unexpected(error("Favorites configuration version is unsupported."));
    }
    const auto entries = iterator->find("entries");
    if (entries == iterator->end()) {
        return std::unexpected(error("Favorites configuration entries are missing."));
    }
    return parseEntries(*entries);
}

nlohmann::json serializeEntries(const FavoriteList& favorites) {
    auto entries = nlohmann::json::array();
    for (const auto& favorite : favorites) {
        entries.push_back({
            {"domain", favorite.domain},
            {"sourceId", favorite.sourceId},
            {"section", favorite.section},
            {"name", favorite.name},
        });
    }
    return entries;
}

}  // namespace

FavoriteStore::FavoriteStore(std::shared_ptr<PluginConfigFile> file) : m_file(std::move(file)) {}

FavoriteResult FavoriteStore::load() {
    if (!m_file) {
        return std::unexpected(error("Favorites configuration is unavailable."));
    }
    const auto config = m_file->read();
    if (!config) {
        return std::unexpected(config.error());
    }
    return readFavorites(*config);
}

FavoriteResult FavoriteStore::setFavorite(
    const repository::FavoriteIdentity& identity,
    const bool enabled) {
    if (!m_file) {
        return std::unexpected(error("Favorites configuration is unavailable."));
    }
    if (!hasCompleteIdentity(identity)) {
        return std::unexpected(error("Favorite identity fields must be non-empty."));
    }

    FavoriteList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto favorites = readFavorites(config);
        if (!favorites) {
            return std::unexpected(favorites.error());
        }

        const auto found = std::find(favorites->begin(), favorites->end(), identity);
        if (enabled && found == favorites->end()) {
            favorites->push_back(identity);
        } else if (!enabled && found != favorites->end()) {
            favorites->erase(found);
        }
        normalize(*favorites);

        auto& storedFavorites = config["favorites"];
        if (!storedFavorites.is_object()) {
            storedFavorites = nlohmann::json::object();
        }
        storedFavorites["version"] = kFavoritesVersion;
        storedFavorites["entries"] = serializeEntries(*favorites);
        committed = std::move(*favorites);
        return {};
    });
    if (!updated) {
        return std::unexpected(updated.error());
    }
    return committed;
}

}  // namespace stui::runtime
