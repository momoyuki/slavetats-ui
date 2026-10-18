#include "runtime/CatalogPreviewVisualSession.h"
#include "textures/TextureResolver.h"

#include <utility>

namespace stui::runtime {
namespace {

std::optional<std::string> previewTexturePath(std::string_view path) {
    if (path.empty()) return std::string{};
    const auto normalized = textures::TextureResolver::normalize(path);
    if (!normalized) return std::nullopt;
    return std::string("textures\\actors\\character\\slavetats\\") + *normalized;
}

}  // namespace

CatalogPreviewVisualSession::CatalogPreviewVisualSession(
    CatalogPreviewVisualBindings bindings) :
    m_bindings(std::move(bindings)) {}

bool CatalogPreviewVisualSession::begin(const CatalogPreviewVisualContext& context) {
    if (m_context || !context.actorFormId || context.slot < 0 || !m_bindings.resolve ||
        !m_bindings.capture || !m_bindings.write) {
        return false;
    }

    const auto target = m_bindings.resolve(context);
    if (!target || !target->actor || !target->root || !target->geometry || !target->material) {
        return false;
    }
    const auto original = m_bindings.capture(*target);
    if (!original) {
        return false;
    }

    m_context = context;
    m_target = *target;
    m_original = *original;
    m_restorePending = false;
    return true;
}

std::optional<CatalogPreviewVisualTarget>
CatalogPreviewVisualSession::resolveStableTarget(
    const CatalogPreviewVisualContext& context) const {
    if (!m_context || !m_target || context != *m_context || !m_bindings.resolve) {
        return std::nullopt;
    }
    const auto current = m_bindings.resolve(context);
    if (!current || *current != *m_target) {
        return std::nullopt;
    }
    return current;
}

bool CatalogPreviewVisualSession::writeAndVerify(
    const CatalogPreviewVisualSnapshot& value) {
    if (!m_target || !m_bindings.write || !m_bindings.capture) {
        return false;
    }
    m_restorePending = true;
    if (!m_bindings.write(*m_target, value)) {
        return false;
    }
    const auto readback = m_bindings.capture(*m_target);
    return readback && *readback == value;
}

bool CatalogPreviewVisualSession::paint(
    const CatalogPreviewVisualContext& context,
    const core::TattooEntry& candidate) {
    if (!resolveStableTarget(context)) {
        return false;
    }
    const auto diffuse = previewTexturePath(candidate.texturePath);
    const auto normal = previewTexturePath(candidate.bump);
    const auto glowTexture = previewTexturePath(candidate.glowTexture);
    if (!diffuse || diffuse->empty() || !normal || !glowTexture) return false;
    const CatalogPreviewVisualSnapshot desired{
        .diffuse = *diffuse,
        .normal = *normal,
        .glowTexture = *glowTexture,
        .tint = candidate.color,
        .alpha = candidate.alpha,
        .glow = candidate.glow,
        .emissiveMult = candidate.emissiveMult,
        .glossiness = candidate.glossiness,
        .specularStrength = candidate.specularStrength,
    };
    // A concurrent overlay refresh can replace the visible material while Browse stays open.
    // Polling an unchanged preview should not issue another full SKEE write.
    if (m_restorePending && m_bindings.capture &&
        m_bindings.capture(*m_target) == desired) {
        return true;
    }
    return writeAndVerify(desired);
}

bool CatalogPreviewVisualSession::restore(const CatalogPreviewVisualContext& context) {
    if (!clear(context)) {
        return false;
    }
    m_context.reset();
    m_target.reset();
    m_original.reset();
    return true;
}

bool CatalogPreviewVisualSession::clear(const CatalogPreviewVisualContext& context) {
    if (!m_original || !m_context || context != *m_context) {
        return false;
    }
    if (m_restorePending &&
        (!resolveStableTarget(context) || !writeAndVerify(*m_original))) {
        return false;
    }
    m_restorePending = false;
    return true;
}

bool CatalogPreviewVisualSession::restorePending() const noexcept {
    return m_restorePending;
}

bool CatalogPreviewVisualSession::targetStillStable(
    const CatalogPreviewVisualContext& context) const {
    return resolveStableTarget(context).has_value();
}

void CatalogPreviewVisualSession::invalidate() noexcept {
    m_context.reset();
    m_target.reset();
    m_original.reset();
    m_restorePending = false;
}

}  // namespace stui::runtime
