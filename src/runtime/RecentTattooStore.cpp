#include "runtime/RecentTattooStore.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace stui::runtime {
namespace {

constexpr int kRecentlyUsedVersion = 1;
constexpr std::array<std::string_view, 5> kIdentityFields{
    "domain", "sourceId", "section", "name", "area"};

ConfigError error(std::string message) {
    return ConfigError{.message = std::move(message)};
}

std::string_view areaName(const core::TattooArea area) {
    switch (area) {
    case core::TattooArea::body: return "Body";
    case core::TattooArea::face: return "Face";
    case core::TattooArea::hands: return "Hands";
    case core::TattooArea::feet: return "Feet";
    }
    return {};
}

std::optional<core::TattooArea> parseArea(const std::string_view value) {
    if (value == "Body") return core::TattooArea::body;
    if (value == "Face") return core::TattooArea::face;
    if (value == "Hands") return core::TattooArea::hands;
    if (value == "Feet") return core::TattooArea::feet;
    return std::nullopt;
}

bool hasCompleteIdentity(const repository::RecentTattooIdentity& identity) {
    return !identity.domain.empty() && !identity.sourceId.empty() &&
        !identity.section.empty() && !identity.name.empty() &&
        !areaName(identity.area).empty();
}

RecentTattooResult parseEntries(const nlohmann::json& entries) {
    if (!entries.is_array()) {
        return std::unexpected(error("Recently Used entries must be an array."));
    }

    RecentTattooList recent;
    recent.reserve(entries.size());
    std::array<std::size_t, 4> areaCounts{};
    for (const auto& entry : entries) {
        if (!entry.is_object()) {
            return std::unexpected(error("Each Recently Used entry must be an object."));
        }
        for (const auto field : kIdentityFields) {
            const auto iterator = entry.find(field);
            if (iterator == entry.end() || !iterator->is_string() ||
                iterator->get_ref<const std::string&>().empty()) {
                return std::unexpected(error(
                    "Recently Used entries require non-empty string identity fields."));
            }
        }
        const auto area = parseArea(entry.at("area").get_ref<const std::string&>());
        if (!area) {
            return std::unexpected(error("Recently Used entry area is unsupported."));
        }
        repository::RecentTattooIdentity identity{
            .domain = entry.at("domain").get<std::string>(),
            .sourceId = entry.at("sourceId").get<std::string>(),
            .section = entry.at("section").get<std::string>(),
            .name = entry.at("name").get<std::string>(),
            .area = *area,
        };
        if (std::ranges::find(recent, identity) != recent.end()) {
            continue;
        }
        const auto areaIndex = static_cast<std::size_t>(*area);
        if (++areaCounts.at(areaIndex) > kRecentTattooLimitPerArea) {
            return std::unexpected(error("Recently Used area exceeds its entry limit."));
        }
        recent.push_back(std::move(identity));
    }
    return recent;
}

RecentTattooResult readRecent(const nlohmann::json& config) {
    const auto iterator = config.find("recentlyUsed");
    if (iterator == config.end()) {
        return RecentTattooList{};
    }
    if (!iterator->is_object()) {
        return std::unexpected(error("Recently Used configuration must be an object."));
    }
    const auto version = iterator->find("version");
    if (version == iterator->end() || !version->is_number_integer() ||
        version->get<int>() != kRecentlyUsedVersion) {
        return std::unexpected(error("Recently Used configuration version is unsupported."));
    }
    const auto entries = iterator->find("entries");
    if (entries == iterator->end()) {
        return std::unexpected(error("Recently Used configuration entries are missing."));
    }
    return parseEntries(*entries);
}

nlohmann::json serializeEntries(const RecentTattooList& recent) {
    auto entries = nlohmann::json::array();
    for (const auto& identity : recent) {
        entries.push_back({
            {"domain", identity.domain},
            {"sourceId", identity.sourceId},
            {"section", identity.section},
            {"name", identity.name},
            {"area", areaName(identity.area)},
        });
    }
    return entries;
}

}  // namespace

RecentTattooStore::RecentTattooStore(std::shared_ptr<PluginConfigFile> file) :
    m_file(std::move(file)) {}

RecentTattooResult RecentTattooStore::load() {
    if (!m_file) {
        return std::unexpected(error("Recently Used configuration is unavailable."));
    }
    const auto config = m_file->read();
    if (!config) {
        return std::unexpected(config.error());
    }
    return readRecent(*config);
}

RecentTattooResult RecentTattooStore::record(
    const repository::RecentTattooIdentity& identity) {
    if (!m_file) {
        return std::unexpected(error("Recently Used configuration is unavailable."));
    }
    if (!hasCompleteIdentity(identity)) {
        return std::unexpected(error("Recently Used identity fields must be non-empty."));
    }

    RecentTattooList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto recent = readRecent(config);
        if (!recent) {
            return std::unexpected(recent.error());
        }
        std::erase(*recent, identity);
        recent->insert(recent->begin(), identity);

        std::size_t count{};
        for (auto iterator = recent->begin(); iterator != recent->end();) {
            if (iterator->area != identity.area) {
                ++iterator;
                continue;
            }
            ++count;
            if (count > kRecentTattooLimitPerArea) {
                iterator = recent->erase(iterator);
            } else {
                ++iterator;
            }
        }

        auto& stored = config["recentlyUsed"];
        if (!stored.is_object()) {
            stored = nlohmann::json::object();
        }
        stored["version"] = kRecentlyUsedVersion;
        stored["entries"] = serializeEntries(*recent);
        committed = std::move(*recent);
        return {};
    });
    if (!updated) {
        return std::unexpected(updated.error());
    }
    return committed;
}

}  // namespace stui::runtime
