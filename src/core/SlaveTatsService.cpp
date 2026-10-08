#include "core/SlaveTatsService.h"

#include <cmath>
#include <optional>
#include <utility>

namespace stui::core {
namespace {

bool isValidArea(TattooArea area) noexcept {
    switch (area) {
    case TattooArea::body:
    case TattooArea::face:
    case TattooArea::hands:
    case TattooArea::feet:
        return true;
    }

    return false;
}

std::optional<ServiceError> validateLoadoutValue(const TattooLoadout& loadout) {
    const auto validation = validateTattooLoadout(loadout);
    if (validation) return std::nullopt;

    switch (validation.error()) {
    case TattooLoadoutValidationError::missingName:
    case TattooLoadoutValidationError::missingIdentity:
        return ServiceError{ServiceErrorCode::tattooNotFound, "Tattoo identity is required"};
    case TattooLoadoutValidationError::invalidArea:
        return ServiceError{ServiceErrorCode::invalidArea, "Invalid tattoo area"};
    case TattooLoadoutValidationError::invalidSlot:
        return ServiceError{ServiceErrorCode::invalidSlot, "Invalid tattoo slot"};
    case TattooLoadoutValidationError::duplicatePosition:
        return ServiceError{ServiceErrorCode::invalidSlot,
            "Tattoo loadout contains duplicate area and slot entries"};
    case TattooLoadoutValidationError::invalidColor:
    case TattooLoadoutValidationError::invalidAppearance:
        return ServiceError{ServiceErrorCode::updateFailed,
            "Tattoo loadout appearance values are invalid"};
    }
    return ServiceError{ServiceErrorCode::updateFailed, "Tattoo loadout is invalid"};
}

}  // namespace

SlaveTatsService::SlaveTatsService(ITattooRuntime& runtime) noexcept :
    m_runtime(runtime) {}

TattooQueryResult SlaveTatsService::queryAvailable(std::string_view domain) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable,
            "SlaveTatsNG not available",
        });
    }

    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable,
            "JContainers not ready",
        });
    }

    return m_runtime.queryAvailable(domain);
}

TattooSlotsResult SlaveTatsService::querySlots(std::uint32_t actorFormId, TattooArea area) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable,
            "SlaveTatsNG not available",
        });
    }

    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable,
            "JContainers not ready",
        });
    }

    if (actorFormId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::actorNotFound,
            "Actor not found",
        });
    }

    if (!isValidArea(area)) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::invalidArea,
            "Invalid tattoo area",
        });
    }

    return m_runtime.querySlots(actorFormId, area);
}

ApplyTattooResult SlaveTatsService::applyToSlot(const ApplyTattooRequest& request) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable,
            "SlaveTatsNG not available",
        });
    }

    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable,
            "JContainers not ready",
        });
    }

    if (request.actorFormId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::actorNotFound,
            "Actor not found",
        });
    }

    if (!isValidArea(request.area)) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::invalidArea,
            "Invalid tattoo area",
        });
    }

    if (request.mode == ApplyTattooMode::synchronizeOnly) {
        return m_runtime.applyToSlot(request);
    }

    if (request.slot < 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::invalidSlot,
            "Invalid tattoo slot",
        });
    }

    if (request.section.empty() || request.name.empty()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::tattooNotFound,
            "Tattoo section and name are required",
        });
    }

    if (!(request.alpha >= 0.0F && request.alpha <= 1.0F)) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::applyFailed,
            "Tattoo alpha must be between 0 and 1",
        });
    }

    return m_runtime.applyToSlot(request);
}

RemoveTattooResult SlaveTatsService::removeFromSlot(const RemoveTattooRequest& request) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable,
            "SlaveTatsNG not available",
        });
    }

    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable,
            "JContainers not ready",
        });
    }

    if (request.actorFormId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::actorNotFound,
            "Actor not found",
        });
    }

    if (!isValidArea(request.area)) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::invalidArea,
            "Invalid tattoo area",
        });
    }

    if (request.slot < 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::invalidSlot,
            "Invalid tattoo slot",
        });
    }

    return m_runtime.removeFromSlot(request);
}

