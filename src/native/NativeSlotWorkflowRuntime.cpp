#include "native/NativeSlotWorkflowRuntime.h"

#include <expected>
#include <utility>

namespace stui::native {
namespace {

core::ServiceError operationError(core::ServiceErrorCode code, const char* message) {
    return core::ServiceError{.code = code, .message = message};
}

core::ServiceError appearanceOperationError(
    core::ServiceErrorCode code,
    const char* message,
    core::UpdateTattooAppearanceMode mode) {
    return core::ServiceError{
        .code = code,
        .message = message,
        .mutationSideEffect = mode == core::UpdateTattooAppearanceMode::updateAndSynchronize
            ? core::MutationSideEffect::mayHaveOccurred
            : core::MutationSideEffect::none,
    };
}

core::ServiceErrorCode appearanceErrorCode(
    core::UpdateTattooAppearanceMode mode) noexcept {
    return mode == core::UpdateTattooAppearanceMode::synchronizeOnly
        ? core::ServiceErrorCode::synchronizeFailed
        : core::ServiceErrorCode::updateFailed;
}

class InFlightGuard {
public:
    explicit InFlightGuard(std::atomic_bool& inFlight) noexcept : m_inFlight(inFlight) {}
    ~InFlightGuard() {
        m_inFlight.store(false);
    }

