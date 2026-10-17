#include "runtime/CatalogPreviewVisualSession.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

namespace {

using stui::core::TattooArea;
using stui::core::TattooEntry;
using stui::runtime::CatalogPreviewVisualContext;
using stui::runtime::CatalogPreviewVisualSession;
using stui::runtime::CatalogPreviewVisualSnapshot;
using stui::runtime::CatalogPreviewVisualTarget;
using stui::runtime::CatalogPreviewVisualBindings;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

struct FakeVisual {
    CatalogPreviewVisualTarget target{.actor = 1, .root = 2, .geometry = 3, .material = 4};
    CatalogPreviewVisualSnapshot values{
        .diffuse = "original.dds",
        .normal = "original_n.dds",
        .glowTexture = "original_g.dds",
        .tint = 0x112233,
        .alpha = 0.75F,
        .glow = 0x445566,
        .emissiveMult = 1.5F,
        .glossiness = 2.5F,
        .specularStrength = 3.5F,
    };
    int writes{};

    CatalogPreviewVisualBindings bindings() {
        return {
            .resolve = [this](const CatalogPreviewVisualContext&) {
                return std::optional(target);
            },
            .capture = [this](const CatalogPreviewVisualTarget&) {
                return std::optional(values);
            },
            .write = [this](const CatalogPreviewVisualTarget&, const CatalogPreviewVisualSnapshot& value) {
                ++writes;
                values = value;
                return true;
            },
        };
    }
};

TattooEntry candidate() {
    return TattooEntry{
        .texturePath = "preview.dds",
        .color = 0xABCDEF,
        .alpha = 0.6F,
        .glow = 0x010203,
        .glossiness = 6.0F,
        .specularStrength = 7.0F,
        .bump = "preview_n.dds",
        .glowTexture = "preview_g.dds",
        .emissiveMult = 8.0F,
    };
}

void paintsAndRestoresExactOriginal() {
    FakeVisual fake;
    const auto original = fake.values;
    CatalogPreviewVisualSession session(fake.bindings());
    const CatalogPreviewVisualContext context{0x14, 3, TattooArea::body, 2};

    expect(session.begin(context), "begin should capture a compatible target");
    expect(session.paint(context, candidate()), "paint should write the candidate");
    expect(fake.values.diffuse == "textures\\actors\\character\\slavetats\\preview.dds" &&
            fake.values.normal == "textures\\actors\\character\\slavetats\\preview_n.dds" &&
            fake.values.glowTexture == "textures\\actors\\character\\slavetats\\preview_g.dds",
        "paint should resolve pack-relative paths before writing texture layers");
    expect(fake.values.tint == 0xABCDEF && fake.values.alpha == 0.6F &&
            fake.values.glow == 0x010203 && fake.values.emissiveMult == 8.0F &&
            fake.values.glossiness == 6.0F && fake.values.specularStrength == 7.0F,
        "paint should update all scalar values");
    expect(session.restore(context), "restore should succeed");
    expect(fake.values == original, "restore should write the exact captured snapshot");
}

void rejectsInvalidPackTextureBeforeWriting() {
    FakeVisual fake;
    CatalogPreviewVisualSession session(fake.bindings());
    const CatalogPreviewVisualContext context{0x14, 3, TattooArea::body, 2};
    expect(session.begin(context), "begin should capture target");
    auto invalid = candidate();
    invalid.texturePath = "..\\outside.dds";
    expect(!session.paint(context, invalid), "invalid pack path must be rejected");
    expect(fake.writes == 0, "invalid pack path must not change the Actor");
}

void rejectsRebuiltMaterialBeforeWriting() {
    FakeVisual fake;
    CatalogPreviewVisualSession session(fake.bindings());
    const CatalogPreviewVisualContext context{0x14, 8, TattooArea::body, 1};
    expect(session.begin(context), "begin should capture target");
    fake.target.material = 99;

    expect(!session.paint(context, candidate()), "paint should reject a replaced material");
    expect(fake.writes == 0, "replaced material must not be written");
}

void rejectsStaleActorGenerationAndSlot() {
    FakeVisual fake;
    CatalogPreviewVisualSession session(fake.bindings());
    const CatalogPreviewVisualContext context{0x123, 11, TattooArea::hands, 4};
    expect(session.begin(context), "begin should capture target");

    auto stale = context;
    ++stale.generation;
    expect(!session.paint(stale, candidate()), "stale generation should be rejected");
    stale = context;
    ++stale.slot;
    expect(!session.restore(stale), "wrong slot should be rejected");
    expect(fake.writes == 0, "stale contexts must not write");
}

void failedReadbackLeavesRestoreObligation() {
    FakeVisual fake;
    auto bindings = fake.bindings();
    bool corruptReadback{};
    bindings.capture = [&fake, &corruptReadback](const CatalogPreviewVisualTarget&) {
        auto value = fake.values;
        if (corruptReadback) value.alpha = 0.1F;
        return std::optional(value);
    };
    const auto original = fake.values;
    CatalogPreviewVisualSession session(std::move(bindings));
    const CatalogPreviewVisualContext context{0x14, 12, TattooArea::feet, 0};
    expect(session.begin(context), "begin should capture target");
    corruptReadback = true;

    expect(!session.paint(context, candidate()), "mismatched readback should fail paint");
    expect(session.restorePending(), "a failed paint must retain cleanup obligation");
    corruptReadback = false;
    expect(session.restore(context), "restore should remain retryable");
    expect(fake.values == original && !session.restorePending(),
        "successful restore should clear cleanup obligation");
}

void partialWriteFailureKeepsRestoreRetryable() {
    FakeVisual fake;
    const auto original = fake.values;
    auto bindings = fake.bindings();
    bindings.write = [&fake](const CatalogPreviewVisualTarget&,
                         const CatalogPreviewVisualSnapshot& value) {
        ++fake.writes;
        if (fake.writes <= 2) {
            fake.values.diffuse = value.diffuse;
            return false;
        }
        fake.values = value;
        return true;
    };
    CatalogPreviewVisualSession session(std::move(bindings));
    const CatalogPreviewVisualContext context{0x14, 14, TattooArea::body, 0};
    expect(session.begin(context), "begin should capture the original material");
    expect(!session.paint(context, candidate()), "partial write should fail paint");
    expect(fake.values != original && session.restorePending(),
        "partial write must retain a restore obligation");
    expect(!session.restore(context) && session.restorePending(),
        "failed restore must remain retryable");
    expect(session.restore(context), "retry should restore the original material");
    expect(fake.values == original && !session.restorePending(),
        "successful retry should clear the restore obligation");
}

void clearRestoresWithoutEndingSession() {
    FakeVisual fake;
    const auto original = fake.values;
    CatalogPreviewVisualSession session(fake.bindings());
    const CatalogPreviewVisualContext context{0x14, 13, TattooArea::body, 0};
    expect(session.begin(context), "begin should capture target");
    expect(session.clear(context), "clearing an untouched preview should succeed");
    expect(fake.writes == 0, "untouched preview should not write material");
    expect(session.paint(context, candidate()), "first hover should paint");
    expect(session.clear(context), "hover exit should restore original");
    expect(fake.values == original && !session.restorePending(),
        "hover exit should clear visual and cleanup state");
    expect(session.paint(context, candidate()), "next hover should reuse the session");
    expect(session.restore(context), "closing should restore and end the session");
    expect(fake.values == original, "close should leave original material");
    expect(!session.paint(context, candidate()), "ended session must reject later paint");
}

}  // namespace

int main() {
    paintsAndRestoresExactOriginal();
    rejectsInvalidPackTextureBeforeWriting();
    rejectsRebuiltMaterialBeforeWriting();
    rejectsStaleActorGenerationAndSlot();
    failedReadbackLeavesRestoreObligation();
    partialWriteFailureKeepsRestoreRetryable();
    clearRestoresWithoutEndingSession();
    std::cout << "CatalogPreviewVisualSessionTests passed\n";
}
