#include "runtime/TattooLoadoutStore.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace stui::runtime {
namespace {

constexpr int kTattooLoadoutsVersion = 1;
constexpr std::int32_t kMaximumColor = 0xFFFFFF;
constexpr std::array<std::string_view, 11> kRequiredTattooFields{
    "area", "slot", "domain", "section", "name", "color", "alpha", "glow",
    "emissiveMult", "glossiness", "specularStrength"};

ConfigError error(std::string message) {
    return ConfigError{.message = std::move(message)};
}

bool isAsciiWhitespace(const unsigned char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
        value == '\f' || value == '\v';
}

std::string trimName(std::string name) {
    const auto first = std::find_if_not(name.begin(), name.end(), [](const char value) {
        return isAsciiWhitespace(static_cast<unsigned char>(value));
    });
    const auto last = std::find_if_not(name.rbegin(), name.rend(), [](const char value) {
        return isAsciiWhitespace(static_cast<unsigned char>(value));
    }).base();
    return first >= last ? std::string{} : std::string(first, last);
}

std::string foldName(const std::string_view name) {
    std::string folded;
    folded.reserve(name.size());
    for (const unsigned char value : name) {
        folded.push_back(value >= 'A' && value <= 'Z' ?
            static_cast<char>(value - 'A' + 'a') : static_cast<char>(value));
    }
    return folded;
}

int areaOrder(const core::TattooArea area) {
    switch (area) {
    case core::TattooArea::body: return 0;
    case core::TattooArea::face: return 1;
    case core::TattooArea::hands: return 2;
    case core::TattooArea::feet: return 3;
    }
    return -1;
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

std::optional<ConfigError> validateEntry(const core::TattooLoadoutEntry& entry) {
    const auto validation = core::validateTattooLoadoutEntry(entry);
    if (validation) return std::nullopt;

    switch (validation.error()) {
    case core::TattooLoadoutValidationError::invalidArea:
        return error("Tattoo loadout area is invalid.");
    case core::TattooLoadoutValidationError::invalidSlot:
        return error("Tattoo loadout slot must not be negative.");
    case core::TattooLoadoutValidationError::missingIdentity:
        return error("Tattoo loadout identity fields must not be empty.");
    case core::TattooLoadoutValidationError::invalidColor:
        return error("Tattoo loadout colors must be between 0 and 0xFFFFFF.");
    case core::TattooLoadoutValidationError::invalidAppearance:
        return error("Tattoo loadout appearance values are invalid.");
    case core::TattooLoadoutValidationError::missingName:
    case core::TattooLoadoutValidationError::duplicatePosition:
        break;
    }
    return error("Tattoo loadout entry is invalid.");
}

std::optional<ConfigError> validateLoadout(core::TattooLoadout& loadout) {
    loadout.name = trimName(std::move(loadout.name));
    if (loadout.name.empty()) {
        return error("Tattoo loadout name must not be empty.");
    }
    for (const auto& entry : loadout.entries) {
        if (auto invalid = validateEntry(entry)) {
            return invalid;
        }
    }
    std::ranges::sort(loadout.entries, [](const auto& left, const auto& right) {
        return std::pair{areaOrder(left.area), left.slot} <
            std::pair{areaOrder(right.area), right.slot};
    });
    for (std::size_t index = 1; index < loadout.entries.size(); ++index) {
        const auto& previous = loadout.entries[index - 1];
        const auto& current = loadout.entries[index];
        if (previous.area == current.area && previous.slot == current.slot) {
            return error("Tattoo loadout contains duplicate area and slot entries.");
        }
    }
    return std::nullopt;
}

std::expected<core::TattooLoadoutEntry, ConfigError> parseTattoo(
    const nlohmann::json& tattoo) {
    if (!tattoo.is_object()) {
        return std::unexpected(error("Each tattoo loadout tattoo must be an object."));
    }
    for (const auto field : kRequiredTattooFields) {
        if (!tattoo.contains(field)) {
            return std::unexpected(error("Tattoo loadout tattoo is missing required fields."));
        }
    }
    if (!tattoo.at("area").is_string() || !tattoo.at("slot").is_number_integer() ||
        !tattoo.at("domain").is_string() || !tattoo.at("section").is_string() ||
        !tattoo.at("name").is_string() || !tattoo.at("color").is_number_integer() ||
        !tattoo.at("alpha").is_number() || !tattoo.at("glow").is_number_integer() ||
        !tattoo.at("emissiveMult").is_number() || !tattoo.at("glossiness").is_number() ||
        !tattoo.at("specularStrength").is_number()) {
        return std::unexpected(error("Tattoo loadout tattoo fields have invalid types."));
    }
    const auto area = parseArea(tattoo.at("area").get<std::string>());
    if (!area) {
        return std::unexpected(error("Tattoo loadout area is invalid."));
    }
    const auto slot = tattoo.at("slot").get<std::int64_t>();
    const auto color = tattoo.at("color").get<std::int64_t>();
    const auto glow = tattoo.at("glow").get<std::int64_t>();
    if (slot < 0 || slot > std::numeric_limits<std::int32_t>::max() ||
        color < 0 || color > kMaximumColor || glow < 0 || glow > kMaximumColor) {
        return std::unexpected(error("Tattoo loadout integer field is out of range."));
    }
    core::TattooLoadoutEntry entry{
        .area = *area,
        .slot = static_cast<std::int32_t>(slot),
        .domain = tattoo.at("domain").get<std::string>(),
        .section = tattoo.at("section").get<std::string>(),
        .name = tattoo.at("name").get<std::string>(),
        .color = static_cast<std::int32_t>(color),
        .alpha = tattoo.at("alpha").get<float>(),
        .glow = static_cast<std::int32_t>(glow),
        .emissiveMult = tattoo.at("emissiveMult").get<float>(),
        .glossiness = tattoo.at("glossiness").get<float>(),
        .specularStrength = tattoo.at("specularStrength").get<float>(),
    };
    if (auto invalid = validateEntry(entry)) {
        return std::unexpected(std::move(*invalid));
    }
    return entry;
}

TattooLoadoutResult parseLoadouts(const nlohmann::json& entries) {
    if (!entries.is_array()) {
        return std::unexpected(error("Tattoo loadout entries must be an array."));
    }
    if (entries.size() > core::kTattooLoadoutLimit) {
        return std::unexpected(error("Tattoo loadouts exceed the entry limit."));
    }
    core::TattooLoadoutList loadouts;
    std::vector<std::string> foldedNames;
    for (const auto& stored : entries) {
        if (!stored.is_object() || !stored.contains("name") || !stored.contains("tattoos") ||
            !stored.at("name").is_string() || !stored.at("tattoos").is_array()) {
            return std::unexpected(error("Tattoo loadout entry fields are invalid."));
        }
        core::TattooLoadout loadout{.name = stored.at("name").get<std::string>()};
        for (const auto& tattoo : stored.at("tattoos")) {
            auto parsed = parseTattoo(tattoo);
            if (!parsed) return std::unexpected(parsed.error());
            loadout.entries.push_back(std::move(*parsed));
        }
        if (auto invalid = validateLoadout(loadout)) {
            return std::unexpected(std::move(*invalid));
        }
        auto folded = foldName(loadout.name);
        if (std::ranges::find(foldedNames, folded) != foldedNames.end()) {
            return std::unexpected(error("Tattoo loadout names must be unique."));
        }
        foldedNames.push_back(std::move(folded));
        loadouts.push_back(std::move(loadout));
    }
    return loadouts;
}

TattooLoadoutResult readLoadouts(const nlohmann::json& config) {
    const auto stored = config.find("tattooLoadouts");
    if (stored == config.end()) return core::TattooLoadoutList{};
    if (!stored->is_object()) {
        return std::unexpected(error("Tattoo loadout configuration must be an object."));
    }
    const auto version = stored->find("version");
    const auto entries = stored->find("entries");
    if (version == stored->end() || !version->is_number_integer() ||
        version->get<int>() != kTattooLoadoutsVersion) {
        return std::unexpected(error("Tattoo loadout configuration version is unsupported."));
    }
    if (entries == stored->end()) {
        return std::unexpected(error("Tattoo loadout configuration entries are missing."));
    }
    return parseLoadouts(*entries);
}

nlohmann::json serializeTattoo(const core::TattooLoadoutEntry& entry) {
    return {
        {"area", areaName(entry.area)}, {"slot", entry.slot}, {"domain", entry.domain},
        {"section", entry.section}, {"name", entry.name}, {"color", entry.color},
        {"alpha", entry.alpha}, {"glow", entry.glow},
        {"emissiveMult", entry.emissiveMult}, {"glossiness", entry.glossiness},
        {"specularStrength", entry.specularStrength},
    };
}

void storeLoadouts(nlohmann::json& config, const core::TattooLoadoutList& loadouts) {
    auto entries = nlohmann::json::array();
    for (const auto& loadout : loadouts) {
        auto tattoos = nlohmann::json::array();
        for (const auto& tattoo : loadout.entries) tattoos.push_back(serializeTattoo(tattoo));
        entries.push_back({{"name", loadout.name}, {"tattoos", std::move(tattoos)}});
    }
    auto& stored = config["tattooLoadouts"];
    if (!stored.is_object()) stored = nlohmann::json::object();
    stored["version"] = kTattooLoadoutsVersion;
    stored["entries"] = std::move(entries);
}

auto findLoadout(core::TattooLoadoutList& loadouts, const std::string_view name) {
    const auto folded = foldName(trimName(std::string(name)));
    return std::ranges::find_if(loadouts, [&](const auto& loadout) {
        return foldName(loadout.name) == folded;
    });
}

}  // namespace

TattooLoadoutStore::TattooLoadoutStore(std::shared_ptr<PluginConfigFile> file) :
    m_file(std::move(file)) {}

TattooLoadoutResult TattooLoadoutStore::load() {
    if (!m_file) return std::unexpected(error("Tattoo loadout configuration is unavailable."));
    const auto config = m_file->read();
    if (!config) return std::unexpected(config.error());
    return readLoadouts(*config);
}

TattooLoadoutResult TattooLoadoutStore::create(core::TattooLoadout loadout) {
    if (!m_file) return std::unexpected(error("Tattoo loadout configuration is unavailable."));
    if (auto invalid = validateLoadout(loadout)) return std::unexpected(std::move(*invalid));
    core::TattooLoadoutList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto loadouts = readLoadouts(config);
        if (!loadouts) return std::unexpected(loadouts.error());
        if (findLoadout(*loadouts, loadout.name) != loadouts->end()) {
            return std::unexpected(error("Tattoo loadout name already exists."));
        }
        if (loadouts->size() >= core::kTattooLoadoutLimit) {
            return std::unexpected(error("Tattoo loadout limit reached."));
        }
        loadouts->push_back(loadout);
        storeLoadouts(config, *loadouts);
        committed = std::move(*loadouts);
        return {};
    });
    return updated ? TattooLoadoutResult(std::move(committed)) :
        TattooLoadoutResult(std::unexpected(updated.error()));
}

