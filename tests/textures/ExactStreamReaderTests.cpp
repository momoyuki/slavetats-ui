#include "textures/ExactStreamReader.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class ExactBooleanStream {
public:
    explicit ExactBooleanStream(std::vector<std::uint8_t> bytes) : m_bytes(std::move(bytes)) {}

    bool read(std::uint8_t* output, std::uint32_t count) {
        requestedCount = count;
        if (fail || count != m_bytes.size()) return false;
        std::ranges::copy(m_bytes, output);
        return true;
    }

    bool fail{false};
    std::uint32_t requestedCount{0};

private:
    std::vector<std::uint8_t> m_bytes;
};

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void booleanReadResultStillReturnsEveryByte() {
    ExactBooleanStream stream({0x44, 0x44, 0x53, 0x20, 0x7C, 0x01});

    const auto result = stui::textures::readExactBytes(stream, 6);

    expect(result.has_value(), "expected exact stream read to succeed");
    expect(*result == std::vector<std::uint8_t>({0x44, 0x44, 0x53, 0x20, 0x7C, 0x01}), "expected all DDS bytes");
    expect(stream.requestedCount == 6, "expected one exact-size read");
}

void failedExactReadReturnsAnError() {
    ExactBooleanStream stream({0x44, 0x44, 0x53, 0x20});
    stream.fail = true;

    const auto result = stui::textures::readExactBytes(stream, 4);

    expect(!result.has_value(), "expected failed stream read to return an error");
}

void oversizedReadDoesNotTouchStream() {
    ExactBooleanStream stream({});
    const auto result = stui::textures::readExactBytes(stream, 128U * 1024 * 1024 + 1);
    expect(!result && stream.requestedCount == 0, "oversized DDS must fail before allocation/read");
}

template <class Test>
int run(std::string_view name, Test&& test) {
    try {
        std::forward<Test>(test)();
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

int main() {
    int failures = 0;
    failures += run("bounds archive allocation", oversizedReadDoesNotTouchStream);
    failures += run("boolean stream result returns every byte", booleanReadResultStillReturnsEveryByte);
    failures += run("failed exact read returns an error", failedExactReadReturnsAnError);
    return failures == 0 ? 0 : 1;
}
