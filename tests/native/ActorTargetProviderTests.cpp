#include "native/ActorTargetProvider.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using stui::core::ServiceErrorCode;
using stui::native::ActorTargetKind;
using stui::native::ActorTargetProviderBindings;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

ActorTargetProviderBindings validBindings() {
    return {
        .resolveCrosshairActor = [] { return reinterpret_cast<void*>(0x1); },
        .formId = [](void*) { return 0x1234U; },
        .is3DLoaded = [](void*) { return true; },
        .displayName = [](void*) { return std::string{"Lydia"}; },
    };
}

void resolvesLoadedCrosshairActorToCopiedTarget() {
    const auto result = stui::native::resolveCrosshairActorTarget(validBindings());

    expect(result.has_value(), "expected loaded crosshair Actor resolution");
    expect(result->kind == ActorTargetKind::crosshair, "expected Crosshair target kind");
    expect(result->formId == 0x1234U, "expected copied Actor form ID");
    expect(result->displayName == "Lydia", "expected copied Actor display name");
}

void rejectsMissingCrosshairActor() {
    auto bindings = validBindings();
    bindings.resolveCrosshairActor = [] { return static_cast<void*>(nullptr); };

    const auto result = stui::native::resolveCrosshairActorTarget(bindings);

    expect(!result.has_value(), "expected missing Actor rejection");
    expect(result.error().code == ServiceErrorCode::actorNotFound,
           "expected actor-not-found error for missing Actor");
}

void rejectsZeroFormId() {
    auto bindings = validBindings();
    bindings.formId = [](void*) { return 0U; };

    const auto result = stui::native::resolveCrosshairActorTarget(bindings);

    expect(!result.has_value(), "expected zero form ID rejection");
    expect(result.error().code == ServiceErrorCode::actorNotFound,
           "expected actor-not-found error for zero form ID");
}

void rejectsActorWithoutLoaded3D() {
    auto bindings = validBindings();
    bindings.is3DLoaded = [](void*) { return false; };

    const auto result = stui::native::resolveCrosshairActorTarget(bindings);

    expect(!result.has_value(), "expected unloaded Actor rejection");
    expect(result.error().code == ServiceErrorCode::actorNotFound,
           "expected actor-not-found error for unloaded Actor");
}

void rejectsMissingProviderCallable() {
    auto bindings = validBindings();
    bindings.displayName = {};

    const auto result = stui::native::resolveCrosshairActorTarget(bindings);

    expect(!result.has_value(), "expected incomplete provider binding rejection");
    expect(result.error().code == ServiceErrorCode::actorNotFound,
           "expected actor-not-found error for missing provider callable");
}

void normalizesEmptyDisplayName() {
    auto bindings = validBindings();
    bindings.displayName = [](void*) { return std::string{}; };

    const auto result = stui::native::resolveCrosshairActorTarget(bindings);

    expect(result.has_value(), "expected unnamed loaded Actor resolution");
    expect(result->displayName == "Unnamed Actor", "expected unnamed Actor fallback label");
}

}  // namespace

int main() {
    try {
        resolvesLoadedCrosshairActorToCopiedTarget();
        std::cout << "PASS resolves loaded crosshair Actor to copied target\n";
        rejectsMissingCrosshairActor();
        std::cout << "PASS rejects missing crosshair Actor\n";
        rejectsZeroFormId();
        std::cout << "PASS rejects zero form ID\n";
        rejectsActorWithoutLoaded3D();
        std::cout << "PASS rejects Actor without loaded 3D\n";
        rejectsMissingProviderCallable();
        std::cout << "PASS rejects missing provider callable\n";
        normalizesEmptyDisplayName();
        std::cout << "PASS normalizes empty display name\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