UpdateTattooAppearanceResult SlaveTatsService::updateAppearance(
    const UpdateTattooAppearanceRequest& request) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable,
            "SlaveTatsNG not available",
        });
    }

    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable,
            "JContainers not ready",
        });
    }

    if (request.actorFormId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::actorNotFound,
            "Actor not found",
        });
    }

    if (request.mode == UpdateTattooAppearanceMode::updateAndSynchronize &&
        request.runtimeHandle == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::staleTattooHandle,
            "Tattoo handle is invalid; refresh the slot snapshot and try again",
        });
    }

    if (request.mode == UpdateTattooAppearanceMode::updateAndSynchronize) {
        if (request.color < 0 || request.color > 0xFFFFFF) {
            return std::unexpected(ServiceError{
                ServiceErrorCode::updateFailed,
                "Tattoo color must be between 0 and 0xFFFFFF",
            });
        }

        if (!std::isfinite(request.alpha) || request.alpha < 0.0F || request.alpha > 1.0F) {
            return std::unexpected(ServiceError{
                ServiceErrorCode::updateFailed,
                "Tattoo alpha must be between 0 and 1",
            });
        }

        if (request.glow < 0 || request.glow > 0xFFFFFF) {
            return std::unexpected(ServiceError{
                ServiceErrorCode::updateFailed,
                "Tattoo glow must be between 0 and 0xFFFFFF",
            });
        }

        if (!std::isfinite(request.glossiness) || request.glossiness < 0.0F) {
            return std::unexpected(ServiceError{
                ServiceErrorCode::updateFailed,
                "Tattoo glossiness must be finite and non-negative",
            });
        }

        if (!std::isfinite(request.specularStrength) || request.specularStrength < 0.0F) {
            return std::unexpected(ServiceError{
                ServiceErrorCode::updateFailed,
                "Tattoo specular strength must be finite and non-negative",
            });
        }

        if (!std::isfinite(request.emissiveMult) || request.emissiveMult < 0.0F) {
            return std::unexpected(ServiceError{
                ServiceErrorCode::updateFailed,
                "Tattoo emissive multiplier must be finite and non-negative",
            });
        }
    }

    return m_runtime.updateAppearance(request);
}

SetTattooLockedResult SlaveTatsService::setTattooLocked(
    const SetTattooLockedRequest& request) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable,
            "SlaveTatsNG not available",
        });
    }

    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable,
            "JContainers not ready",
        });
    }

    if (request.actorFormId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::actorNotFound,
            "Actor not found",
        });
    }

    if (request.runtimeHandle == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::staleTattooHandle,
            "Tattoo handle is invalid; refresh the slot snapshot and try again",
        });
    }

    return m_runtime.setTattooLocked(request);
}

CaptureTattooLoadoutResult SlaveTatsService::captureLoadout(
    const CaptureTattooLoadoutRequest& request) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available"});
    }
    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable, "JContainers not ready"});
    }
    if (request.actorFormId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::actorNotFound, "Actor not found"});
    }
    return m_runtime.captureLoadout(request);
}

PreviewTattooLoadoutResult SlaveTatsService::previewLoadout(
    const PreviewTattooLoadoutRequest& request) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available"});
    }
    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable, "JContainers not ready"});
    }
    if (request.actorFormId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::actorNotFound, "Actor not found"});
    }
    if (auto invalid = validateLoadoutValue(request.loadout)) {
        return std::unexpected(std::move(*invalid));
    }
    return m_runtime.previewLoadout(request);
}

RestoreTattooLoadoutResult SlaveTatsService::restoreLoadout(
    const RestoreTattooLoadoutRequest& request) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available"});
    }
    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable, "JContainers not ready"});
    }
    if (request.actorFormId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::actorNotFound, "Actor not found"});
    }
    if (auto invalid = validateLoadoutValue(request.loadout)) {
        return std::unexpected(std::move(*invalid));
    }
    return m_runtime.restoreLoadout(request);
}

RestoreTattooLoadoutResult SlaveTatsService::retryLoadoutSynchronization(
    const RetryTattooLoadoutSynchronizationRequest& request) {
    if (!m_runtime.apiAvailable()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available"});
    }
    if (!m_runtime.jContainersReady()) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::jContainersUnavailable, "JContainers not ready"});
    }
    if (request.actorFormId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::actorNotFound, "Actor not found"});
    }
    if (request.retryId == 0) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::synchronizeFailed, "Tattoo loadout retry is invalid"});
    }
    return m_runtime.retryLoadoutSynchronization(request);
}

}  // namespace stui::core
