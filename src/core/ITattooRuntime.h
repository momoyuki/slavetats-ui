#pragma once

#include "core/TattooModels.h"

#include <string_view>

namespace stui::core {

class ITattooRuntime {
public:
    virtual ~ITattooRuntime() = default;

    [[nodiscard]] virtual bool apiAvailable() const noexcept = 0;
    [[nodiscard]] virtual bool jContainersReady() const noexcept = 0;
    virtual TattooQueryResult queryAvailable(std::string_view domain) = 0;
    virtual TattooSlotsResult querySlots(std::uint32_t actorFormId, TattooArea area) = 0;
    virtual ApplyTattooResult applyToSlot(const ApplyTattooRequest& request) = 0;
    virtual RemoveTattooResult removeFromSlot(const RemoveTattooRequest& request) = 0;
    virtual UpdateTattooAppearanceResult updateAppearance(
        const UpdateTattooAppearanceRequest& request) = 0;
    virtual SetTattooLockedResult setTattooLocked(
        const SetTattooLockedRequest& request) = 0;
};

}  // namespace stui::core
