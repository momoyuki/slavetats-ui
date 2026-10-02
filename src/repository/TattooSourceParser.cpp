#include "repository/TattooSourceParser.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string_view>
#include <utility>

namespace stui::repository {
namespace {

using Json = nlohmann::json;

bool readRequiredString(
    const Json& entry,
    std::string_view field,
    std::string& value,
    std::string& error) {
    const auto item = entry.find(field);
    if (item == entry.end() || !item->is_string()) {
        error = "field '" + std::string(field) + "' must be a string";
        return false;
    }

    value = item->get<std::string>();
    return true;
}

bool readOptionalStringField(
    const Json& entry,
    std::string_view field,
    std::optional<std::string>& value,
    std::string& error) {
    const auto item = entry.find(field);
    if (item == entry.end()) return true;
    if (!item->is_string()) {
        error = "field '" + std::string(field) + "' must be a string";
        return false;
    }

    value = item->get<std::string>();
    return true;
}

bool readDomain(const Json& entry, TattooDefinition& definition, std::string& error) {
    const auto item = entry.find("domain");
    if (item == entry.end()) return true;
    if (!item->is_string()) {
        error = "field 'domain' must be a string";
        return false;
    }

    const std::string value = item->get<std::string>();
    if (!value.empty()) {
        definition.domain = value;
    }
    return true;
}

bool readOptionalMaterialFloatField(
    const Json& entry,
    std::string_view field,
    std::optional<float>& value,
    std::string& error) {
    const auto item = entry.find(field);
    if (item == entry.end()) return true;
    if (!item->is_number()) {
        error = "field '" + std::string(field) + "' must be a finite non-negative number";
        return false;
    }

    const float parsedValue = item->get<float>();
    if (!std::isfinite(parsedValue) || parsedValue < 0.0F) {
        error = "field '" + std::string(field) + "' must be a finite non-negative number";
        return false;
    }

    value = parsedValue;
    return true;
}

bool readOptionalFields(const Json& entry, TattooDefinition& definition, std::string& error) {
    if (const auto glow = entry.find("glow"); glow != entry.end()) {
        if (!glow->is_number_integer()) {
            error = "field 'glow' must be an integer";
            return false;
        }

        const auto value = glow->get<std::int64_t>();
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max()) {
            error = "field 'glow' is outside the supported range";
            return false;
        }
        definition.glow = static_cast<std::int32_t>(value);
    }

    if (const auto inBsa = entry.find("in_bsa"); inBsa != entry.end()) {
        if (inBsa->is_boolean()) {
            definition.inBsa = inBsa->get<bool>();
        } else if (inBsa->is_number_integer()) {
            definition.inBsa = inBsa->get<std::int64_t>() != 0;
        } else {
            error = "field 'in_bsa' must be a boolean or integer";
            return false;
        }
    }

    if (const auto credit = entry.find("credit"); credit != entry.end()) {
        if (!credit->is_string()) {
            error = "field 'credit' must be a string";
            return false;
        }
        definition.credit = credit->get<std::string>();
    }

    if (!readOptionalStringField(entry, "glowTexture", definition.glowTexture, error) ||
        !readOptionalMaterialFloatField(entry, "emissiveMult", definition.emissiveMult, error) ||
        !readOptionalMaterialFloatField(entry, "glossiness", definition.glossiness, error) ||
        !readOptionalMaterialFloatField(entry, "specularStrength", definition.specularStrength, error) ||
        !readOptionalStringField(entry, "bump", definition.bump, error)) {
        return false;
    }

    return true;
}

}  // namespace

TattooSourceParseReport parseTattooSource(const TattooSourceFile& source) {
    TattooSourceParseReport report;
    std::ifstream stream(source.effectivePath);
    if (!stream) {
        report.issues.push_back({.message = "could not open source file"});
        return report;
    }

    const auto root = Json::parse(stream, nullptr, false);
    if (root.is_discarded()) {
        report.issues.push_back({.message = "invalid JSON"});
        return report;
    }
    if (!root.is_array()) {
        report.issues.push_back({.message = "root must be an array"});
        return report;
    }

    for (std::size_t index = 0; index < root.size(); ++index) {
        const auto& entry = root[index];
        if (!entry.is_object()) {
            report.issues.push_back({.entryIndex = index, .message = "entry must be an object"});
            continue;
        }

        TattooDefinition definition{
            .sourceId = source.sourceId,
            .sourceFile = source.sourceFile,
            .packName = source.packName,
            .sourceIndex = index,
        };
        std::string error;
        if (!readRequiredString(entry, "name", definition.name, error) ||
            !readRequiredString(entry, "section", definition.section, error) ||
            !readRequiredString(entry, "texture", definition.texturePath, error) ||
            !readRequiredString(entry, "area", definition.area, error) ||
            !readDomain(entry, definition, error) ||
            !readOptionalFields(entry, definition, error)) {
            report.issues.push_back({.entryIndex = index, .message = std::move(error)});
            continue;
        }

        report.definitions.push_back(std::move(definition));
    }

    return report;
}

}  // namespace stui::repository
