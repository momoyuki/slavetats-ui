#include "runtime/AppearancePresetStore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace stui::runtime {
namespace {

constexpr int kAppearancePresetsVersion = 1;
constexpr std::uint32_t kMaximumColor = 0xFFFFFF;
constexpr float kMaximumEmissiveMult = 10.0F;
constexpr float kMaximumGlossiness = 1000.0F;
constexpr float kMaximumSpecularStrength = 100.0F;
constexpr std::array<std::string_view, 7> kRequiredFields{
    "name", "color", "alpha", "glow", "emissiveMult", "glossiness",
    "specularStrength"};

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
    if (first >= last) {
        return {};
    }
    return {first, last};
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

bool validBoundedFloat(const float value, const float maximum) {
    return std::isfinite(value) && value >= 0.0F && value <= maximum;
}

std::optional<ConfigError> validatePreset(AppearancePreset& preset) {
    preset.name = trimName(std::move(preset.name));
    if (preset.name.empty()) {
        return error("Appearance preset name must not be empty.");
    }
    if (preset.color > kMaximumColor || preset.glow > kMaximumColor) {
        return error("Appearance preset colors must be between 0 and 0xFFFFFF.");
    }
    if (!validBoundedFloat(preset.alpha, 1.0F)) {
        return error("Appearance preset alpha must be between 0 and 1.");
    }
    if (!validBoundedFloat(preset.emissiveMult, kMaximumEmissiveMult)) {
        return error("Appearance preset emission strength must be between 0 and 10.");
    }
    if (!validBoundedFloat(preset.glossiness, kMaximumGlossiness)) {
        return error("Appearance preset glossiness must be between 0 and 1000.");
    }
    if (!validBoundedFloat(preset.specularStrength, kMaximumSpecularStrength)) {
        return error("Appearance preset specular strength must be between 0 and 100.");
    }
    return std::nullopt;
}

AppearancePresetResult parseEntries(const nlohmann::json& entries) {
    if (!entries.is_array()) {
        return std::unexpected(error("Appearance preset entries must be an array."));
    }
    if (entries.size() > kAppearancePresetLimit) {
        return std::unexpected(error("Appearance presets exceed the entry limit."));
    }

    AppearancePresetList presets;
    presets.reserve(entries.size());
    std::vector<std::string> foldedNames;
    foldedNames.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!entry.is_object()) {
            return std::unexpected(error("Each appearance preset entry must be an object."));
        }
        for (const auto field : kRequiredFields) {
            if (!entry.contains(field)) {
                return std::unexpected(error("Appearance preset entry is missing required fields."));
            }
        }
        if (!entry.at("name").is_string() || !entry.at("color").is_number_unsigned() ||
            !entry.at("alpha").is_number() || !entry.at("glow").is_number_unsigned() ||
            !entry.at("emissiveMult").is_number() || !entry.at("glossiness").is_number() ||
            !entry.at("specularStrength").is_number()) {
            return std::unexpected(error("Appearance preset entry fields have invalid types."));
        }

        const auto color = entry.at("color").get<std::uint64_t>();
        const auto glow = entry.at("glow").get<std::uint64_t>();
        if (color > kMaximumColor || glow > kMaximumColor) {
            return std::unexpected(error(
                "Appearance preset colors must be between 0 and 0xFFFFFF."));
        }
        AppearancePreset preset{
            .name = entry.at("name").get<std::string>(),
            .color = static_cast<std::uint32_t>(color),
            .alpha = entry.at("alpha").get<float>(),
            .glow = static_cast<std::uint32_t>(glow),
            .emissiveMult = entry.at("emissiveMult").get<float>(),
            .glossiness = entry.at("glossiness").get<float>(),
            .specularStrength = entry.at("specularStrength").get<float>(),
        };
        if (auto invalid = validatePreset(preset)) {
            return std::unexpected(std::move(*invalid));
        }
        auto folded = foldName(preset.name);
        if (std::ranges::find(foldedNames, folded) != foldedNames.end()) {
            return std::unexpected(error("Appearance preset names must be unique."));
        }
        foldedNames.push_back(std::move(folded));
        presets.push_back(std::move(preset));
    }
    return presets;
}

AppearancePresetResult readPresets(const nlohmann::json& config) {
    const auto iterator = config.find("appearancePresets");
    if (iterator == config.end()) {
        return AppearancePresetList{};
    }
    if (!iterator->is_object()) {
        return std::unexpected(error("Appearance preset configuration must be an object."));
    }
    const auto version = iterator->find("version");
    if (version == iterator->end() || !version->is_number_integer() ||
        version->get<int>() != kAppearancePresetsVersion) {
        return std::unexpected(error("Appearance preset configuration version is unsupported."));
    }
    const auto entries = iterator->find("entries");
    if (entries == iterator->end()) {
        return std::unexpected(error("Appearance preset configuration entries are missing."));
    }
    return parseEntries(*entries);
}

