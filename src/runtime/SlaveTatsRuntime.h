#pragma once

#include "core/ITattooRuntime.h"
#include "runtime/OverlaySlotConfiguration.h"
#include "JContainers/jc_interface.h"
#include "SlaveTatsNG_Interface.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace stui::runtime {

using AppliedTattooHandleQueryResult =
    std::expected<std::vector<std::int32_t>, core::ServiceError>;

struct SlaveTatsAppearanceBindings {
    std::function<void*(std::uint32_t)> resolveActor;
    std::function<AppliedTattooHandleQueryResult(void*)> queryAppliedTattooHandles;
    std::function<void(std::int32_t, const char*, std::int32_t)> setTattooInt;
    std::function<std::int32_t(std::int32_t, const char*, std::int32_t)> getTattooInt;
    std::function<void(std::int32_t, const char*, float)> setTattooFloat;
    std::function<float(std::int32_t, const char*, float)> getTattooFloat;
    std::function<void(void*, const char*, std::int32_t)> setActorInt;
    std::function<std::int32_t(void*, const char*, std::int32_t)> getActorInt;
    std::function<bool(void*, bool)> synchronizeTattoos;
};

class SlaveTatsRuntime final : public core::ITattooRuntime {
public:
    SlaveTatsRuntime() = default;
    explicit SlaveTatsRuntime(SlaveTatsAppearanceBindings appearanceBindings) :
        m_appearanceBindings(std::move(appearanceBindings)) {}

    void bindSlaveTats(const slavetats::interface::Addresses* api) noexcept;
    void noteSlaveTatsVersionMismatch(std::uint32_t version) noexcept;
    [[nodiscard]] bool bindJContainers(const jc::root_interface* root);

    [[nodiscard]] bool apiAvailable() const noexcept override;
    [[nodiscard]] bool jContainersReady() const noexcept override;
    [[nodiscard]] std::uint32_t apiVersion() const noexcept;
    [[nodiscard]] const slavetats::interface::Addresses* api() const noexcept;
    core::TattooQueryResult queryAvailable(std::string_view domain) override;
    core::TattooSlotsResult querySlots(std::uint32_t actorFormId, core::TattooArea area) override;
    core::ApplyTattooResult applyToSlot(const core::ApplyTattooRequest& request) override;
    core::RemoveTattooResult removeFromSlot(const core::RemoveTattooRequest& request) override;
    core::UpdateTattooAppearanceResult updateAppearance(
        const core::UpdateTattooAppearanceRequest& request) override;
    core::SetTattooLockedResult setTattooLocked(
        const core::SetTattooLockedRequest& request) override;

private:
    const slavetats::interface::Addresses* m_api{nullptr};
    std::uint32_t m_apiVersion{0};
    bool m_jContainersReady{false};
    OverlaySlotConfiguration m_slotConfiguration;
    std::optional<SlaveTatsAppearanceBindings> m_appearanceBindings;
};

}  // namespace stui::runtime
