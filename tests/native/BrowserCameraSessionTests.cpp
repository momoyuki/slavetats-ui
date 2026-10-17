#include "native/BrowserCameraSession.h"

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void expect(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

class FakeCamera final : public stui::native::BrowserCameraPort {
public:
    bool allow{true};
    bool loaded{true};
    bool validMove{true};
    std::vector<std::uint32_t> begun;
    std::vector<bool> sides;
    int restored{};
    int moved{};
    int remembered{};
    void rememberView(std::uint32_t, bool) override { ++remembered; }

    bool begin(std::uint32_t formId, bool menuOnRight) override {
        begun.push_back(formId);
        sides.push_back(menuOnRight);
        return allow && loaded;
    }
    bool isTargetLoaded(std::uint32_t) override { return loaded; }
    bool control(const stui::native::BrowserCameraMotion&) override {
        if (!validMove) { ++restored; return false; }
        ++moved;
        return true;
    }
    void restore() override { ++restored; }
};

void targetSwitchAndUnloadNeverFallbackToPlayer() {
    FakeCamera port;
    stui::native::BrowserCameraSession session(port);
    session.setTarget(0x14);
    session.move(12.0F, 0.0F, 0.0F);
    expect(session.active() && port.begun == std::vector<std::uint32_t>{0x14},
        "Player gesture should acquire camera");
    expect(port.moved == 1, "active camera should move");
    session.setTarget(0x1234);
    session.move(4.0F, 0.0F, 0.0F);
    expect(port.restored == 1 && port.begun.back() == 0x1234,
        "NPC target should restore old view before binding NPC");
    port.loaded = false;
    session.move(10.0F, 0.0F, 0.0F);
    expect(!session.active() && port.restored == 2 && port.moved == 2,
        "unloaded NPC should restore and reject movement");
    session.setTarget(0x1234);
    expect(!session.active() && port.begun.back() == 0x1234,
        "unloaded NPC must not focus Player");
    session.close();
    expect(port.restored == 2, "closed inactive camera should not restore twice");
}

void idlePlayerViewLeavesSmoothCamInControlUntilGesture() {
    FakeCamera port;
    stui::native::BrowserCameraSession session(port);
    session.setTarget(0x14);
    session.move(0.0F, 0.0F, 0.0F);
    session.setTarget(0x14);
    expect(!session.active() && port.begun.empty() && port.moved == 0,
        "opening and idle refresh must not freeze SmoothCam's Player framing");
    expect(port.remembered == 1,
        "opening view must be captured once without taking camera ownership");
    session.move(8.0F, 0.0F, 0.0F);
    expect(session.active() && port.begun.size() == 1 && port.moved == 1,
        "first actual gesture must capture the current Player view once");
    session.move(0.0F, 0.0F, 0.0F);
    expect(port.begun.size() == 1 && port.moved == 2,
        "active camera must still reassert its pose while gameplay is paused");
    session.close();
    session.setTarget(0x14);
    session.close();
    expect(port.restored == 1 && port.begun.size() == 1,
        "reopening without input must not acquire or restore camera again");
    session.setTarget(0x14, false);
    session.move(1.0F, 0.0F, 0.0F);
    expect(port.sides.size() == 2 && !port.sides.back(),
        "left menu must reach the native camera when its first gesture begins");
    session.setTarget(0x14, true);
    expect(!session.active() && port.restored == 2,
        "changing menu side must end the old framing session before recapturing");
}

void refusedControlNeverMovesCamera() {
    FakeCamera port;
    port.allow = false;
    stui::native::BrowserCameraSession session(port);
    session.setTarget(0x14);
    session.move(10.0F, 5.0F, 1.0F);
    expect(!session.active() && port.moved == 0 && port.restored == 0,
        "refused camera ownership must not change camera state");
}

void invalidatedPortCanAcquireOnNextGesture() {
    FakeCamera port;
    stui::native::BrowserCameraSession session(port);
    session.setTarget(0x14);
    session.move(1, 0, 0);
    port.validMove = false;
    session.move(1, 0, 0);
    expect(!session.active() && port.restored == 1,
        "native state/root invalidation must reach the session without a second restore");
    port.validMove = true;
    session.move(0, 0, 0);
    expect(port.begun.size() == 1, "idle refresh must not reacquire after invalidation");
    session.move(1, 0, 0);
    expect(session.active() && port.begun == std::vector<std::uint32_t>{0x14, 0x14},
        "next gesture must reacquire the exact requested Actor after state/root recovery");
    session.close();
    expect(port.restored == 2, "recovered session must restore exactly once on close");
}

}  // namespace

int main() {
    try {
        idlePlayerViewLeavesSmoothCamInControlUntilGesture();
        targetSwitchAndUnloadNeverFallbackToPlayer();
        refusedControlNeverMovesCamera();
        invalidatedPortCanAcquireOnNextGesture();
        std::cout << "PASS browser camera session\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
