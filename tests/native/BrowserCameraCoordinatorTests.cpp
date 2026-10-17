#include "native/BrowserCameraCoordinator.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void expect(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

class FakePort final : public stui::native::BrowserCameraPort {
public:
    stui::native::BrowserCameraMotion lastMotion;
    std::vector<std::uint32_t> begun;
    std::vector<bool> sides;
    int restored{};
    int moved{};
    int cleared{};
    bool loaded{true};
    bool begin(std::uint32_t formId, bool menuOnRight) override {
        begun.push_back(formId);
        sides.push_back(menuOnRight);
        return loaded;
    }
    bool isTargetLoaded(std::uint32_t) override { return loaded; }
    void rememberView(std::uint32_t, bool) override {}
    bool control(const stui::native::BrowserCameraMotion& motion) override {
        lastMotion = motion; ++moved; return true;
    }
    void restore() override { ++restored; }
    void clearRememberedViews() override { ++cleared; }
};
void panAndResetCommandsSurviveCoalescing() {
    FakePort port;
    std::vector<std::function<void()>> tasks;
    stui::native::BrowserCameraCoordinator coordinator(port,
        [&tasks](std::function<void()> task) { tasks.push_back(std::move(task)); });
    coordinator.setOpen(true);
    coordinator.setTarget(0x14);
    coordinator.control({.panX = 5, .panY = 10});
    coordinator.control({.panX = 7, .panY = 2});
    tasks.back()();
    expect(port.lastMotion.panX == 12 && port.lastMotion.panY == 12,
        "pan must coalesce through the game-thread boundary");
    coordinator.control({.orbitX = 20});
    coordinator.control({.reset = true});
    tasks.back()();
    expect(port.lastMotion.reset && port.lastMotion.orbitX == 0,
        "Reset must discard queued movement before restoring the opening pose");
}

void staleOpeningCannotFocusWrongActor() {
    FakePort port;
    std::vector<std::function<void()>> tasks;
    stui::native::BrowserCameraCoordinator coordinator(port,
        [&tasks](std::function<void()> task) { tasks.push_back(std::move(task)); });
    coordinator.setOpen(true);
    coordinator.setTarget(0x14);
    coordinator.setTarget(0x1234, false);
    coordinator.move(1.0F, 0.0F, 0.0F);
    expect(tasks.size() == 1, "changes should coalesce into one task");
    tasks.front()();
    expect(port.begun == std::vector<std::uint32_t>{0x1234},
        "only the latest explicit target should receive camera control");
    expect(port.sides == std::vector<bool>{false},
        "scheduled camera acquisition must receive the latest menu side");
    coordinator.move(5.0F, 2.0F, 0.0F);
    coordinator.setOpen(false);
    tasks.back()();
    expect(port.moved == 1 && port.restored == 1,
        "close should discard queued pointer movement and restore camera");
}

void periodicValidationRestoresUnloadedNpc() {
    FakePort port;
    std::vector<std::function<void()>> tasks;
    stui::native::BrowserCameraCoordinator coordinator(port,
        [&tasks](std::function<void()> task) { tasks.push_back(std::move(task)); });
    coordinator.setOpen(true);
    coordinator.setTarget(0x1234);
    coordinator.move(1.0F, 0.0F, 0.0F);
    tasks.back()();
    port.loaded = false;
    coordinator.refresh();
    tasks.back()();
    expect(port.restored == 1, "unloaded target should release camera on refresh");
    expect(port.begun == std::vector<std::uint32_t>{0x1234},
        "refresh must not silently focus Player");
}

void staleTargetAfterCloseCannotReopenOldNpc() {
    FakePort port;
    std::vector<std::function<void()>> tasks;
    stui::native::BrowserCameraCoordinator coordinator(port,
        [&tasks](std::function<void()> task) { tasks.push_back(std::move(task)); });
    coordinator.setOpen(true);
    coordinator.setTarget(0x1234);
    coordinator.move(1.0F, 0.0F, 0.0F);
    tasks.back()();
    coordinator.setOpen(false);
    coordinator.setTarget(0x1234);
    tasks.back()();
    coordinator.setOpen(true);
    tasks.back()();
    expect(port.begun == std::vector<std::uint32_t>{0x1234} && port.restored == 1,
        "late target update after close must not reopen the old NPC");
}

void refreshReassertsCameraStateWhileMenuIsOpen() {
    FakePort port;
    std::vector<std::function<void()>> tasks;
    stui::native::BrowserCameraCoordinator coordinator(port,
        [&tasks](std::function<void()> task) { tasks.push_back(std::move(task)); });
    coordinator.setOpen(true);
    coordinator.setTarget(0x14);
    coordinator.move(1.0F, 0.0F, 0.0F);
    tasks.back()();
    const auto beforeRefresh = port.moved;
    coordinator.refresh();
    tasks.back()();
    expect(port.moved == beforeRefresh + 1,
        "refresh must reassert camera state while the menu pauses gameplay");
}

void openingFramesPlayerOnTheAvailableSideOnce() {
    FakePort port;
    std::vector<std::function<void()>> tasks;
    stui::native::BrowserCameraCoordinator coordinator(port,
        [&tasks](std::function<void()> task) { tasks.push_back(std::move(task)); });
    coordinator.setOpen(true);
    coordinator.setTarget(0x14, true, 0.27F, {24.0F, -11.0F});
    tasks.back()();
    expect(port.lastMotion.focus == stui::native::BrowserCameraFocus::wholeBody &&
        port.lastMotion.frameCenterX == 0.27F && port.lastMotion.initialFrame &&
        port.lastMotion.initialPanX == 24.0F && port.lastMotion.initialPanY == -11.0F,
        "right-side menu must initially frame Player in the left control area");
    const auto movesAfterOpening = port.moved;
    coordinator.refresh();
    tasks.back()();
    expect(port.moved == movesAfterOpening + 1 &&
        port.lastMotion.focus == stui::native::BrowserCameraFocus::none &&
        port.lastMotion.initialPanX == 0.0F && port.lastMotion.initialPanY == 0.0F,
        "idle refresh must reassert the pose without repeating initial framing");
    coordinator.setOpen(false);
    tasks.back()();
    coordinator.setOpen(true);
    coordinator.setTarget(0x14, false, 0.73F);
    tasks.back()();
    expect(port.lastMotion.focus == stui::native::BrowserCameraFocus::wholeBody &&
        port.lastMotion.frameCenterX == 0.73F,
        "left-side menu must initially frame Player in the right control area");
}
void gameResetDropsQueuedMotionAndClearsRecall() {
    FakePort port;
    std::vector<std::function<void()>> tasks;
    stui::native::BrowserCameraCoordinator coordinator(port,
        [&tasks](std::function<void()> task) { tasks.push_back(std::move(task)); });
    coordinator.setOpen(true);
    coordinator.setTarget(0x14);
    tasks.back()();
    coordinator.control({.orbitX = 5});
    coordinator.resetGameSession();
    tasks.back()();
    expect(port.restored == 1 && port.cleared == 1 && port.moved == 1,
        "save-load reset must restore camera, clear recalled views and drop stale motion");
}

}  // namespace

int main() {
    try {
        staleOpeningCannotFocusWrongActor();
        periodicValidationRestoresUnloadedNpc();
        staleTargetAfterCloseCannotReopenOldNpc();
        refreshReassertsCameraStateWhileMenuIsOpen();
        openingFramesPlayerOnTheAvailableSideOnce();
        gameResetDropsQueuedMotionAndClearsRecall();
        panAndResetCommandsSurviveCoalescing();
        std::cout << "PASS browser camera coordinator\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