    InFlightGuard(const InFlightGuard&) = delete;
    InFlightGuard& operator=(const InFlightGuard&) = delete;

private:
    std::atomic_bool& m_inFlight;
};

}  // namespace

NativeSlotWorkflowRuntime::NativeSlotWorkflowRuntime(
    NativeSlotWorkflowModel& model,
    ActorTargetOperation resolveActorTarget,
    SlotQueryOperation query,
    SlotApplyOperation apply,
    SlotRemoveOperation remove,
    SlotAppearanceOperation updateAppearance,
    SlotLockOperation setLocked,
    NativeSlotScheduler scheduler,
    LivePreviewClock livePreviewClock,
    FavoriteOperation favoriteOperation,
    RecentTattooLoadOperation recentTattooLoadOperation,
    RecentTattooRecordOperation recentTattooRecordOperation,
    AppearancePresetOperation appearancePresetOperation,
    TattooLoadoutStoreOperation tattooLoadoutStoreOperation,
    TattooLoadoutCaptureOperation tattooLoadoutCaptureOperation,
    TattooLoadoutPreviewOperation tattooLoadoutPreviewOperation,
    TattooLoadoutRestoreOperation tattooLoadoutRestoreOperation,
    TattooLoadoutRetryOperation tattooLoadoutRetryOperation,
    LoadedActorScanOperation loadedActorScanOperation,
    CatalogPreviewBeginOperation catalogPreviewBegin,
    CatalogPreviewPaintOperation catalogPreviewPaint,
    CatalogPreviewRestoreOperation catalogPreviewRestore,
    CatalogPreviewCommitOperation catalogPreviewCommit)
    : m_model(model),
      m_resolveActorTarget(std::move(resolveActorTarget)),
      m_scanNearbyActors(),
      m_scanLoadedActors(std::move(loadedActorScanOperation)),
      m_query(std::move(query)),
      m_apply(std::move(apply)),
      m_remove(std::move(remove)),
      m_updateAppearance(std::move(updateAppearance)),
      m_setLocked(std::move(setLocked)),
      m_scheduler(std::move(scheduler)),
      m_livePreviewClock(std::move(livePreviewClock)),
      m_favoriteOperation(std::move(favoriteOperation)),
      m_recentTattooLoadOperation(std::move(recentTattooLoadOperation)),
      m_recentTattooRecordOperation(std::move(recentTattooRecordOperation)),
      m_appearancePresetOperation(std::move(appearancePresetOperation)),
      m_tattooLoadoutStoreOperation(std::move(tattooLoadoutStoreOperation)),
      m_tattooLoadoutCaptureOperation(std::move(tattooLoadoutCaptureOperation)),
      m_tattooLoadoutPreviewOperation(std::move(tattooLoadoutPreviewOperation)),
      m_tattooLoadoutRestoreOperation(std::move(tattooLoadoutRestoreOperation)),
      m_tattooLoadoutRetryOperation(std::move(tattooLoadoutRetryOperation)),
      m_catalogPreviewBegin(std::move(catalogPreviewBegin)),
      m_catalogPreviewPaint(std::move(catalogPreviewPaint)),
      m_catalogPreviewRestore(std::move(catalogPreviewRestore)),
      m_catalogPreviewCommit(std::move(catalogPreviewCommit)) {}

bool NativeSlotWorkflowRuntime::catalogPreviewActive() const noexcept {
    return m_catalogPreviewModel.open();
}

bool NativeSlotWorkflowRuntime::catalogPreviewStarting() const noexcept {
    return m_catalogPreviewBeginPending;
}

const core::ServiceError* NativeSlotWorkflowRuntime::catalogPreviewError() const noexcept {
    return m_catalogPreviewError ? &*m_catalogPreviewError : nullptr;
}

std::optional<core::TattooEntry> NativeSlotWorkflowRuntime::catalogPreviewSelection() const {
    return m_catalogPreviewModel.selectedCandidate();
}

std::optional<core::TattooEntry> NativeSlotWorkflowRuntime::catalogPreviewDisplay() const {
    return m_catalogPreviewModel.displayCandidate();
}

bool NativeSlotWorkflowRuntime::catalogPreviewIsPinned(
    const repository::TattooDefinition& tattoo) const {
    return m_catalogPreviewPinnedIdentity && m_catalogPreviewModel.selectedCandidate() &&
        *m_catalogPreviewPinnedIdentity ==
            repository::recentTattooIdentity(tattoo, m_model.selectedArea());
}

void NativeSlotWorkflowRuntime::selectCatalogPreview(
    const repository::TattooDefinition& tattoo) {
    if (!m_catalogPreviewModel.open()) return;
    if (m_catalogPreviewModel.select(catalogPreviewCandidate(tattoo))) {
        m_catalogPreviewPinnedIdentity =
            repository::recentTattooIdentity(tattoo, m_model.selectedArea());
    }
}
void NativeSlotWorkflowRuntime::setCatalogPreviewInk(std::int32_t color) {
    m_catalogPreviewModel.setInk(color);
}
void NativeSlotWorkflowRuntime::setCatalogPreviewGlow(std::int32_t color) {
    m_catalogPreviewModel.setGlow(color);
}
void NativeSlotWorkflowRuntime::setCatalogPreviewGlossy(bool enabled) {
    m_catalogPreviewModel.setGlossy(enabled);
}
void NativeSlotWorkflowRuntime::undoCatalogPreview() {
    m_catalogPreviewModel.undo();
    m_catalogPreviewPinnedIdentity.reset();
    m_catalogPreviewLastPaint.reset();
}
bool NativeSlotWorkflowRuntime::saveCatalogPreview() {
    return m_catalogPreviewModel.requestSave();
}

bool NativeSlotWorkflowRuntime::closeCatalogPreview(bool closeMenu) {
    if (m_catalogPreviewBeginPending) {
        m_catalogPreviewCancelBegin = true;
        m_catalogPreviewCloseMenu = m_catalogPreviewCloseMenu || closeMenu;
        return true;
    }
    if (!m_catalogPreviewModel.open()) return false;
    m_catalogPreviewCloseRequested = true;
    m_catalogPreviewCloseMenu = m_catalogPreviewCloseMenu || closeMenu;
    m_catalogPreviewModel.close();
    return true;
}

void NativeSlotWorkflowRuntime::retryCatalogPreview() {
    m_catalogPreviewError.reset();
    if (!m_catalogPreviewSession) m_catalogPreviewAttempt.reset();
    m_catalogPreviewModel.retry();
}

void NativeSlotWorkflowRuntime::invalidateCatalogPreview() noexcept {
    ++m_catalogPreviewEpoch;
    m_catalogPreviewModel = CatalogPreviewModel{};
    m_catalogPreviewAttempt.reset();
    m_catalogPreviewSession.reset();
    m_catalogPreviewError.reset();
    m_catalogPreviewPinnedIdentity.reset();
    m_catalogPreviewLastPaint.reset();
    m_catalogPreviewBeginPending = false;
    m_catalogPreviewCancelBegin = false;
    m_catalogPreviewCloseRequested = false;
    m_catalogPreviewCloseMenu = false;
    m_model.setCatalogPreviewActive(false);
}

NativeSlotWorkflowRuntime::NativeSlotWorkflowRuntime(
    NativeSlotWorkflowModel& model,
    NearbyActorRuntimeTag,
    NearbyActorScanOperation scanNearbyActors,
    SlotQueryOperation query,
    SlotApplyOperation apply,
    SlotRemoveOperation remove,
    SlotAppearanceOperation updateAppearance,
    SlotLockOperation setLocked,
    NativeSlotScheduler scheduler,
    LivePreviewClock livePreviewClock,
    FavoriteOperation favoriteOperation,
    RecentTattooLoadOperation recentTattooLoadOperation,
    RecentTattooRecordOperation recentTattooRecordOperation,
    AppearancePresetOperation appearancePresetOperation,
    TattooLoadoutStoreOperation tattooLoadoutStoreOperation,
    TattooLoadoutCaptureOperation tattooLoadoutCaptureOperation,
    TattooLoadoutPreviewOperation tattooLoadoutPreviewOperation,
    TattooLoadoutRestoreOperation tattooLoadoutRestoreOperation,
    TattooLoadoutRetryOperation tattooLoadoutRetryOperation,
    LoadedActorScanOperation loadedActorScanOperation,
    CatalogPreviewBeginOperation catalogPreviewBegin,
    CatalogPreviewPaintOperation catalogPreviewPaint,
    CatalogPreviewRestoreOperation catalogPreviewRestore,
    CatalogPreviewCommitOperation catalogPreviewCommit)
    : m_model(model),
      m_resolveActorTarget(),
      m_scanNearbyActors(std::move(scanNearbyActors)),
      m_scanLoadedActors(std::move(loadedActorScanOperation)),
      m_query(std::move(query)),
      m_apply(std::move(apply)),
      m_remove(std::move(remove)),
      m_updateAppearance(std::move(updateAppearance)),
      m_setLocked(std::move(setLocked)),
      m_scheduler(std::move(scheduler)),
      m_livePreviewClock(std::move(livePreviewClock)),
      m_favoriteOperation(std::move(favoriteOperation)),
      m_recentTattooLoadOperation(std::move(recentTattooLoadOperation)),
      m_recentTattooRecordOperation(std::move(recentTattooRecordOperation)),
      m_appearancePresetOperation(std::move(appearancePresetOperation)),
      m_tattooLoadoutStoreOperation(std::move(tattooLoadoutStoreOperation)),
      m_tattooLoadoutCaptureOperation(std::move(tattooLoadoutCaptureOperation)),
      m_tattooLoadoutPreviewOperation(std::move(tattooLoadoutPreviewOperation)),
      m_tattooLoadoutRestoreOperation(std::move(tattooLoadoutRestoreOperation)),
      m_tattooLoadoutRetryOperation(std::move(tattooLoadoutRetryOperation)),
      m_catalogPreviewBegin(std::move(catalogPreviewBegin)),
      m_catalogPreviewPaint(std::move(catalogPreviewPaint)),
      m_catalogPreviewRestore(std::move(catalogPreviewRestore)),
      m_catalogPreviewCommit(std::move(catalogPreviewCommit)) {}

void NativeSlotWorkflowRuntime::pump() {
    drainFavoriteCompletions();
    drainRecentTattooCompletions();
    drainAppearancePresetCompletions();
    drainTattooLoadoutCompletions();
    const auto now = m_livePreviewClock();
    m_model.advanceLivePreview(now);

    bool expected = false;
    if (!m_inFlight.compare_exchange_strong(expected, true)) {
        return;
    }

    if (auto nearby = m_model.takeNearbyActorScanRequest()) {
        scheduleNearbyActorScan(*nearby);
        return;
    }
    if (auto loaded = m_model.takeLoadedActorScanRequest()) {
        scheduleLoadedActorScan(*loaded);
        return;
    }
    if (auto target = m_model.takeActorTargetRequest()) {
        scheduleActorTarget(*target);
        return;
    }
    if (auto query = m_model.takeSlotQuery()) {
        scheduleQuery(std::move(*query));
        return;
    }
    if (auto apply = m_model.takeApplyRequest()) {
        scheduleApply(std::move(*apply));
        return;
    }
    if (auto remove = m_model.takeRemoveRequest()) {
        scheduleRemove(std::move(*remove));
        return;
    }
    if (auto appearance = m_model.takeAppearanceRequest()) {
        scheduleAppearance(std::move(*appearance));
        return;
    }
    if (auto lock = m_model.takeLockRequest()) {
        scheduleLock(std::move(*lock));
        return;
    }
    if (auto favorite = m_model.takeFavoriteRequest()) {
        scheduleFavorite(std::move(*favorite));
        return;
    }
    if (auto recentTattoo = m_model.takeRecentTattooRequest()) {
        scheduleRecentTattoo(std::move(*recentTattoo));
        return;
    }
    if (auto appearancePreset = m_model.takeAppearancePresetRequest()) {
        scheduleAppearancePreset(std::move(*appearancePreset));
        return;
    }
    if (auto loadoutStore = m_model.takeTattooLoadoutStoreRequest()) {
        scheduleTattooLoadoutStore(std::move(*loadoutStore));
        return;
    }
    if (auto loadoutActor = m_model.takeTattooLoadoutActorRequest()) {
        scheduleTattooLoadoutActor(std::move(*loadoutActor));
        return;
    }

    if (m_catalogPreviewSession && m_catalogPreviewModel.open()) {
        if (m_catalogPreviewModel.selectedCandidate() && m_catalogPreviewLastPaint &&
            now - *m_catalogPreviewLastPaint >= std::chrono::milliseconds(500))
            m_catalogPreviewModel.requestRepaint();
        if (auto paint = m_catalogPreviewModel.takePaint()) {
            scheduleCatalogPreviewPaint(std::move(*paint));
            return;
        }
        if (auto commit = m_catalogPreviewModel.takeCommit()) {
            scheduleCatalogPreviewCommit(std::move(*commit));
            return;
        }
    }
    if (m_catalogPreviewBegin && !m_catalogPreviewBeginPending &&
        !m_catalogPreviewSession && !m_catalogPreviewModel.open()) {
        const auto context = m_model.catalogPreviewContext();
        if (context && m_catalogPreviewAttempt != context) {
            scheduleCatalogPreviewBegin(*context);
            return;
        }
        if (!context) m_catalogPreviewAttempt.reset();
    }

    m_inFlight.store(false);
}

void NativeSlotWorkflowRuntime::scheduleCatalogPreviewBegin(
    core::CatalogPreviewContext context) {
    m_catalogPreviewAttempt = context;
    m_catalogPreviewBeginPending = true;
    m_model.setCatalogPreviewActive(true);
    const auto epoch = m_catalogPreviewEpoch;
    NativeSlotTask task = [this, context, epoch] {
        {
        InFlightGuard guard(m_inFlight);
        if (epoch != m_catalogPreviewEpoch) return;
        core::CatalogPreviewBeginResult result = std::unexpected(operationError(
            core::ServiceErrorCode::previewUnavailable,
            "Catalog preview is unavailable."));
        try { result = m_catalogPreviewBegin(context); } catch (...) {}
        m_catalogPreviewBeginPending = false;
        if (!result) {
            m_catalogPreviewError = result.error();
            m_model.setCatalogPreviewActive(false);
            if (m_catalogPreviewCancelBegin) {
                m_model.finishCatalogPreviewClose(m_catalogPreviewCloseMenu);
                m_catalogPreviewAttempt.reset();
            }
            m_catalogPreviewCancelBegin = false;
            m_catalogPreviewCloseMenu = false;
            return;
        }
        m_catalogPreviewSession = result->sessionId;
        m_catalogPreviewLastPaint.reset();
        const bool cancel = m_catalogPreviewCancelBegin ||
            m_model.catalogPreviewContext() != context;
        m_catalogPreviewModel.begin({context.actorFormId, context.generation,
            context.area, context.slot}, true);
        m_catalogPreviewError.reset();
        m_catalogPreviewCancelBegin = false;
        if (cancel) m_catalogPreviewModel.close();
        }
        if (m_catalogPreviewModel.closing()) pump();
    };
    try { m_scheduler(std::move(task)); }
    catch (...) {
        m_catalogPreviewBeginPending = false;
        m_model.setCatalogPreviewActive(false);
        m_catalogPreviewError = operationError(core::ServiceErrorCode::previewUnavailable,
            "Failed to schedule catalog preview.");
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleCatalogPreviewPaint(CatalogPreviewPaint paint) {
    const auto session = *m_catalogPreviewSession;
    const bool closing = m_catalogPreviewModel.closing();
    const auto epoch = m_catalogPreviewEpoch;
    NativeSlotTask task = [this, paint = std::move(paint), session, closing, epoch] {
        {
        InFlightGuard guard(m_inFlight);
        if (epoch != m_catalogPreviewEpoch) return;
        const core::CatalogPreviewContext context{paint.context.actorFormId,
            paint.context.generation, paint.context.area, paint.context.slot};
        core::ServiceError error = operationError(core::ServiceErrorCode::previewFailed,
            "Catalog preview update failed.");
        bool success = false;
        try {
            if (closing) {
                if (m_catalogPreviewRestore) {
                    const auto result = m_catalogPreviewRestore({session, context});
                    success = result.has_value();
                    if (!result) error = result.error();
                }
            } else if (m_catalogPreviewPaint) {
                const auto result = m_catalogPreviewPaint({session, context, paint.candidate});
                success = result.has_value();
                if (!result) error = result.error();
            }
        } catch (...) {}
        m_catalogPreviewModel.completePaint(paint, success);
        if (success && paint.candidate && !closing)
            m_catalogPreviewLastPaint = m_livePreviewClock();
        else if (success && !paint.candidate)
            m_catalogPreviewLastPaint.reset();
        if (!success) m_catalogPreviewError = error;
        else m_catalogPreviewError.reset();
        if (success && closing && !m_catalogPreviewModel.open()) {
            m_catalogPreviewSession.reset();
            m_catalogPreviewAttempt.reset();
            m_catalogPreviewPinnedIdentity.reset();
            m_model.finishCatalogPreviewClose(m_catalogPreviewCloseMenu);
            m_catalogPreviewCloseMenu = false;
            m_catalogPreviewCloseRequested = false;
        }
        }
        if (m_catalogPreviewCloseRequested && m_catalogPreviewModel.closing()) pump();
    };
    try { m_scheduler(std::move(task)); }
    catch (...) {
        m_catalogPreviewModel.completePaint(paint, false);
        m_catalogPreviewError = operationError(core::ServiceErrorCode::previewFailed,
            "Failed to schedule catalog preview update.");
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleCatalogPreviewCommit(CatalogPreviewCommit commit) {
    const auto request = catalogPreviewCommitRequest(commit, *m_catalogPreviewSession);
    const auto epoch = m_catalogPreviewEpoch;
    NativeSlotTask task = [this, commit = std::move(commit), request, epoch] {
        {
        InFlightGuard guard(m_inFlight);
        if (epoch != m_catalogPreviewEpoch) return;
        core::CatalogPreviewCommitResult result = std::unexpected(operationError(
            core::ServiceErrorCode::applyFailed, "Catalog preview Save failed."));
        try {
            if (m_catalogPreviewCommit) result = m_catalogPreviewCommit(request);
        } catch (...) {}
        const bool written = result ||
            result.error().mutationSideEffect != core::MutationSideEffect::none;
        m_catalogPreviewModel.completeCommit(commit, result.has_value(), written);
        if (!result) {
            m_catalogPreviewError = result.error();
            if (m_catalogPreviewCloseRequested && !written) m_catalogPreviewModel.close();
        } else {
            m_catalogPreviewError.reset();
            if (m_catalogPreviewPinnedIdentity) {
                m_model.recordCatalogPreviewSaved(*m_catalogPreviewPinnedIdentity);
            }
            m_catalogPreviewPinnedIdentity.reset();
            m_catalogPreviewSession.reset();
            m_catalogPreviewAttempt.reset();
            m_model.finishCatalogPreviewClose(m_catalogPreviewCloseMenu);
            m_catalogPreviewCloseMenu = false;
            m_catalogPreviewCloseRequested = false;
            m_model.refreshSelectedArea();
        }
        }
        if (m_catalogPreviewCloseRequested && m_catalogPreviewModel.closing()) pump();
    };
    try { m_scheduler(std::move(task)); }
    catch (...) {
        m_catalogPreviewModel.completeCommit(commit, false, false);
        m_catalogPreviewError = operationError(core::ServiceErrorCode::applyFailed,
            "Failed to schedule catalog preview Save.");
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::drainTattooLoadoutCompletions() {
    std::vector<std::pair<std::uint64_t, runtime::TattooLoadoutResult>> store;
    std::vector<std::pair<std::uint64_t, core::CaptureTattooLoadoutResult>> capture;
    std::vector<std::pair<std::uint64_t, core::PreviewTattooLoadoutResult>> preview;
    std::vector<std::pair<std::uint64_t, core::RestoreTattooLoadoutResult>> restore;
    {
        const std::scoped_lock lock(m_tattooLoadoutCompletionMutex);
        store.swap(m_tattooLoadoutStoreCompletions);
        capture.swap(m_tattooLoadoutCaptureCompletions);
        preview.swap(m_tattooLoadoutPreviewCompletions);
        restore.swap(m_tattooLoadoutRestoreCompletions);
    }
    for (auto& [requestId, result] : store) {
        m_model.completeTattooLoadoutStore(requestId, std::move(result));
    }
    for (auto& [generation, result] : capture) {
        m_model.completeTattooLoadoutCapture(generation, std::move(result));
    }
    for (auto& [generation, result] : preview) {
        m_model.completeTattooLoadoutPreview(generation, std::move(result));
    }
    for (auto& [generation, result] : restore) {
        m_model.completeTattooLoadoutRestore(generation, std::move(result));
    }
}

void NativeSlotWorkflowRuntime::drainAppearancePresetCompletions() {
    std::vector<std::pair<std::uint64_t, runtime::AppearancePresetResult>> completions;
    {
        const std::scoped_lock lock(m_appearancePresetCompletionMutex);
        completions.swap(m_appearancePresetCompletions);
    }
    for (auto& [requestId, result] : completions) {
        m_model.completeAppearancePreset(requestId, std::move(result));
    }
}

void NativeSlotWorkflowRuntime::drainRecentTattooCompletions() {
    std::vector<std::pair<std::uint64_t, runtime::RecentTattooResult>> completions;
    {
        const std::scoped_lock lock(m_recentTattooCompletionMutex);
        completions.swap(m_recentTattooCompletions);
    }
    for (auto& [requestId, result] : completions) {
        m_model.completeRecentTattoo(requestId, std::move(result));
    }
}

void NativeSlotWorkflowRuntime::drainFavoriteCompletions() {
    std::vector<std::pair<std::uint64_t, runtime::FavoriteResult>> completions;
    {
        const std::scoped_lock lock(m_favoriteCompletionMutex);
        completions.swap(m_favoriteCompletions);
    }
    for (auto& [requestId, result] : completions) {
        m_model.completeFavorite(requestId, std::move(result));
    }
}

void NativeSlotWorkflowRuntime::scheduleActorTarget(ActorTargetResolutionTicket ticket) {
    NativeSlotTask task = [this, ticket] {
        InFlightGuard guard(m_inFlight);
        ActorTargetResult result = std::unexpected(operationError(
            core::ServiceErrorCode::actorNotFound,
            "Failed to resolve crosshair Actor."));
        try {
            result = m_resolveActorTarget();
        } catch (...) {
        }
        m_model.completeActorTargetResolution(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeActorTargetResolution(
            ticket.generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::actorNotFound,
                "Failed to resolve crosshair Actor.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleNearbyActorScan(NearbyActorScanTicket ticket) {
    NativeSlotTask task = [this, ticket] {
        InFlightGuard guard(m_inFlight);
        NearbyActorScanResult result = std::unexpected(operationError(
            core::ServiceErrorCode::actorNotFound,
            "Failed to scan nearby Actors."));
        try {
            if (m_scanNearbyActors) {
                result = m_scanNearbyActors();
            }
        } catch (...) {
        }
        m_model.completeNearbyActorScan(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeNearbyActorScan(
            ticket.generation,
            NearbyActorScanResult(std::unexpected(operationError(
                core::ServiceErrorCode::actorNotFound,
                "Failed to scan nearby Actors."))));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleLoadedActorScan(LoadedActorScanTicket ticket) {
    NativeSlotTask task = [this, ticket] {
        InFlightGuard guard(m_inFlight);
        LoadedActorScanResult result = std::unexpected(operationError(
            core::ServiceErrorCode::actorNotFound,
            "Failed to scan loaded Actors."));
        try {
            if (m_scanLoadedActors) result = m_scanLoadedActors();
        } catch (...) {
        }
        m_model.completeLoadedActorScan(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeLoadedActorScan(
            ticket.generation,
            LoadedActorScanResult(std::unexpected(operationError(
                core::ServiceErrorCode::actorNotFound,
                "Failed to schedule loaded Actor scan."))));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleQuery(SlotQueryTicket ticket) {
    NativeSlotTask task = [this, ticket] {
        InFlightGuard guard(m_inFlight);
        core::TattooSlotsResult result = std::unexpected(operationError(
            core::ServiceErrorCode::slotQueryFailed,
            "Slot query failed."));
        try {
            result = m_query(ticket.actorFormId, ticket.area);
        } catch (...) {
        }
        m_model.completeSlotQuery(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeSlotQuery(
            ticket.generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::slotQueryFailed,
                "Failed to schedule slot query.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleApply(SlotApplyTicket ticket) {
    const std::uint64_t generation = ticket.generation;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        core::ApplyTattooResult result = std::unexpected(operationError(
            core::ServiceErrorCode::applyFailed,
            "Tattoo apply failed."));
        try {
            result = m_apply(ticket.request);
        } catch (...) {
        }
        m_model.completeApply(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeApply(
            generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::applyFailed,
                "Failed to schedule tattoo apply.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleRemove(SlotRemoveTicket ticket) {
    const std::uint64_t generation = ticket.generation;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        core::RemoveTattooResult result = std::unexpected(operationError(
            core::ServiceErrorCode::removeFailed,
            "Tattoo remove failed."));
        try {
            result = m_remove(ticket.request);
        } catch (...) {
        }
        m_model.completeRemove(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeRemove(
            generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::removeFailed,
                "Failed to schedule tattoo remove.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleAppearance(SlotAppearanceTicket ticket) {
    const std::uint64_t generation = ticket.generation;
    const auto errorCode = appearanceErrorCode(ticket.request.mode);
    const auto mode = ticket.request.mode;
    NativeSlotTask task = [this, ticket = std::move(ticket), errorCode, mode] {
        InFlightGuard guard(m_inFlight);
        core::UpdateTattooAppearanceResult result = std::unexpected(appearanceOperationError(
            errorCode,
            "Tattoo appearance update failed.",
            mode));
        try {
            result = m_updateAppearance(ticket.request);
        } catch (...) {
        }
        m_model.completeAppearanceUpdate(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeAppearanceUpdate(
            generation,
            std::unexpected(operationError(
                errorCode,
                "Failed to schedule tattoo appearance update.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleLock(SlotLockTicket ticket) {
    const std::uint64_t generation = ticket.generation;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        core::SetTattooLockedResult result = std::unexpected(operationError(
            core::ServiceErrorCode::lockFailed,
            "Tattoo lock state update failed."));
        try {
            result = m_setLocked(ticket.request);
        } catch (...) {
        }
        m_model.completeLockStateChange(ticket.generation, std::move(result));
    };
    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeLockStateChange(
            generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::lockFailed,
                "Failed to schedule tattoo lock state update.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleFavorite(FavoriteTicket ticket) {
    const std::uint64_t requestId = ticket.requestId;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        runtime::FavoriteResult result = std::unexpected(runtime::ConfigError{
            .message = "Favorite storage operation failed."});
        try {
            if (!m_favoriteOperation) {
                result = std::unexpected(runtime::ConfigError{
                    .message = "Favorite storage is unavailable."});
            } else {
                result = m_favoriteOperation(ticket.identity, ticket.enabled);
            }
        } catch (...) {
        }
        const std::scoped_lock lock(m_favoriteCompletionMutex);
        m_favoriteCompletions.emplace_back(ticket.requestId, std::move(result));
    };
    try {
        m_scheduler(std::move(task));
    } catch (...) {
        {
            const std::scoped_lock lock(m_favoriteCompletionMutex);
            m_favoriteCompletions.emplace_back(requestId, std::unexpected(runtime::ConfigError{
                .message = "Failed to schedule favorite storage."}));
        }
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleRecentTattoo(RecentTattooTicket ticket) {
    const std::uint64_t requestId = ticket.requestId;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        runtime::RecentTattooResult result = std::unexpected(runtime::ConfigError{
            .message = "Recent tattoo storage operation failed."});
        try {
            if (ticket.kind == RecentTattooRequestKind::load) {
                if (m_recentTattooLoadOperation) {
                    result = m_recentTattooLoadOperation();
                } else {
                    result = std::unexpected(runtime::ConfigError{
                        .message = "Recent tattoo storage is unavailable."});
                }
            } else if (ticket.identity && m_recentTattooRecordOperation) {
                result = m_recentTattooRecordOperation(*ticket.identity);
            } else {
                result = std::unexpected(runtime::ConfigError{
                    .message = "Recent tattoo storage is unavailable."});
            }
        } catch (...) {
        }
        const std::scoped_lock lock(m_recentTattooCompletionMutex);
        m_recentTattooCompletions.emplace_back(ticket.requestId, std::move(result));
    };
    try {
        m_scheduler(std::move(task));
    } catch (...) {
        {
            const std::scoped_lock lock(m_recentTattooCompletionMutex);
            m_recentTattooCompletions.emplace_back(requestId, std::unexpected(
                runtime::ConfigError{.message = "Failed to schedule recent tattoo storage."}));
        }
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleAppearancePreset(AppearancePresetTicket ticket) {
    const std::uint64_t requestId = ticket.requestId;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        runtime::AppearancePresetResult result = std::unexpected(runtime::ConfigError{
            .message = "Appearance preset storage operation failed."});
        try {
            if (m_appearancePresetOperation) {
                result = m_appearancePresetOperation(ticket);
            } else {
                result = std::unexpected(runtime::ConfigError{
                    .message = "Appearance preset storage is unavailable."});
            }
        } catch (...) {
        }
        const std::scoped_lock lock(m_appearancePresetCompletionMutex);
        m_appearancePresetCompletions.emplace_back(ticket.requestId, std::move(result));
    };
    try {
        m_scheduler(std::move(task));
    } catch (...) {
        {
            const std::scoped_lock lock(m_appearancePresetCompletionMutex);
            m_appearancePresetCompletions.emplace_back(requestId, std::unexpected(
                runtime::ConfigError{
                    .message = "Failed to schedule appearance preset storage."}));
        }
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleTattooLoadoutStore(
    TattooLoadoutStoreTicket ticket) {
    const auto requestId = ticket.requestId;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        runtime::TattooLoadoutResult result = std::unexpected(runtime::ConfigError{
            .message = "Tattoo loadout storage operation failed."});
        try {
            if (m_tattooLoadoutStoreOperation) {
                result = m_tattooLoadoutStoreOperation(ticket);
            } else {
                result = std::unexpected(runtime::ConfigError{
                    .message = "Tattoo loadout storage is unavailable."});
            }
        } catch (...) {
        }
        const std::scoped_lock lock(m_tattooLoadoutCompletionMutex);
        m_tattooLoadoutStoreCompletions.emplace_back(ticket.requestId, std::move(result));
    };
    try {
        m_scheduler(std::move(task));
    } catch (...) {
        {
            const std::scoped_lock lock(m_tattooLoadoutCompletionMutex);
            m_tattooLoadoutStoreCompletions.emplace_back(
                requestId,
                std::unexpected(runtime::ConfigError{
                    .message = "Failed to schedule tattoo loadout storage."}));
        }
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleTattooLoadoutActor(
    TattooLoadoutActorTicket ticket) {
    const auto generation = ticket.targetGeneration;
    const auto operation = ticket.operation;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        const auto error = core::ServiceError{
            .code = core::ServiceErrorCode::applyFailed,
            .message = "Tattoo loadout Actor operation failed.",
        };
        const std::scoped_lock lock(m_tattooLoadoutCompletionMutex);
        try {
            switch (ticket.operation) {
            case TattooLoadoutActorOperation::capture:
                m_tattooLoadoutCaptureCompletions.emplace_back(
                    ticket.targetGeneration,
                    m_tattooLoadoutCaptureOperation ?
                        m_tattooLoadoutCaptureOperation({ticket.actorFormId}) :
                        core::CaptureTattooLoadoutResult(std::unexpected(error)));
                return;
            case TattooLoadoutActorOperation::preview:
                m_tattooLoadoutPreviewCompletions.emplace_back(
                    ticket.targetGeneration,
                    m_tattooLoadoutPreviewOperation && ticket.loadout ?
                        m_tattooLoadoutPreviewOperation(
                            {ticket.actorFormId, *ticket.loadout}) :
                        core::PreviewTattooLoadoutResult(std::unexpected(error)));
                return;
            case TattooLoadoutActorOperation::restore:
                m_tattooLoadoutRestoreCompletions.emplace_back(
                    ticket.targetGeneration,
                    m_tattooLoadoutRestoreOperation && ticket.loadout ?
                        m_tattooLoadoutRestoreOperation(
                            {ticket.actorFormId, *ticket.loadout, ticket.approvedPlan}) :
                        core::RestoreTattooLoadoutResult(std::unexpected(error)));
                return;
            case TattooLoadoutActorOperation::retrySynchronization:
                m_tattooLoadoutRestoreCompletions.emplace_back(
                    ticket.targetGeneration,
                    m_tattooLoadoutRetryOperation && ticket.retryId ?
                        m_tattooLoadoutRetryOperation(
                            {ticket.actorFormId, *ticket.retryId}) :
                        core::RestoreTattooLoadoutResult(std::unexpected(error)));
                return;
            }
        } catch (...) {
            switch (ticket.operation) {
            case TattooLoadoutActorOperation::capture:
                m_tattooLoadoutCaptureCompletions.emplace_back(
                    ticket.targetGeneration,
                    std::unexpected(error));
                break;
            case TattooLoadoutActorOperation::preview:
                m_tattooLoadoutPreviewCompletions.emplace_back(
                    ticket.targetGeneration,
                    std::unexpected(error));
                break;
            case TattooLoadoutActorOperation::restore:
            case TattooLoadoutActorOperation::retrySynchronization:
                m_tattooLoadoutRestoreCompletions.emplace_back(
                    ticket.targetGeneration,
                    std::unexpected(error));
                break;
            }
        }
    };
    try {
        m_scheduler(std::move(task));
    } catch (...) {
        const auto error = core::ServiceError{
            .code = core::ServiceErrorCode::applyFailed,
            .message = "Failed to schedule tattoo loadout Actor operation.",
        };
        {
            const std::scoped_lock lock(m_tattooLoadoutCompletionMutex);
            if (operation == TattooLoadoutActorOperation::capture) {
                m_tattooLoadoutCaptureCompletions.emplace_back(
                    generation, std::unexpected(error));
            } else if (operation == TattooLoadoutActorOperation::preview) {
                m_tattooLoadoutPreviewCompletions.emplace_back(
                    generation, std::unexpected(error));
            } else {
                m_tattooLoadoutRestoreCompletions.emplace_back(
                    generation, std::unexpected(error));
            }
        }
        m_inFlight.store(false);
    }
}

}  // namespace stui::native
