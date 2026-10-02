#pragma once

#include "core/ITattooRuntime.h"

#include <string_view>

namespace stui::core {

class SlaveTatsService {
public:
    explicit SlaveTatsService(ITattooRuntime& runtime) noexcept;

    TattooQueryResult queryAvailable(std::string_view domain);
    TattooSlotsResult querySlots(std::uint32_t actorFormId, TattooArea area);
    ApplyTattooResult applyToSlot(const ApplyTattooRequest& request);
    RemoveTattooResult removeFromSlot(const RemoveTattooRequest& request);
    UpdateTattooAppearanceResult updateAppearance(const UpdateTattooAppearanceRequest& request);
    SetTattooLockedResult setTattooLocked(const SetTattooLockedRequest& request);

private:
    ITattooRuntime& m_runtime;
};

}  // namespace stui::core
