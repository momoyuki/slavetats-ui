#pragma once

#include "runtime/CatalogPreviewVisualSession.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace stui::runtime {

struct SkeeNodePropertyBindings {
    std::function<std::optional<std::int32_t>(
        const CatalogPreviewVisualTarget&, std::uint16_t, std::uint8_t)> getInt;
    std::function<std::optional<float>(
        const CatalogPreviewVisualTarget&, std::uint16_t, std::uint8_t)> getFloat;
    std::function<std::optional<std::string>(
        const CatalogPreviewVisualTarget&, std::uint16_t, std::uint8_t)> getString;
    std::function<bool(
        const CatalogPreviewVisualTarget&, std::uint16_t, std::uint8_t, std::int32_t)> setInt;
    std::function<bool(
        const CatalogPreviewVisualTarget&, std::uint16_t, std::uint8_t, float)> setFloat;
    std::function<bool(
        const CatalogPreviewVisualTarget&, std::uint16_t, std::uint8_t,
        const std::string&)> setString;
};

class SkeeNodePropertyAccess {
public:
    explicit SkeeNodePropertyAccess(SkeeNodePropertyBindings bindings);

    [[nodiscard]] std::optional<CatalogPreviewVisualSnapshot> capture(
        const CatalogPreviewVisualTarget& target) const;
    [[nodiscard]] bool write(
        const CatalogPreviewVisualTarget& target,
        const CatalogPreviewVisualSnapshot& value) const;

private:
    SkeeNodePropertyBindings m_bindings;
};

}  // namespace stui::runtime
