#include "runtime/SkyrimCatalogPreviewBindings.h"
#include "textures/TextureResolver.h"

#include "native/CatalogPreviewProbe.h"
#include "runtime/SkeeNodePropertyAccess.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <Windows.h>

#include <optional>
#include <string>

namespace stui::runtime {
namespace {

class IntGetter final : public skee::GetVariant {
public:
    void Int(std::int32_t value) override { result = value; }
    void Float(float) override {}
    void String(const char*) override {}
    void Bool(bool) override {}
    void TextureSet(const void*) override {}
    std::optional<std::int32_t> result;
};
class FloatGetter final : public skee::GetVariant {
public:
    void Int(std::int32_t) override {}
    void Float(float value) override { result = value; }
    void String(const char*) override {}
    void Bool(bool) override {}
    void TextureSet(const void*) override {}
    std::optional<float> result;
};
class StringGetter final : public skee::GetVariant {
public:
    void Int(std::int32_t) override {}
    void Float(float) override {}
    void String(const char* value) override { result = value ? value : ""; }
    void Bool(bool) override {}
    void TextureSet(const void*) override {}
    std::optional<std::string> result;
};
class IntSetter final : public skee::SetVariant {
public:
    explicit IntSetter(std::int32_t value) : m_value(value) {}
    Type GetType() override { return Type::Int; }
    std::int32_t Int() override { return m_value; }
private:
    std::int32_t m_value;
};
class FloatSetter final : public skee::SetVariant {
public:
    explicit FloatSetter(float value) : m_value(value) {}
    Type GetType() override { return Type::Float; }
    float Float() override { return m_value; }
private:
    float m_value;
};
class StringSetter final : public skee::SetVariant {
public:
    explicit StringSetter(const std::string& value) : m_value(value) {}
    Type GetType() override { return Type::String; }
    const char* String() override { return m_value.c_str(); }
private:
    std::string m_value;
};

SkeeNodePropertyBindings propertyBindings(skee::OverrideInterface* interface) {
    return {
        .getInt = [interface](const CatalogPreviewVisualTarget& target,
                      std::uint16_t key, std::uint8_t index) -> std::optional<std::int32_t> {
            IntGetter visitor;
            if (!interface->GetNodeProperty(reinterpret_cast<void*>(target.actor), false,
                    target.nodeName.c_str(), key, index, visitor)) return std::nullopt;
            return visitor.result;
        },
        .getFloat = [interface](const CatalogPreviewVisualTarget& target,
                        std::uint16_t key, std::uint8_t index) -> std::optional<float> {
            FloatGetter visitor;
            if (!interface->GetNodeProperty(reinterpret_cast<void*>(target.actor), false,
                    target.nodeName.c_str(), key, index, visitor)) return std::nullopt;
            return visitor.result;
        },
        .getString = [interface](const CatalogPreviewVisualTarget& target,
                         std::uint16_t key, std::uint8_t index) -> std::optional<std::string> {
            StringGetter visitor;
            if (!interface->GetNodeProperty(reinterpret_cast<void*>(target.actor), false,
                    target.nodeName.c_str(), key, index, visitor)) return std::nullopt;
            return visitor.result;
        },
        .setInt = [interface](const CatalogPreviewVisualTarget& target,
                      std::uint16_t key, std::uint8_t index, std::int32_t value) {
            IntSetter setter(value);
            interface->SetNodeProperty(reinterpret_cast<void*>(target.actor), false,
                target.nodeName.c_str(), key, index, setter, true);
            return true;
        },
        .setFloat = [interface](const CatalogPreviewVisualTarget& target,
                        std::uint16_t key, std::uint8_t index, float value) {
            FloatSetter setter(value);
            interface->SetNodeProperty(reinterpret_cast<void*>(target.actor), false,
                target.nodeName.c_str(), key, index, setter, true);
            return true;
        },
        .setString = [interface](const CatalogPreviewVisualTarget& target,
                         std::uint16_t key, std::uint8_t index, const std::string& value) {
            StringSetter setter(value);
            interface->SetNodeProperty(reinterpret_cast<void*>(target.actor), false,
                target.nodeName.c_str(), key, index, setter, true);
            return true;
        },
    };
}

}  // namespace

CatalogPreviewVisualBindings makeSkyrimCatalogPreviewBindings(
    skee::OverrideInterface* interface) {
    if (!interface) return {};
    return {
        .resolve = [](const CatalogPreviewVisualContext& context)
                -> std::optional<CatalogPreviewVisualTarget> {
            auto* actor=RE::TESForm::LookupByID<RE::Actor>(context.actorFormId);
            if (!actor || !actor->Is3DLoaded()) return std::nullopt;
            auto* root=actor->Get3D();
            const auto nodeName=native::catalogPreviewNodeName(context.area,context.slot);
            if (!root || !nodeName) return std::nullopt;
            auto* object=root->GetObjectByName(RE::BSFixedString(nodeName->c_str()));
            auto* geometry=object ? object->AsGeometry() : nullptr;
            auto* shader=geometry ? geometry->lightingShaderProp_cast() : nullptr;
            auto* material=shader ? shader->material : nullptr;
            if (!material || material->GetType()!=RE::BSShaderMaterial::Type::kLighting ||
                !shader->emissiveColor) return std::nullopt;
            const auto feature=material->GetFeature();
            if (feature!=RE::BSShaderMaterial::Feature::kFaceGenRGBTint &&
                feature!=RE::BSShaderMaterial::Feature::kHairTint) return std::nullopt;
            return CatalogPreviewVisualTarget{
                .actor=reinterpret_cast<std::uintptr_t>(actor),
                .root=reinterpret_cast<std::uintptr_t>(root),
                .geometry=reinterpret_cast<std::uintptr_t>(geometry),
                .material=reinterpret_cast<std::uintptr_t>(material),
                .nodeName=*nodeName,
            };
        },
        .capture=[access=SkeeNodePropertyAccess(propertyBindings(interface))](
                     const CatalogPreviewVisualTarget& target) {
            return access.capture(target);
        },
        .write=[access=SkeeNodePropertyAccess(propertyBindings(interface))](
                   const CatalogPreviewVisualTarget& target,
                   const CatalogPreviewVisualSnapshot& value) {
            return access.write(target,value);
        },
    };
}

// Called only by the scheduled Current Slots query. No scene pointers escape.
void collectExternalTextures(core::TattooSlots& slots, skee::OverrideInterface* interface) {
    for (auto& slot : slots.slots) slot.externalThumbnail.reset();
    if (REL::Module::IsVR()) return;
    auto* actor = RE::TESForm::LookupByID<RE::Actor>(slots.actorFormId);
    if (!actor || !actor->Is3DLoaded()) return;
    auto* root = actor->Get3D();
    if (!root) return;
    for (const auto& target : native::externalProbeTargets(slots)) {
        std::optional<std::string> property, registered, materialPath;
        auto* object = root->GetObjectByName(RE::BSFixedString(target.nodeName.c_str()));
        auto* geometry = object ? object->AsGeometry() : nullptr;
        auto* shader = geometry ? geometry->lightingShaderProp_cast() : nullptr;
        auto* base = shader ? shader->material : nullptr;
        const bool lighting = base && base->GetType() == RE::BSShaderMaterial::Type::kLighting;
        bool busy = false;
        if (lighting) {
            auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(base);
            // Engine OnLoadTextureSet uses a 0/1 interlocked lock at this member.
            // Try once: never block the game thread waiting for a texture load.
            auto* lock = reinterpret_cast<volatile LONG*>(&material->textureSetLock);
            if (InterlockedCompareExchange(lock, 1, 0) == 0) {
                try {
                    const auto textures = material->GetTextureSet();
                    const char* path = textures ? textures->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : nullptr;
                    if (path && *path) materialPath = std::string(path);
                } catch (...) {
                    InterlockedExchange(lock, 0);
                    throw;
                }
                InterlockedExchange(lock, 0);
            } else busy = true;
        }
        if (interface && geometry && !busy) {
            StringGetter value;
            if (interface->GetNodeProperty(actor, false, target.nodeName.c_str(), 9, 0, value)) property = value.result;
            auto* npc = actor->GetActorBase();
            if (npc) {
                StringGetter overrideValue;
                if (interface->GetNodeOverride(actor, npc->GetSex() == RE::SEXES::kFemale,
                        target.nodeName.c_str(), 9, 0, overrideValue)) registered = overrideValue.result;
            }
        }
        if (property && materialPath) {
            auto resource = textures::TextureResolver::matchingExternal(*property, *materialPath);
            if (resource) {
                for (auto& slot : slots.slots) {
                    if (slot.index == target.slotIndex && slot.occupancy == core::SlotOccupancy::external) {
                        slot.externalThumbnail = std::move(*resource);
                        break;
                    }
                }
            }
        }
        SKSE::log::debug("[ExternalTextureProbe] actor={:08X} node='{}' geometry={} lighting={} busy={} property='{}' override='{}' material='{}' comparison={} (case/separator comparison; source image only)",
            target.actorFormId, target.nodeName, geometry != nullptr, lighting, busy,
            property.value_or("<unavailable>"), registered.value_or("<unavailable>"),
            materialPath.value_or("<unavailable>"), static_cast<int>(native::compareExternalPaths(property, materialPath)));
    }
}
}  // namespace stui::runtime
