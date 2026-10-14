#include "native/OfficialFlickPort.h"

#include "FLICK/FUCK_API.h"

namespace stui::native {
namespace {

class TattooBrowserTool final : public FUCK::ITool {
public:
    explicit TattooBrowserTool(FlickLauncher& launcher) : launcher_(launcher) {}

    [[nodiscard]] const char* Name() const override { return "Tattoo Browser"; }
    void Draw() override {
        FUCK::TextUnformatted("Open the SlaveTats UI tattoo browser.");
        if (FUCK::Button("Open Tattoo Browser")) {
            (void)launcher_.requestOpen();
        }
    }

private:
    FlickLauncher& launcher_;
};

}  // namespace

bool OfficialFlickPort::connect() noexcept {
    try {
        return FUCK::Connect("SlaveTatsUI");
    } catch (...) {
        return false;
    }
}

bool OfficialFlickPort::registerTool(FlickLauncher& launcher) noexcept {
    // FLICK retains this pointer for the process lifetime.
    static TattooBrowserTool tool(launcher);
    try {
        FUCK::RegisterTool(&tool);
        return true;
    } catch (...) {
        return false;
    }
}

void OfficialFlickPort::closeMenu() noexcept {
    try {
        FUCK::SetMenuOpen(false);
    } catch (...) {
        // The scheduled browser open remains independent of FLICK state.
    }
}

}  // namespace stui::native
