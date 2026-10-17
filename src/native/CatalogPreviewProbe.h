#pragma once
#include "core/TattooModels.h"
#include <optional>
#include <string>
namespace stui::native {
enum class CatalogPreviewMaterialSupport {
    missingGeometry, unsupportedShader, tintConversionRequired,
    missingEmissionStorage, scalarCandidate
};
struct CatalogPreviewMaterialFacts {
    bool geometry{}, lightingMaterial{}, compatibleTint{}, emissionStorage{};
};
inline CatalogPreviewMaterialSupport catalogPreviewMaterialSupport(
    CatalogPreviewMaterialFacts facts) noexcept {
    if (!facts.geometry) return CatalogPreviewMaterialSupport::missingGeometry;
    if (!facts.lightingMaterial) return CatalogPreviewMaterialSupport::unsupportedShader;
    if (!facts.compatibleTint) return CatalogPreviewMaterialSupport::tintConversionRequired;
    if (!facts.emissionStorage) return CatalogPreviewMaterialSupport::missingEmissionStorage;
    return CatalogPreviewMaterialSupport::scalarCandidate;
}
inline std::optional<std::string> catalogPreviewNodeName(core::TattooArea area,int slot) {
    if (slot < 0) return std::nullopt;
    const char* prefix{};
    switch (area) {
    case core::TattooArea::body: prefix="Body"; break;
    case core::TattooArea::face: prefix="Face"; break;
    case core::TattooArea::hands: prefix="Hands"; break;
    case core::TattooArea::feet: prefix="Feet"; break;
    default: return std::nullopt;
    }
    return std::string(prefix)+" [Ovl"+std::to_string(slot)+"]";
}
// Scheduled game-thread diagnostics only. Does not call SKEE property setters/getters.
void logCatalogPreviewNodeCapabilities(const core::TattooSlots& slots);
} // namespace stui::native