TattooLoadoutResult TattooLoadoutStore::overwrite(core::TattooLoadout loadout) {
    if (!m_file) return std::unexpected(error("Tattoo loadout configuration is unavailable."));
    if (auto invalid = validateLoadout(loadout)) return std::unexpected(std::move(*invalid));
    core::TattooLoadoutList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto loadouts = readLoadouts(config);
        if (!loadouts) return std::unexpected(loadouts.error());
        const auto found = findLoadout(*loadouts, loadout.name);
        if (found == loadouts->end()) {
            return std::unexpected(error("Tattoo loadout to overwrite was not found."));
        }
        *found = loadout;
        storeLoadouts(config, *loadouts);
        committed = std::move(*loadouts);
        return {};
    });
    return updated ? TattooLoadoutResult(std::move(committed)) :
        TattooLoadoutResult(std::unexpected(updated.error()));
}

TattooLoadoutResult TattooLoadoutStore::rename(
    const std::string_view oldName,
    std::string newName) {
    if (!m_file) return std::unexpected(error("Tattoo loadout configuration is unavailable."));
    newName = trimName(std::move(newName));
    if (newName.empty()) return std::unexpected(error("Tattoo loadout name must not be empty."));
    core::TattooLoadoutList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto loadouts = readLoadouts(config);
        if (!loadouts) return std::unexpected(loadouts.error());
        const auto found = findLoadout(*loadouts, oldName);
        if (found == loadouts->end()) {
            return std::unexpected(error("Tattoo loadout to rename was not found."));
        }
        const auto collision = findLoadout(*loadouts, newName);
        if (collision != loadouts->end() && collision != found) {
            return std::unexpected(error("Tattoo loadout name already exists."));
        }
        found->name = std::move(newName);
        storeLoadouts(config, *loadouts);
        committed = std::move(*loadouts);
        return {};
    });
    return updated ? TattooLoadoutResult(std::move(committed)) :
        TattooLoadoutResult(std::unexpected(updated.error()));
}

TattooLoadoutResult TattooLoadoutStore::erase(const std::string_view name) {
    if (!m_file) return std::unexpected(error("Tattoo loadout configuration is unavailable."));
    core::TattooLoadoutList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto loadouts = readLoadouts(config);
        if (!loadouts) return std::unexpected(loadouts.error());
        const auto found = findLoadout(*loadouts, name);
        if (found == loadouts->end()) {
            return std::unexpected(error("Tattoo loadout to delete was not found."));
        }
        loadouts->erase(found);
        storeLoadouts(config, *loadouts);
        committed = std::move(*loadouts);
        return {};
    });
    return updated ? TattooLoadoutResult(std::move(committed)) :
        TattooLoadoutResult(std::unexpected(updated.error()));
}

}  // namespace stui::runtime
