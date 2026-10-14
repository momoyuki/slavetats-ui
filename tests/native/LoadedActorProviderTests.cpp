#include "native/LoadedActorProvider.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using stui::native::ActorTarget;
using stui::native::ActorTargetKind;
using stui::native::LoadedActorCandidate;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

LoadedActorCandidate candidate(std::uint32_t formId, float distance, std::string name) {
    return {
        .target = ActorTarget{ActorTargetKind::explicitNpc, formId, std::move(name)},
        .distance = distance,
    };
}

void sortsByNameThenFormId() {
    const auto result = stui::native::normalizeLoadedActorCandidates({
        candidate(0x30, 25.0F, "Zelda"),
        candidate(0x20, 1.0F, "alice"),
        candidate(0x10, 100.0F, "Alice"),
        candidate(0x40, 2.0F, "Bob"),
    });

    expect(result.size() == 4, "expected all valid loaded Actors");
    expect(result[0].target.formId == 0x10 && result[1].target.formId == 0x20,
        "expected case-insensitive name ordering with form-ID tie-break");
    expect(result[2].target.formId == 0x40 && result[3].target.formId == 0x30,
        "expected remaining names in deterministic order");
}

void removesDuplicatesAndCapsAtSixtyFour() {
    std::vector<LoadedActorCandidate> input;
    input.push_back(candidate(0x99, 1.0F, "Aardvark"));
    input.push_back(candidate(0x99, 2.0F, "Aardvark later"));
    for (std::uint32_t index = 0; index < 70; ++index) {
        input.push_back(candidate(0x100 + index, static_cast<float>(index),
            "Actor " + std::to_string(index)));
    }

    const auto result = stui::native::normalizeLoadedActorCandidates(std::move(input));

    expect(result.size() == 64, "expected at most sixty-four explicit NPCs");
    const auto duplicate = std::count_if(result.begin(), result.end(), [](const auto& entry) {
        return entry.target.formId == 0x99;
    });
    expect(duplicate == 1, "expected duplicate identity to be retained once");
}

void rejectsPlayerInvalidIdentityAndDistance() {
    const auto result = stui::native::normalizeLoadedActorCandidates({
        {.target = ActorTarget{ActorTargetKind::player, 0x14, "Player"}, .distance = 1.0F},
        candidate(0, 2.0F, "Zero"),
        candidate(0x20, -1.0F, "Negative"),
        candidate(0x21, std::numeric_limits<float>::quiet_NaN(), "NaN"),
        candidate(0x23, 4.0F, ""),
        candidate(0x22, 5.0F, "Valid"),
    });

    expect(result.size() == 2 && result[0].target.formId == 0x23 &&
            result[0].target.displayName == "Unnamed Actor" &&
            result[1].target.formId == 0x22,
        "expected only valid explicit NPC candidates with normalized names");
}

}  // namespace

int main() {
    try {
        sortsByNameThenFormId();
        std::cout << "PASS sorts loaded Actors by name and form ID\n";
        removesDuplicatesAndCapsAtSixtyFour();
        std::cout << "PASS removes duplicate identities and caps at sixty-four\n";
        rejectsPlayerInvalidIdentityAndDistance();
        std::cout << "PASS rejects Player and invalid candidates\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
