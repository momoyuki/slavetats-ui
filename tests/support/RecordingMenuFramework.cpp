#include <string>

namespace {
std::string g_unformattedText;
}

extern "C" __declspec(dllexport) void igTextUnformatted(
    const char* text, const char* end) {
    g_unformattedText = end ? std::string(text, end) : std::string(text);
}

extern "C" __declspec(dllexport) const char* recordedUnformattedText() {
    return g_unformattedText.c_str();
}
