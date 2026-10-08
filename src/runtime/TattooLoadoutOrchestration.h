#pragma once

#include "core/ITattooRuntime.h"

#include <array>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>

namespace stui::core {

struct TattooLoadoutRollbackSnapshot {
    std::uint32_t actorFormId{};
    std::array<TattooSlots, 4> areas;
};

struct TattooLoadoutVerification {
    bool matches{};
    std::vector<TattooLoadoutAffectedSlot> affectedSlots;
};

}  // namespace stui::core

namespace stui::runtime {

enum class TattooLoadoutRetryPhase { forwardSync, rollbackSync };

struct TattooLoadoutRetryContext {
    core::TattooLoadoutRetryId id{};
    std::uint32_t actorFormId{};
    TattooLoadoutRetryPhase phase{TattooLoadoutRetryPhase::forwardSync};
    core::TattooLoadoutPlan forwardPlan;
    core::TattooLoadoutRollbackSnapshot rollbackSnapshot;
};

class TattooLoadoutRetryRegistry {
public:
    core::TattooLoadoutRetryId store(TattooLoadoutRetryContext context);
    std::optional<TattooLoadoutRetryContext> take(
        core::TattooLoadoutRetryId id,
        std::uint32_t actorFormId);
    void erase(core::TattooLoadoutRetryId id) noexcept;

private:
    core::TattooLoadoutRetryId m_nextId{1};
    std::map<core::TattooLoadoutRetryId, TattooLoadoutRetryContext> m_contexts;
};

class ITattooLoadoutBackend {
public:
    virtual ~ITattooLoadoutBackend() = default;
    virtual core::TattooLoadoutPlanResult preflight(
        const core::PreviewTattooLoadoutRequest& request) = 0;
    virtual std::expected<core::TattooLoadoutRollbackSnapshot, core::ServiceError>
        captureRollback(const core::TattooLoadoutPlan& plan) = 0;
    virtual std::expected<void, core::ServiceError> applyAction(
        const core::TattooLoadoutPlanAction& action) = 0;
    virtual bool synchronize(std::uint32_t actorFormId) = 0;
    virtual core::TattooLoadoutVerification verifyForward(
        const core::TattooLoadoutPlan& plan) = 0;
    virtual std::expected<void, core::ServiceError> applyRollback(
        const core::TattooLoadoutRollbackSnapshot& snapshot) = 0;
    virtual core::TattooLoadoutVerification verifyRollback(
        const core::TattooLoadoutRollbackSnapshot& snapshot) = 0;
};

[[nodiscard]] core::RestoreTattooLoadoutResult executeTattooLoadout(
    const core::RestoreTattooLoadoutRequest& request,
    ITattooLoadoutBackend& backend,
    TattooLoadoutRetryRegistry& retries);

[[nodiscard]] core::RestoreTattooLoadoutResult retryTattooLoadoutSynchronization(
    const core::RetryTattooLoadoutSynchronizationRequest& request,
    ITattooLoadoutBackend& backend,
    TattooLoadoutRetryRegistry& retries);

}  // namespace stui::runtime
