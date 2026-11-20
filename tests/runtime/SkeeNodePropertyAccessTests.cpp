#include "runtime/SkeeNodePropertyAccess.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <tuple>
#include <variant>

namespace {

using stui::runtime::CatalogPreviewVisualSnapshot;
using stui::runtime::CatalogPreviewVisualTarget;
using stui::runtime::SkeeNodePropertyAccess;
using stui::runtime::SkeeNodePropertyBindings;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using Key = std::pair<std::uint16_t, std::uint8_t>;
using Value = std::variant<std::int32_t, float, std::string>;

struct FakeProperties {
    std::map<Key, Value> values;
    std::map<Key, int> writes;

    SkeeNodePropertyBindings bindings() {
        return {
            .getInt = [this](const CatalogPreviewVisualTarget&, std::uint16_t key,
                          std::uint8_t index) -> std::optional<std::int32_t> {
                const auto found = values.find({key, index});
                if (found == values.end()) return std::nullopt;
                const auto* value = std::get_if<std::int32_t>(&found->second);
                return value ? std::optional(*value) : std::nullopt;
            },
            .getFloat = [this](const CatalogPreviewVisualTarget&, std::uint16_t key,
                            std::uint8_t index) -> std::optional<float> {
                const auto found = values.find({key, index});
                if (found == values.end()) return std::nullopt;
                const auto* value = std::get_if<float>(&found->second);
                return value ? std::optional(*value) : std::nullopt;
            },
            .getString = [this](const CatalogPreviewVisualTarget&, std::uint16_t key,
                             std::uint8_t index) -> std::optional<std::string> {
                const auto found = values.find({key, index});
                if (found == values.end()) return std::nullopt;
                const auto* value = std::get_if<std::string>(&found->second);
                return value ? std::optional(*value) : std::nullopt;
            },
            .setInt = [this](const CatalogPreviewVisualTarget&, std::uint16_t key,
                          std::uint8_t index, std::int32_t value) {
                ++writes[{key, index}];
                values[{key, index}] = value;
                return true;
            },
            .setFloat = [this](const CatalogPreviewVisualTarget&, std::uint16_t key,
                            std::uint8_t index, float value) {
                ++writes[{key, index}];
                values[{key, index}] = value;
                return true;
            },
            .setString = [this](const CatalogPreviewVisualTarget&, std::uint16_t key,
                             std::uint8_t index, const std::string& value) {
                ++writes[{key, index}];
                values[{key, index}] = value;
                return true;
            },
        };
    }
};

CatalogPreviewVisualSnapshot expectedSnapshot() {
    return {
        .diffuse = "d.dds",
        .normal = "n.dds",
        .glowTexture = "g.dds",
        .tint = 0x102030,
        .alpha = 0.7F,
        .glow = 0x405060,
        .emissiveMult = 1.2F,
        .glossiness = 2.3F,
        .specularStrength = 3.4F,
    };
}

void writesThenCapturesAllTypedProperties() {
    FakeProperties fake;
    SkeeNodePropertyAccess access(fake.bindings());
    const CatalogPreviewVisualTarget target{1, 2, 3, 4};
    const auto expected = expectedSnapshot();

    expect(access.write(target, expected), "write should set every typed property");
    const auto captured = access.capture(target);
    expect(captured && *captured == expected,
        "capture should recover all texture and scalar properties");
}

void writesOnlyChangedPropertiesAndRecoversPartialFailure() {
    FakeProperties fake;
    const CatalogPreviewVisualTarget target{1, 2, 3, 4};
    auto bindings = fake.bindings();
    SkeeNodePropertyAccess access(bindings);
    auto desired = expectedSnapshot();
    expect(access.write(target, desired), "initial full write succeeds");
    fake.writes.clear();
    expect(access.write(target, desired) && fake.writes.empty(), "identical material must issue no writes");
    desired.alpha = 0.25F;
    desired.glow = 0;
    desired.emissiveMult = 0.0F;
    expect(access.write(target, desired), "scalar-only update succeeds");
    expect(fake.writes.size() == 3 && !fake.writes.contains({9, 0}) &&
        !fake.writes.contains({9, 1}) && !fake.writes.contains({9, 2}),
        "scalar edits must not reset any texture property");
    fake.writes.clear();
    desired.diffuse = "replacement.dds";
    desired.glossiness = 42.0F;
    bindings.setFloat = [](const CatalogPreviewVisualTarget&, std::uint16_t, std::uint8_t, float) {
        return false;
    };
    expect(!SkeeNodePropertyAccess(bindings).write(target, desired), "partial failure must be reported");
    expect(fake.values[{9, 0}] == Value(desired.diffuse), "texture write preceded scalar failure");
    fake.writes.clear();
    expect(access.write(target, desired), "retry recovers partial property writes");
    expect(fake.writes.size() == 1 && fake.writes.contains({2, 0xFF}),
        "retry must inspect live properties and write only failed scalar");
    expect(access.capture(target) == desired, "all fields must match after recovery");
}

void rejectsIncompleteTypedSnapshot() {
    FakeProperties fake;
    SkeeNodePropertyAccess access(fake.bindings());
    const CatalogPreviewVisualTarget target{1, 2, 3, 4};
    expect(access.write(target, expectedSnapshot()), "setup write should succeed");
    fake.values.erase({9, 2});

    expect(!access.capture(target), "missing glow texture must reject capture");
}

void rejectsNonFiniteMaterialScalars() {
    FakeProperties fake;
    SkeeNodePropertyAccess access(fake.bindings());
    const CatalogPreviewVisualTarget target{1, 2, 3, 4};
    expect(access.write(target, expectedSnapshot()), "setup write should succeed");
    for (const Key key : {Key{8, 0xFF}, Key{1, 0xFF}, Key{2, 0xFF}, Key{3, 0xFF}}) {
        fake.values[key] = std::numeric_limits<float>::quiet_NaN();
        expect(!access.capture(target), "NaN material scalar must reject capture");
        fake.values[key] = std::numeric_limits<float>::infinity();
        expect(!access.capture(target), "infinite material scalar must reject capture");
        fake.values[key] = -std::numeric_limits<float>::infinity();
        expect(!access.capture(target), "negative infinity must reject capture");
        expect(access.write(target, expectedSnapshot()), "restore finite fixture values");
    }

    auto invalid = expectedSnapshot();
    invalid.emissiveMult = std::numeric_limits<float>::quiet_NaN();
    expect(!access.write(target, invalid), "non-finite paint must be rejected before any setter");
    expect(access.capture(target) == expectedSnapshot(),
        "rejected paint must preserve the original material");
}

void readsDiffuseWithoutOtherProperties() {
    FakeProperties fake;
    fake.values[{9, 0}] = std::string("textures/example.dds");
    SkeeNodePropertyAccess access(fake.bindings());
    const CatalogPreviewVisualTarget target{1, 2, 3, 4, "Body [Ovl3]"};
    expect(access.captureDiffuse(target) == std::optional<std::string>("textures/example.dds"),
        "diffuse lookup must not require unrelated material properties");
    fake.values.erase({9, 0});
    expect(!access.captureDiffuse(target), "missing diffuse must remain unavailable");
}
void stopsWritingAfterFirstFailure() {
    FakeProperties fake;
    auto bindings = fake.bindings();
    int writes{};
    bindings.setFloat = [&writes](const CatalogPreviewVisualTarget&, std::uint16_t,
                            std::uint8_t, float) {
        ++writes;
        return false;
    };
    SkeeNodePropertyAccess access(std::move(bindings));

    expect(!access.write({1, 2, 3, 4}, expectedSnapshot()),
        "property write failure should be reported");
    expect(writes == 1, "write should stop at the first failed property");
}

}  // namespace

int main() {
    writesOnlyChangedPropertiesAndRecoversPartialFailure();
    readsDiffuseWithoutOtherProperties();
    writesThenCapturesAllTypedProperties();
    rejectsIncompleteTypedSnapshot();
    rejectsNonFiniteMaterialScalars();
    stopsWritingAfterFirstFailure();
    std::cout << "SkeeNodePropertyAccessTests passed\n";
}
