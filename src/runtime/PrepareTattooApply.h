#pragma once
#include "core/TattooModels.h"
#include "runtime/SlaveTatsAlpha.h"
#include <functional>
namespace stui::runtime {
struct TattooApplyTemplateBindings {
    std::function<int(int)> copy;
    std::function<bool(int,const char*,int)> setInt;
    std::function<bool(int,const char*,float)> setFloat;
    std::function<bool(int,const char*,const std::string&)> setString;
};
// Only top-level scalar/string appearance keys are changed; nested pack metadata
// stays shared and read-only. The binding retains the independent copy in a pool.
inline std::expected<int,core::ServiceError> prepareTattooApplyTemplate(
    int source,const core::ApplyTattooRequest& request,const TattooApplyTemplateBindings& b) {
    const auto fail=[] {return std::unexpected(core::ServiceError{
        core::ServiceErrorCode::applyFailed,"Could not prepare an isolated tattoo appearance"});};
    if (!source || !b.copy || !b.setInt || !b.setFloat || !b.setString) return fail();
    const int copy=b.copy(source);
    if (!copy || copy==source) return fail();
    if (!b.setInt(copy,"color",request.color) ||
        !b.setFloat(copy,"invertedAlpha",toSlaveTatsInvertedAlpha(request.alpha))) return fail();
    if (request.appearance) {
        const auto& a=*request.appearance;
        if ((a.glow && !b.setInt(copy,"glow",*a.glow)) ||
            (a.glossiness && !b.setFloat(copy,"glossiness",*a.glossiness)) ||
            (a.specularStrength && !b.setFloat(copy,"specularStrength",*a.specularStrength)) ||
            (a.emissiveMult && !b.setFloat(copy,"emissiveMult",*a.emissiveMult)) ||
            (a.glowTexture && !b.setString(copy,"glowTexture",*a.glowTexture)) ||
            (a.bump && !b.setString(copy,"bump",*a.bump))) return fail();
    }
    return copy;
}
} // namespace stui::runtime
