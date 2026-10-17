#include "native/CatalogPreviewModel.h"

#include <algorithm>
#include <utility>

namespace stui::native {

core::TattooEntry catalogPreviewCandidate(
    const repository::TattooDefinition& definition) {
    return core::TattooEntry{
        .domain = definition.domain,
        .section = definition.section,
        .name = definition.name,
        .texturePath = definition.texturePath,
        .area = definition.area,
        .color = 0x000000,
        .alpha = 1.0F,
        .glow = definition.glow.value_or(0),
        .glossiness = definition.glossiness.value_or(0.0F),
        .specularStrength = definition.specularStrength.value_or(0.0F),
        .bump = definition.bump.value_or(std::string{}),
        .glowTexture = definition.glowTexture.value_or(std::string{}),
        .emissiveMult = definition.emissiveMult.value_or(1.0F),
    };
}

core::CatalogPreviewCommitRequest catalogPreviewCommitRequest(
    const CatalogPreviewCommit& commit, core::CatalogPreviewSessionId sessionId) {
    return {
        .sessionId = sessionId,
        .context = {commit.context.actorFormId, commit.context.generation,
            commit.context.area, commit.context.slot},
        .apply = {
            .actorFormId = commit.context.actorFormId,
            .area = commit.context.area,
            .slot = commit.context.slot,
            .domain = commit.candidate.domain,
            .section = commit.candidate.section,
            .name = commit.candidate.name,
            .color = commit.candidate.color,
            .alpha = commit.candidate.alpha,
            .mode = commit.synchronizeOnly
                ? core::ApplyTattooMode::synchronizeOnly
                : core::ApplyTattooMode::applyAndSynchronize,
            .appearance = core::ApplyTattooAppearanceOverrides{
                .glow = commit.candidate.glow,
                .glossiness = commit.candidate.glossiness,
                .specularStrength = commit.candidate.specularStrength,
                .emissiveMult = commit.candidate.emissiveMult,
                .glowTexture = commit.candidate.glowTexture,
                .bump = commit.candidate.bump,
            },
        },
    };
}

bool CatalogPreviewModel::begin(CatalogPreviewContext context, bool capable) {
    if (m_open || m_dirty || m_inFlight || !capable || !context.actorFormId || context.slot < 0)
        return false;
    switch (context.area) {
    case core::TattooArea::body:
    case core::TattooArea::face:
    case core::TattooArea::hands:
    case core::TattooArea::feet:
        break;
    default:
        return false;
    }
    m_context = context;
    m_open = true;
    m_closing = false;
    m_failed = false;
    m_written = false;
    m_commitFailed = false;
    m_saveIntent.reset();
    m_commitInFlight.reset();
    m_hover.reset();
    m_pin.reset();
    m_desired.reset();
    m_ink.reset();
    m_glow.reset();
    m_glossy.reset();
    m_pending = false;
    ++m_revision;
    m_acknowledged = 0;
    return true;
}

core::TattooEntry CatalogPreviewModel::effective(core::TattooEntry value) const {
    if (m_ink) value.color = *m_ink;
    if (m_glow) value.glow = *m_glow;
    if (m_glossy) {
        value.glossiness = *m_glossy
            ? (value.glossiness > 0 ? value.glossiness : 5.0F) : 0.0F;
        value.specularStrength = *m_glossy
            ? (value.specularStrength > 0 ? value.specularStrength : 5.0F) : 0.0F;
    }
    return value;
}

void CatalogPreviewModel::updateDesired(bool force) {
    const auto& base = m_hover ? m_hover : m_pin;
    auto desired = base ? std::optional(effective(*base)) : std::nullopt;
    if (!force && desired == m_desired) return;
    m_desired = std::move(desired);
    ++m_revision;
    m_failed = false;
    m_pending = m_desired.has_value() || m_dirty || m_inFlight.has_value();
}

void CatalogPreviewModel::hover(std::optional<core::TattooEntry> candidate) {
    if (!m_open || m_closing || m_saveIntent) return;
    m_hover = std::move(candidate);
    updateDesired();
}

bool CatalogPreviewModel::pin() {
    if (!m_open || m_closing || m_saveIntent || !m_hover) return false;
    m_pin = m_hover;
    updateDesired();
    return true;
}

void CatalogPreviewModel::setInk(std::int32_t color) {
    if (!m_open || m_closing || m_saveIntent) return;
    m_ink = std::clamp(color, 0, 0xFFFFFF);
    updateDesired();
}

void CatalogPreviewModel::setGlow(std::int32_t color) {
    if (!m_open || m_closing || m_saveIntent) return;
    m_glow = std::clamp(color, 0, 0xFFFFFF);
    updateDesired();
}

void CatalogPreviewModel::setGlossy(bool enabled) {
    if (!m_open || m_closing || m_saveIntent) return;
    m_glossy = enabled;
    updateDesired();
}

void CatalogPreviewModel::undo() {
    if (!m_open || m_closing || m_commitInFlight || m_written) return;
    m_saveIntent.reset();
    m_commitFailed = false;
    m_hover.reset();
    m_pin.reset();
    m_ink.reset();
    m_glow.reset();
    m_glossy.reset();
    updateDesired(true);
}

void CatalogPreviewModel::close() {
    if (!m_open || m_commitInFlight || m_written) return;
    m_saveIntent.reset();
    m_commitFailed = false;
    m_hover.reset();
    m_pin.reset();
    m_desired.reset();
    m_closing = true;
    ++m_revision;
    m_failed = false;
    // Even an untouched visual session must be ended by the runtime. The
    // resulting empty paint ticket is a close barrier, not a material write.
    m_pending = true;
}

void CatalogPreviewModel::retry() {
    if (m_open && m_commitFailed) {
        m_commitFailed = false;
        return;
    }
    if (!m_open || !m_failed) return;
    m_failed = false;
    m_pending = true;
}

std::optional<CatalogPreviewPaint> CatalogPreviewModel::takePaint() {
    if (!m_open || m_inFlight || !m_pending || m_failed || m_written || m_commitInFlight)
        return std::nullopt;
    m_pending = false;
    m_inFlight = CatalogPreviewPaint{m_context, ++m_operation, m_revision, m_desired};
    // A failed or interrupted setter may have already changed some properties.
    if (m_desired) m_dirty = true;
    return m_inFlight;
}

void CatalogPreviewModel::completePaint(const CatalogPreviewPaint& paint, bool success) {
    if (!m_inFlight || paint.context != m_context || paint.operation != m_inFlight->operation)
        return;
    m_inFlight.reset();
    if (paint.revision != m_revision) {
        m_pending = true;
        return;
    }
    if (!success) {
        m_failed = true;
        return;
    }
    m_acknowledged = m_revision;
    if (!paint.candidate) {
        m_dirty = false;
        if (m_closing) m_open = false;
    }
}

std::optional<core::TattooEntry> CatalogPreviewModel::saveCandidate() const {
    if (!m_open || m_closing || !m_pin || m_inFlight || m_pending || m_failed ||
        m_commitInFlight || m_commitFailed || m_written || m_acknowledged != m_revision)
        return std::nullopt;
    auto selected = effective(*m_pin);
    return m_desired && *m_desired == selected ? std::optional(std::move(selected)) : std::nullopt;
}

std::optional<core::TattooEntry> CatalogPreviewModel::pinnedCandidate() const {
    return m_pin ? std::optional(effective(*m_pin)) : std::nullopt;
}

std::optional<core::TattooEntry> CatalogPreviewModel::displayCandidate() const {
    if (m_hover) return effective(*m_hover);
    return pinnedCandidate();
}

bool CatalogPreviewModel::restorePending() const noexcept {
    return !m_written && m_dirty && (!m_desired || m_closing);
}

bool CatalogPreviewModel::requestSave() {
    if (!m_open || m_closing || !m_pin || m_saveIntent || m_commitInFlight) return false;
    m_saveIntent = effective(*m_pin);
    m_hover.reset();
    updateDesired();
    return true;
}

std::optional<CatalogPreviewCommit> CatalogPreviewModel::takeCommit() {
    if (!m_open || m_closing || !m_saveIntent || m_inFlight || m_pending ||
        m_failed || m_commitFailed || m_commitInFlight ||
        m_acknowledged != m_revision || m_desired != m_saveIntent)
        return std::nullopt;
    m_commitInFlight = CatalogPreviewCommit{m_context, ++m_operation, *m_saveIntent, m_written};
    return m_commitInFlight;
}

void CatalogPreviewModel::completeCommit(
    const CatalogPreviewCommit& commit, bool success, bool mutationWritten) {
    if (!m_commitInFlight || commit.context != m_context ||
        commit.operation != m_commitInFlight->operation)
        return;
    m_commitInFlight.reset();
    if (!success) {
        m_written = m_written || mutationWritten;
        m_commitFailed = true;
        return;
    }
    // Persistent state is now authoritative; no stale hover/Close may restore it.
    m_dirty = false;
    m_open = false;
    m_pending = false;
    m_written = false;
    m_saveIntent.reset();
    m_hover.reset();
    m_pin.reset();
    m_desired.reset();
}

}  // namespace stui::native
