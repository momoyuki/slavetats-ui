#include "native/NearbyActorProvider.h"

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
using stui::native::NearbyActorCandidate;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

NearbyActorCandidate candidate(std::uint32_t formId, float distance, std::string name) {
    return {
        .target = ActorTarget{ActorTargetKind::nearby, formId, std::move(name)},
        .distance = distance,
    };
}

void sortsNearestFirstWithFormIdTieBreak() {
    auto result = stui::native::normalizeNearbyActorCandidates({
        candidate(0x30, 100.0F, "Far"),
        candidate(0x20, 25.0F, "Tie B"),
        candidate(0x10, 25.0F, "Tie A"),
    });

    expect(result.size() == 3, "expected all valid candidates");
    expect(result[0].target.formId == 0x10 && result[1].target.formId == 0x20 &&
               result[2].target.formId == 0x30,
        "expected nearest-first ordering with form-ID tie-break");
}

void removesDuplicateFormIdsAndCapsAtTen() {
    std::vector<NearbyActorCandidate> input;
    input.push_back(candidate(0x99, 1.0F, "Duplicate first"));
    input.push_back(candidate(0x99, 2.0F, "Duplicate second"));
    for (std::uint32_t index = 0; index < 12; ++index) {
        input.push_back(candidate(0x100 + index, 10.0F + static_cast<float>(index), "Actor"));
    }

    const auto result = stui::native::normalizeNearbyActorCandidates(std::move(input));

    expect(result.size() == 10, "expected at most ten nearby Actors");
    expect(result[0].target.formId == 0x99, "expected nearest duplicate to be retained");
    for (std::size_t index = 1; index < result.size(); ++index) {
        expect(result[index].target.formId != 0x99, "expected duplicate form ID removed");
    }
}

void rejectsPlayerZeroAndInvalidDistances() {
    const auto result = stui::native::normalizeNearbyActorCandidates({
        {.target = ActorTarget{ActorTargetKind::player, 0x14, "Player"}, .distance = 1.0F},
        candidate(0, 2.0F, "Zero"),
        candidate(0x20, -1.0F, "Negative"),
        candidate(0x21, std::numeric_limits<float>::quiet_NaN(), "NaN"),
        candidate(0x22, 5.0F, "Valid"),
    });

    expect(result.size() == 1 && result[0].target.formId == 0x22,
        "expected Player and invalid candidates to be rejected");
}

}  // namespace

int main() {
    try {
        sortsNearestFirstWithFormIdTieBreak();
        std::cout << "PASS sorts nearest first with form ID tie-break\n";
        removesDuplicateFormIdsAndCapsAtTen();
        std::cout << "PASS removes duplicates and caps at ten\n";
        rejectsPlayerZeroAndInvalidDistances();
        std::cout << "PASS rejects Player zero and invalid distances\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
