#include "runtime/SkeeNodePropertyAccess.h"

#include <utility>

namespace stui::runtime {
namespace {

constexpr std::uint16_t kEmissiveColor = 0;
constexpr std::uint16_t kEmissiveMultiplier = 1;
constexpr std::uint16_t kGlossiness = 2;
constexpr std::uint16_t kSpecularStrength = 3;
constexpr std::uint16_t kTint = 7;
constexpr std::uint16_t kAlpha = 8;
constexpr std::uint16_t kTexture = 9;
constexpr std::uint8_t kScalar = 0xFF;
constexpr std::uint8_t kDiffuseTexture = 0;
constexpr std::uint8_t kNormalTexture = 1;
constexpr std::uint8_t kGlowTexture = 2;

}  // namespace

SkeeNodePropertyAccess::SkeeNodePropertyAccess(SkeeNodePropertyBindings bindings) :
    m_bindings(std::move(bindings)) {}

std::optional<CatalogPreviewVisualSnapshot> SkeeNodePropertyAccess::capture(
    const CatalogPreviewVisualTarget& target) const {
    if (!m_bindings.getInt || !m_bindings.getFloat || !m_bindings.getString) {
        return std::nullopt;
    }

    const auto diffuse = m_bindings.getString(target, kTexture, kDiffuseTexture);
    const auto normal = m_bindings.getString(target, kTexture, kNormalTexture);
    const auto glowTexture = m_bindings.getString(target, kTexture, kGlowTexture);
    const auto tint = m_bindings.getInt(target, kTint, kScalar);
    const auto alpha = m_bindings.getFloat(target, kAlpha, kScalar);
    const auto glow = m_bindings.getInt(target, kEmissiveColor, kScalar);
    const auto emissiveMult = m_bindings.getFloat(target, kEmissiveMultiplier, kScalar);
    const auto glossiness = m_bindings.getFloat(target, kGlossiness, kScalar);
    const auto specularStrength = m_bindings.getFloat(target, kSpecularStrength, kScalar);
    if (!diffuse || !normal || !glowTexture || !tint || !alpha || !glow ||
        !emissiveMult || !glossiness || !specularStrength) {
        return std::nullopt;
    }

    return CatalogPreviewVisualSnapshot{
        .diffuse = *diffuse,
        .normal = *normal,
        .glowTexture = *glowTexture,
        .tint = *tint,
        .alpha = *alpha,
        .glow = *glow,
        .emissiveMult = *emissiveMult,
        .glossiness = *glossiness,
        .specularStrength = *specularStrength,
    };
}

bool SkeeNodePropertyAccess::write(
    const CatalogPreviewVisualTarget& target,
    const CatalogPreviewVisualSnapshot& value) const {
    if (!m_bindings.setInt || !m_bindings.setFloat || !m_bindings.setString) {
        return false;
    }
    return m_bindings.setString(target, kTexture, kDiffuseTexture, value.diffuse) &&
        m_bindings.setString(target, kTexture, kNormalTexture, value.normal) &&
        m_bindings.setString(target, kTexture, kGlowTexture, value.glowTexture) &&
        m_bindings.setInt(target, kTint, kScalar, value.tint) &&
        m_bindings.setFloat(target, kAlpha, kScalar, value.alpha) &&
        m_bindings.setInt(target, kEmissiveColor, kScalar, value.glow) &&
        m_bindings.setFloat(target, kEmissiveMultiplier, kScalar, value.emissiveMult) &&
        m_bindings.setFloat(target, kGlossiness, kScalar, value.glossiness) &&
        m_bindings.setFloat(target, kSpecularStrength, kScalar, value.specularStrength);
}

}  // namespace stui::runtime
