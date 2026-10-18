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
                values[{key, index}] = value;
                return true;
            },
            .setFloat = [this](const CatalogPreviewVisualTarget&, std::uint16_t key,
                            std::uint8_t index, float value) {
                values[{key, index}] = value;
                return true;
            },
            .setString = [this](const CatalogPreviewVisualTarget&, std::uint16_t key,
                             std::uint8_t index, const std::string& value) {
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
    writesThenCapturesAllTypedProperties();
    rejectsIncompleteTypedSnapshot();
    rejectsNonFiniteMaterialScalars();
    stopsWritingAfterFirstFailure();
    std::cout << "SkeeNodePropertyAccessTests passed\n";
}
