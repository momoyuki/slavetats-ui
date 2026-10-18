#include "runtime/CatalogPreviewRuntimeSession.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace stui::runtime {
namespace {

core::ServiceError previewError(core::ServiceErrorCode code, const char* message) {
    return core::ServiceError{code, message};
}

}  // namespace

CatalogPreviewRuntimeSession::CatalogPreviewRuntimeSession(
    CatalogPreviewSlotQuery query,
    CatalogPreviewVisualBindings visualBindings,
    CatalogPreviewApply apply) :
    m_query(std::move(query)),
    m_visualBindings(std::move(visualBindings)),
    m_apply(std::move(apply)) {}

CatalogPreviewVisualContext CatalogPreviewRuntimeSession::visualContext(
    const core::CatalogPreviewContext& context) noexcept {
    return {context.actorFormId, context.generation, context.area, context.slot};
}

std::expected<CatalogPreviewRuntimeSession::SlotFingerprint, core::ServiceError>
CatalogPreviewRuntimeSession::validateSlot(
    const core::CatalogPreviewContext& context) const {
    if (!m_query) return std::unexpected(previewError(
        core::ServiceErrorCode::previewUnavailable, "Catalog preview slot query is unavailable"));
    auto slots=m_query(context.actorFormId,context.area);
    if (!slots) return std::unexpected(slots.error());
    if (slots->actorFormId!=context.actorFormId || slots->area!=context.area) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
            "Catalog preview Actor or area changed"));
    }
    const auto found=std::ranges::find_if(slots->slots,[&](const core::TattooSlot& slot) {
        return slot.index==context.slot;
    });
    if (found==slots->slots.end()) return std::unexpected(previewError(
        core::ServiceErrorCode::invalidSlot,"Catalog preview slot is unavailable"));
    if (found->occupancy==core::SlotOccupancy::external) return std::unexpected(previewError(
        core::ServiceErrorCode::externalSlot,"External slots cannot be previewed"));
    if (found->occupancy!=core::SlotOccupancy::empty &&
        found->occupancy!=core::SlotOccupancy::slaveTats) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
            "Catalog preview slot occupancy is unknown"));
    }
    if ((found->occupancy==core::SlotOccupancy::slaveTats &&
            (!found->tattoo || !found->tattoo->runtimeHandle)) ||
        (found->occupancy==core::SlotOccupancy::empty && found->tattoo)) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
            "Catalog preview slot snapshot is inconsistent"));
    }
    if (found->tattoo && found->tattoo->locked) return std::unexpected(previewError(
        core::ServiceErrorCode::previewConflict,"Locked slots cannot be previewed"));
    return SlotFingerprint{
        .occupancy=found->occupancy,
        .tattoo=found->tattoo,
    };
}

bool CatalogPreviewRuntimeSession::matchesActive(
    core::CatalogPreviewSessionId sessionId,
    const core::CatalogPreviewContext& context) const noexcept {
    return sessionId && sessionId==m_sessionId && m_context && *m_context==context;
}

core::CatalogPreviewBeginResult CatalogPreviewRuntimeSession::begin(
    const core::CatalogPreviewBeginRequest& request) {
    if (m_context || m_synchronizationRetry) return std::unexpected(previewError(
        core::ServiceErrorCode::previewConflict,"A catalog preview session is already active"));
    auto slot=validateSlot(request);
    if (!slot) return std::unexpected(slot.error());
    auto visual=std::make_unique<CatalogPreviewVisualSession>(m_visualBindings);
    if (!visual->begin(visualContext(request))) return std::unexpected(previewError(
        core::ServiceErrorCode::previewUnavailable,
        "The selected slot does not support nonpersistent preview"));
    m_context=request;
    m_originalSlot=*slot;
    m_visual=std::move(visual);
    m_sessionId=++m_nextSessionId;
    if (!m_sessionId) m_sessionId=++m_nextSessionId;
    return core::CatalogPreviewBeginSuccess{m_sessionId};
}

core::CatalogPreviewPaintResult CatalogPreviewRuntimeSession::paint(
    const core::CatalogPreviewPaintRequest& request) {
    if (!matchesActive(request.sessionId,request.context) || !m_visual || !m_originalSlot) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
            "Catalog preview session is stale"));
    }
    auto slot=validateSlot(request.context);
    if (!slot || *slot!=*m_originalSlot) return std::unexpected(previewError(
        core::ServiceErrorCode::previewConflict,"The selected slot changed during preview"));
    const bool painted = request.candidate
        ? m_visual->paint(visualContext(request.context), *request.candidate)
        : m_visual->clear(visualContext(request.context));
    if (!painted) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewFailed,
            "Catalog preview paint failed"));
    }
    return core::CatalogPreviewPaintSuccess{};
}

core::CatalogPreviewRestoreResult CatalogPreviewRuntimeSession::restore(
    const core::CatalogPreviewRestoreRequest& request) {
    if (!matchesActive(request.sessionId,request.context) || !m_visual || !m_originalSlot) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
            "Catalog preview session is stale"));
    }
    if (m_visual->restorePending()) {
        auto slot=validateSlot(request.context);
        if (!slot || *slot!=*m_originalSlot) return std::unexpected(previewError(
            core::ServiceErrorCode::previewConflict,"The selected slot changed during preview"));
    }
    if (!m_visual->restore(visualContext(request.context))) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewFailed,
            "Catalog preview restore failed"));
    }
    invalidate();
    return core::CatalogPreviewRestoreSuccess{};
}

core::CatalogPreviewCommitResult CatalogPreviewRuntimeSession::commit(
    const core::CatalogPreviewCommitRequest& request) {
    if (!m_apply || !request.sessionId ||
        request.apply.actorFormId != request.context.actorFormId ||
        request.apply.area != request.context.area ||
        request.apply.slot != request.context.slot) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
            "Catalog preview Save does not match its Actor and slot"));
    }

    if (request.apply.mode == core::ApplyTattooMode::synchronizeOnly) {
        if (!m_synchronizationRetry ||
            m_synchronizationRetry->sessionId != request.sessionId ||
            m_synchronizationRetry->context != request.context) {
            return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
                "No matching catalog preview synchronization retry exists"));
        }
        auto original = request.apply;
        original.mode = core::ApplyTattooMode::applyAndSynchronize;
        if (original != m_synchronizationRetry->apply) {
            return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
                "Catalog preview synchronization retry changed its Save intent"));
        }
        auto result = m_apply(request.apply);
        if (result) m_synchronizationRetry.reset();
        return result;
    }

    if (m_synchronizationRetry || !matchesActive(request.sessionId, request.context) ||
        !m_visual || !m_originalSlot) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
            "Catalog preview session is stale"));
    }
    auto slot = validateSlot(request.context);
    if (!slot || *slot != *m_originalSlot) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
            "The selected slot changed before Save"));
    }
    if (!m_visual->targetStillStable(visualContext(request.context))) {
        return std::unexpected(previewError(core::ServiceErrorCode::previewConflict,
            "The previewed Actor material changed before Save"));
    }
    auto result = m_apply(request.apply);
    if (result || result.error().mutationSideEffect != core::MutationSideEffect::none) {
        invalidate();
        if (!result) m_synchronizationRetry = request;
    }
    return result;
}

void CatalogPreviewRuntimeSession::invalidate() noexcept {
    if (m_visual) m_visual->invalidate();
    m_visual.reset();
    m_context.reset();
    m_originalSlot.reset();
    m_sessionId=0;
    m_synchronizationRetry.reset();
}

}  // namespace stui::runtime