nlohmann::json serializeEntries(const AppearancePresetList& presets) {
    auto entries = nlohmann::json::array();
    for (const auto& preset : presets) {
        entries.push_back({
            {"name", preset.name},
            {"color", preset.color},
            {"alpha", preset.alpha},
            {"glow", preset.glow},
            {"emissiveMult", preset.emissiveMult},
            {"glossiness", preset.glossiness},
            {"specularStrength", preset.specularStrength},
        });
    }
    return entries;
}

void storePresets(nlohmann::json& config, const AppearancePresetList& presets) {
    auto& stored = config["appearancePresets"];
    if (!stored.is_object()) {
        stored = nlohmann::json::object();
    }
    stored["version"] = kAppearancePresetsVersion;
    stored["entries"] = serializeEntries(presets);
}

auto findPreset(AppearancePresetList& presets, const std::string_view name) {
    const auto folded = foldName(trimName(std::string(name)));
    return std::ranges::find_if(presets, [&](const AppearancePreset& preset) {
        return foldName(preset.name) == folded;
    });
}

}  // namespace

AppearancePresetStore::AppearancePresetStore(std::shared_ptr<PluginConfigFile> file) :
    m_file(std::move(file)) {}

AppearancePresetResult AppearancePresetStore::load() {
    if (!m_file) {
        return std::unexpected(error("Appearance preset configuration is unavailable."));
    }
    const auto config = m_file->read();
    if (!config) {
        return std::unexpected(config.error());
    }
    return readPresets(*config);
}

AppearancePresetResult AppearancePresetStore::create(AppearancePreset preset) {
    if (!m_file) {
        return std::unexpected(error("Appearance preset configuration is unavailable."));
    }
    if (auto invalid = validatePreset(preset)) {
        return std::unexpected(std::move(*invalid));
    }

    AppearancePresetList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto presets = readPresets(config);
        if (!presets) {
            return std::unexpected(presets.error());
        }
        if (findPreset(*presets, preset.name) != presets->end()) {
            return std::unexpected(error("Appearance preset name already exists."));
        }
        if (presets->size() >= kAppearancePresetLimit) {
            return std::unexpected(error("Appearance preset limit reached."));
        }
        presets->push_back(preset);
        storePresets(config, *presets);
        committed = std::move(*presets);
        return {};
    });
    if (!updated) {
        return std::unexpected(updated.error());
    }
    return committed;
}

AppearancePresetResult AppearancePresetStore::overwrite(AppearancePreset preset) {
    if (!m_file) {
        return std::unexpected(error("Appearance preset configuration is unavailable."));
    }
    if (auto invalid = validatePreset(preset)) {
        return std::unexpected(std::move(*invalid));
    }

    AppearancePresetList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto presets = readPresets(config);
        if (!presets) {
            return std::unexpected(presets.error());
        }
        const auto found = findPreset(*presets, preset.name);
        if (found == presets->end()) {
            return std::unexpected(error("Appearance preset to overwrite was not found."));
        }
        *found = preset;
        storePresets(config, *presets);
        committed = std::move(*presets);
        return {};
    });
    if (!updated) {
        return std::unexpected(updated.error());
    }
    return committed;
}

AppearancePresetResult AppearancePresetStore::rename(
    const std::string_view oldName,
    std::string newName) {
    if (!m_file) {
        return std::unexpected(error("Appearance preset configuration is unavailable."));
    }
    newName = trimName(std::move(newName));
    if (newName.empty()) {
        return std::unexpected(error("Appearance preset name must not be empty."));
    }

    AppearancePresetList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto presets = readPresets(config);
        if (!presets) {
            return std::unexpected(presets.error());
        }
        const auto found = findPreset(*presets, oldName);
        if (found == presets->end()) {
            return std::unexpected(error("Appearance preset to rename was not found."));
        }
        const auto collision = findPreset(*presets, newName);
        if (collision != presets->end() && collision != found) {
            return std::unexpected(error("Appearance preset name already exists."));
        }
        found->name = std::move(newName);
        storePresets(config, *presets);
        committed = std::move(*presets);
        return {};
    });
    if (!updated) {
        return std::unexpected(updated.error());
    }
    return committed;
}

AppearancePresetResult AppearancePresetStore::erase(const std::string_view name) {
    if (!m_file) {
        return std::unexpected(error("Appearance preset configuration is unavailable."));
    }

    AppearancePresetList committed;
    const auto updated = m_file->update([&](nlohmann::json& config) -> ConfigUpdateResult {
        auto presets = readPresets(config);
        if (!presets) {
            return std::unexpected(presets.error());
        }
        const auto found = findPreset(*presets, name);
        if (found == presets->end()) {
            return std::unexpected(error("Appearance preset to delete was not found."));
        }
        presets->erase(found);
        storePresets(config, *presets);
        committed = std::move(*presets);
        return {};
    });
    if (!updated) {
        return std::unexpected(updated.error());
    }
    return committed;
}

}  // namespace stui::runtime
