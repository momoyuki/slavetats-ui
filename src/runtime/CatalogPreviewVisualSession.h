#pragma once

#include "core/TattooModels.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace stui::runtime {

struct CatalogPreviewVisualContext {
    std::uint32_t actorFormId{};
    std::uint64_t generation{};
    core::TattooArea area{core::TattooArea::body};
    std::int32_t slot{-1};

    bool operator==(const CatalogPreviewVisualContext&) const = default;
};

struct CatalogPreviewVisualTarget {
    std::uintptr_t actor{};
    std::uintptr_t root{};
    std::uintptr_t geometry{};
    std::uintptr_t material{};
    std::string nodeName;

    bool operator==(const CatalogPreviewVisualTarget&) const = default;
};

struct CatalogPreviewVisualSnapshot {
    std::string diffuse;
    std::string normal;
    std::string glowTexture;
    std::int32_t tint{};
    float alpha{1.0F};
    std::int32_t glow{};
    float emissiveMult{1.0F};
    float glossiness{};
    float specularStrength{};

    bool operator==(const CatalogPreviewVisualSnapshot&) const = default;
};

struct CatalogPreviewVisualBindings {
    std::function<std::optional<CatalogPreviewVisualTarget>(
        const CatalogPreviewVisualContext&)> resolve;
    std::function<std::optional<CatalogPreviewVisualSnapshot>(
        const CatalogPreviewVisualTarget&)> capture;
    std::function<bool(
        const CatalogPreviewVisualTarget&,
        const CatalogPreviewVisualSnapshot&)> write;
};

class CatalogPreviewVisualSession {
public:
    explicit CatalogPreviewVisualSession(CatalogPreviewVisualBindings bindings);

    [[nodiscard]] bool begin(const CatalogPreviewVisualContext& context);
    [[nodiscard]] bool paint(
        const CatalogPreviewVisualContext& context,
        const core::TattooEntry& candidate);
    [[nodiscard]] bool restore(const CatalogPreviewVisualContext& context);
    [[nodiscard]] bool clear(const CatalogPreviewVisualContext& context);
    [[nodiscard]] bool targetStillStable(const CatalogPreviewVisualContext& context) const;
    [[nodiscard]] bool restorePending() const noexcept;
    void invalidate() noexcept;

private:
    [[nodiscard]] std::optional<CatalogPreviewVisualTarget> resolveStableTarget(
        const CatalogPreviewVisualContext& context) const;
    [[nodiscard]] bool writeAndVerify(const CatalogPreviewVisualSnapshot& value);

    CatalogPreviewVisualBindings m_bindings;
    std::optional<CatalogPreviewVisualContext> m_context;
    std::optional<CatalogPreviewVisualTarget> m_target;
    std::optional<CatalogPreviewVisualSnapshot> m_original;
    bool m_restorePending{};
};

}  // namespace stui::runtime
