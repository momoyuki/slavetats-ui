#include "native/CatalogPreviewProbe.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <array>
#include <set>
#include <utility>
namespace stui::native {
void logCatalogPreviewNodeCapabilities(const core::TattooSlots& slots) {
    if (REL::Module::IsVR()) return;
    auto* actor=RE::TESForm::LookupByID<RE::Actor>(slots.actorFormId);
    if (!actor || !actor->Is3DLoaded()) return;
    auto* root=actor->Get3D();
    if (!root) return;
    static std::set<std::pair<std::uint32_t,core::TattooArea>> reported;
    if (!reported.emplace(slots.actorFormId,slots.area).second) return;
    std::array<unsigned,5> counts{};
    unsigned emptyCandidates{},managedCandidates{};
    for (const auto& slot:slots.slots) {
        if (slot.occupancy==core::SlotOccupancy::external ||
            (slot.tattoo && slot.tattoo->locked)) continue;
        const auto name=catalogPreviewNodeName(slots.area,slot.index);
        if (!name) continue;
        auto* object=root->GetObjectByName(RE::BSFixedString(name->c_str()));
        auto* geometry=object ? object->AsGeometry() : nullptr;
        auto* shader=geometry ? geometry->lightingShaderProp_cast() : nullptr;
        auto* material=shader ? shader->material : nullptr;
        const bool lighting=material && material->GetType()==RE::BSShaderMaterial::Type::kLighting;
        const auto feature=lighting ? material->GetFeature() : RE::BSShaderMaterial::Feature::kNone;
        const bool tint=feature==RE::BSShaderMaterial::Feature::kFaceGenRGBTint ||
            feature==RE::BSShaderMaterial::Feature::kHairTint;
        const auto support=catalogPreviewMaterialSupport({
            geometry!=nullptr,lighting,tint,shader && shader->emissiveColor!=nullptr});
        ++counts[static_cast<std::size_t>(support)];
        if (support==CatalogPreviewMaterialSupport::scalarCandidate) {
            if (slot.occupancy==core::SlotOccupancy::empty) ++emptyCandidates;
            else ++managedCandidates;
        }
    }
    SKSE::log::info("[CatalogPreview] node preflight actor={:08X} area={} default-format-only missing={} unsupportedShader={} tintConversion={} missingEmission={} scalarCandidates={} (empty={}, managed={}); no shader changes",
        slots.actorFormId,static_cast<int>(slots.area),counts[0],counts[1],counts[2],counts[3],counts[4],
        emptyCandidates,managedCandidates);
}
} // namespace stui::native
