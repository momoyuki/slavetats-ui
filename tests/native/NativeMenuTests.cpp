#include "native/NativeMenu.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class FakeMenuFrameworkPort final : public stui::native::MenuFrameworkPort {
public:
    [[nodiscard]] bool available() const noexcept override { return isAvailable; }
    [[nodiscard]] float version() const noexcept override { return frameworkVersion; }

    [[nodiscard]] stui::native::RegistrationResult setSection(
        std::string_view value) override {
        section = value;
        return {};
    }

    [[nodiscard]] stui::native::RegistrationResult addSectionItem(
        std::string_view value, stui::native::MenuCallback callback) override {
        itemPath = value;
        itemCallback = callback;
        ++itemRegistrations;
        return {};
    }

    [[nodiscard]] std::expected<stui::native::MenuWindow, stui::native::MenuRegistrationError>
    addWindow(stui::native::MenuCallback callback, bool shouldPauseGame) override {
        windowCallback = callback;
        pauseGame = shouldPauseGame;
        ++windowRegistrations;
        if (windowError) {
            return std::unexpected(*windowError);
        }
        return windowToken;
    }

    void setWindowOpen(stui::native::MenuWindow, bool value) noexcept override { open = value; }
    [[nodiscard]] bool isWindowOpen(stui::native::MenuWindow) const noexcept override {
        return open;
    }

    std::string section;
    std::string itemPath;
    stui::native::MenuCallback itemCallback{};
    stui::native::MenuCallback windowCallback{};
    int itemRegistrations{};
    int windowRegistrations{};
    stui::native::MenuWindow windowToken{1};
    std::optional<stui::native::MenuRegistrationError> windowError;
    float frameworkVersion{3.13F};
    bool isAvailable{true};
    bool pauseGame{true};
    bool open{};
};

void registersOneBlockingTattooBrowser() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu;

    const auto result = menu.registerMenu(port);

    expect(result.has_value(), "expected native menu registration");
    expect(port.section == "SlaveTatsUI", "expected owning section");
    expect(port.itemPath == "Tattoo Browser", "expected browser item");
    expect(port.itemRegistrations == 1, "expected one browser item");
    expect(port.windowRegistrations == 1, "expected one native window");
    expect(port.pauseGame, "expected window to block gameplay input");
}

void repeatedRegistrationIsIdempotent() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu;

    expect(menu.registerMenu(port).has_value(), "expected initial registration");
    expect(menu.registerMenu(port).has_value(), "expected repeated registration success");
    expect(port.itemRegistrations == 1, "expected no duplicate browser item");
    expect(port.windowRegistrations == 1, "expected no duplicate native window");
}

void sectionRenderingDoesNotOpenBrowserWithoutRequest() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu;

    expect(menu.registerMenu(port).has_value(), "expected registration");
    expect(port.itemCallback != nullptr, "expected section callback");
    port.itemCallback();

    expect(!port.open, "expected section rendering not to open browser implicitly");
}

void explicitSectionRequestOpensBrowser() {
    FakeMenuFrameworkPort port;
    bool openRequested = false;
    stui::native::NativeMenu menu({}, [&openRequested] { return openRequested; });
    expect(menu.registerMenu(port).has_value(), "expected registration");
    port.itemCallback();
    expect(!menu.isOpen(), "expected idle launcher not to open browser");

    openRequested = true;
    port.itemCallback();

    expect(menu.isOpen(), "expected explicit launcher request to open browser");
}

void unavailableFrameworkDoesNotRegisterAnything() {
    FakeMenuFrameworkPort port;
    port.isAvailable = false;
    stui::native::NativeMenu menu;

    const auto result = menu.registerMenu(port);

    expect(!result, "expected unavailable registration failure");
    expect(result.error() == stui::native::MenuRegistrationError::unavailable,
           "expected unavailable error");
    expect(port.itemRegistrations == 0 && port.windowRegistrations == 0,
           "expected no registration against unavailable framework");
}

void unsupportedMajorVersionDoesNotRegisterAnything() {
    FakeMenuFrameworkPort port;
    port.frameworkVersion = 4.0F;
    stui::native::NativeMenu menu;

    const auto result = menu.registerMenu(port);

    expect(!result, "expected unsupported version failure");
    expect(result.error() == stui::native::MenuRegistrationError::unsupportedVersion,
           "expected unsupported version error");
    expect(port.itemRegistrations == 0 && port.windowRegistrations == 0,
           "expected no registration against unsupported framework");
}

void zeroWindowTokenDoesNotPublishBrowserItem() {
    FakeMenuFrameworkPort port;
    port.windowToken = 0;
    stui::native::NativeMenu menu;

    const auto result = menu.registerMenu(port);

    expect(!result, "expected zero window token rejection");
    expect(result.error() == stui::native::MenuRegistrationError::windowCreationFailed,
           "expected window creation error");
    expect(port.itemRegistrations == 0, "expected no browser item without a window");
}

void destroyedMenuMakesSectionCallbackANoOp() {
    FakeMenuFrameworkPort port;
    stui::native::MenuCallback callback{};
    {
        stui::native::NativeMenu menu;
        expect(menu.registerMenu(port).has_value(), "expected registration");
        callback = port.itemCallback;
    }
    port.open = false;

    callback();

    expect(!port.open, "expected callback not to access destroyed menu");
}

void windowRegistrationPreservesNamedFrameworkError() {
    FakeMenuFrameworkPort port;
    port.windowError = stui::native::MenuRegistrationError::missingExport;
    stui::native::NativeMenu menu;

    const auto result = menu.registerMenu(port);

    expect(!result, "expected window registration failure");
    expect(result.error() == stui::native::MenuRegistrationError::missingExport,
           "expected named framework error to be preserved");
    expect(port.itemRegistrations == 0, "expected no browser item after window failure");
}

void renderExceptionIsContainedAtCallbackBoundary() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu(
        [](stui::native::NativeMenu&) { throw std::runtime_error("render failed"); });
    expect(menu.registerMenu(port).has_value(), "expected registration");

    port.windowCallback();

    expect(menu.lastError() == stui::native::MenuRegistrationError::callbackFailed,
           "expected contained callback failure");
}

void openAndCloseUseRegisteredWindowState() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu;
    expect(menu.registerMenu(port).has_value(), "expected registration");
    expect(menu.isRegistered(), "expected registered state");

    menu.open();
    expect(menu.isOpen(), "expected open state");
    menu.close();
    expect(!menu.isOpen(), "expected closed state");
}

void toggleOpensAndClosesWithoutInvokingLauncher() {
    FakeMenuFrameworkPort port;
    int launchCount = 0;
    stui::native::NativeMenu menu({}, [&launchCount] {
        ++launchCount;
        return true;
    });
    expect(menu.registerMenu(port).has_value(), "expected registration");

    menu.toggle();
    expect(launchCount == 0, "expected opening toggle not to invoke launcher rendering");
    expect(menu.isOpen(), "expected opening toggle to open native window");

    menu.toggle();
    expect(launchCount == 0, "expected closing toggle not to invoke launcher rendering");
    expect(!menu.isOpen(), "expected closing toggle to close native window");
}

void rejectedSectionLaunchLeavesWindowClosed() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu({}, [] { return false; });
    expect(menu.registerMenu(port).has_value(), "expected registration");

    port.itemCallback();

    expect(!menu.isOpen(), "expected rejected section launch to leave window closed");
}

void throwingSectionLaunchIsContained() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu({}, []() -> bool {
        throw std::runtime_error("launch failed");
    });
    expect(menu.registerMenu(port).has_value(), "expected registration");

    port.itemCallback();

    expect(!menu.isOpen(), "expected throwing section launch to leave window closed");
    expect(menu.lastError() == stui::native::MenuRegistrationError::callbackFailed,
           "expected contained section launch failure");
}

void renderActionCanCloseOwningWindow() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu([](stui::native::NativeMenu& owner) { owner.close(); });
    expect(menu.registerMenu(port).has_value(), "expected registration");
    menu.open();

    port.windowCallback();

    expect(!menu.isOpen(), "expected render action to close its owning window");
}

void matchingFrameworkInputTogglesAndConsumesTheEvent() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu;
    expect(menu.registerMenu(port).has_value(), "expected registration");

    expect(menu.handleFrameworkHotkey(true, true, true),
           "expected matching key-down to be consumed");
    expect(menu.isOpen(), "expected matching key-down to open the menu");
    expect(menu.handleFrameworkHotkey(true, true, true),
           "expected closing key-down to be consumed");
    expect(!menu.isOpen(), "expected next matching key-down to close the menu");
    expect(!menu.handleFrameworkHotkey(true, false, true),
           "expected key-up not to be consumed");
    expect(!menu.handleFrameworkHotkey(false, true, true),
           "expected non-keyboard input not to be consumed");
    expect(!menu.handleFrameworkHotkey(true, true, false),
           "expected unrelated keyboard input not to be consumed");
}

void openGuardBlocksNewOpeningFromHotkeyAndSectionItem() {
    FakeMenuFrameworkPort port;
    bool allowOpen = false;
    int openCount = 0;
    stui::native::NativeMenu menu({}, [] { return true; });
    expect(menu.registerMenu(port).has_value(), "expected registration");
    menu.setOpenGuard([&] { return allowOpen; });
    menu.setOpenCallback([&] { ++openCount; });

    expect(menu.handleFrameworkHotkey(true, true, true),
        "expected blocked matching hotkey to remain consumed");
    expect(!menu.isOpen() && openCount == 0,
        "expected guard to block hotkey opening and open callback");
    port.itemCallback();
    expect(!menu.isOpen() && openCount == 0,
        "expected guard to block section-item opening");

    allowOpen = true;
    expect(menu.handleFrameworkHotkey(true, true, true) && menu.isOpen(),
        "expected opening after blocking menu closes");
    expect(openCount == 1, "expected allowed opening callback once");

    allowOpen = false;
    expect(menu.handleFrameworkHotkey(true, true, true) && !menu.isOpen(),
        "expected guard not to prevent closing an already-open window");
}

void throwingOpenGuardLeavesWindowClosed() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu;
    expect(menu.registerMenu(port).has_value(), "expected registration");
    menu.setOpenGuard([]() -> bool { throw std::runtime_error("open guard failed"); });

    expect(menu.handleFrameworkHotkey(true, true, true),
        "expected matching hotkey to remain consumed after guard failure");
    expect(!menu.isOpen(), "expected failed guard to leave window closed");
    expect(menu.lastError() == stui::native::MenuRegistrationError::callbackFailed,
        "expected open guard exception to remain inside noexcept boundary");
}

void unavailableRegistrationCanBeRetriedAndClearsError() {
    FakeMenuFrameworkPort port;
    port.isAvailable = false;
    stui::native::NativeMenu menu;

    expect(!menu.registerMenu(port), "expected unavailable registration failure");
    expect(menu.lastError() == stui::native::MenuRegistrationError::unavailable,
           "expected stored unavailable error");
    port.isAvailable = true;
    expect(menu.registerMenu(port).has_value(), "expected registration retry");
    expect(!menu.lastError(), "expected successful retry to clear prior error");
    expect(menu.isRegistered(), "expected registered state after retry");
}

void hotkeyCloseCanBeDeferredUntilTheOwnerCompletesItsWork() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu;
    expect(menu.registerMenu(port).has_value(), "expected registration");
    int openCount = 0;
    int closeRequests = 0;
    menu.setOpenCallback([&] { ++openCount; });
    menu.setCloseRequestCallback([&] {
        ++closeRequests;
        return true;
    });
    expect(menu.handleFrameworkHotkey(true, true, true) && menu.isOpen(), "hotkey opens normally");
    expect(closeRequests == 0 && openCount == 1, "opening must not request close");
    expect(menu.handleFrameworkHotkey(true, true, true) && menu.isOpen(), "handled close keeps window open");
    expect(menu.handleFrameworkHotkey(true, true, true) && menu.isOpen(), "repeated close remains deferred");
    expect(closeRequests == 2 && openCount == 1, "deferred close cannot reset/reopen the session");
    menu.close();
    expect(!menu.isOpen() && closeRequests == 2, "completion closes directly without another close request");
}

void unhandledCloseRequestPreservesImmediateHotkeyClose() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu;
    expect(menu.registerMenu(port).has_value(), "expected registration");
    menu.setCloseRequestCallback([] { return false; });
    menu.open();
    expect(menu.handleFrameworkHotkey(true, true, true) && !menu.isOpen(),
        "non-editor owner permits immediate close");
}

void throwingCloseRequestKeepsTheWindowOpen() {
    FakeMenuFrameworkPort port;
    stui::native::NativeMenu menu;
    expect(menu.registerMenu(port).has_value(), "expected registration");
    menu.setCloseRequestCallback([]() -> bool { throw std::runtime_error("close request failed"); });
    menu.open();
    expect(menu.handleFrameworkHotkey(true, true, true) && menu.isOpen(),
        "failed close intent must not bypass pending owner work");
    expect(menu.lastError() == stui::native::MenuRegistrationError::callbackFailed,
        "close callback exception is contained at the noexcept hotkey boundary");
}

void registrationErrorsHaveStableDiagnosticNames() {
    expect(stui::native::registrationErrorName(
               stui::native::MenuRegistrationError::missingExport) == "missing export",
           "expected missing-export diagnostic");
    expect(stui::native::registrationErrorName(
               stui::native::MenuRegistrationError::windowCreationFailed) ==
               "window creation failed",
           "expected window-creation diagnostic");
}

}  // namespace

int main() {
    try {
        registersOneBlockingTattooBrowser();
        std::cout << "PASS registers one blocking tattoo browser\n";
        repeatedRegistrationIsIdempotent();
        std::cout << "PASS repeated registration is idempotent\n";
        sectionRenderingDoesNotOpenBrowserWithoutRequest();
        std::cout << "PASS section rendering does not open browser without request\n";
        explicitSectionRequestOpensBrowser();
        std::cout << "PASS explicit section request opens browser\n";
        unavailableFrameworkDoesNotRegisterAnything();
        std::cout << "PASS unavailable framework does not register anything\n";
        unsupportedMajorVersionDoesNotRegisterAnything();
        std::cout << "PASS unsupported major version does not register anything\n";
        zeroWindowTokenDoesNotPublishBrowserItem();
        std::cout << "PASS zero window token does not publish browser item\n";
        destroyedMenuMakesSectionCallbackANoOp();
        std::cout << "PASS destroyed menu makes section callback a no-op\n";
        windowRegistrationPreservesNamedFrameworkError();
        std::cout << "PASS window registration preserves named framework error\n";
        renderExceptionIsContainedAtCallbackBoundary();
        std::cout << "PASS render exception is contained at callback boundary\n";
        openAndCloseUseRegisteredWindowState();
        std::cout << "PASS open and close use registered window state\n";
        toggleOpensAndClosesWithoutInvokingLauncher();
        std::cout << "PASS toggle opens and closes without invoking launcher\n";
        rejectedSectionLaunchLeavesWindowClosed();
        std::cout << "PASS rejected section launch leaves window closed\n";
        throwingSectionLaunchIsContained();
        std::cout << "PASS throwing section launch is contained\n";
        renderActionCanCloseOwningWindow();
        std::cout << "PASS render action can close owning window\n";
        matchingFrameworkInputTogglesAndConsumesTheEvent();
        std::cout << "PASS matching framework input toggles and consumes the event\n";
        openGuardBlocksNewOpeningFromHotkeyAndSectionItem();
        std::cout << "PASS open guard blocks hotkey and section-item opening\n";
        throwingOpenGuardLeavesWindowClosed();
        std::cout << "PASS throwing open guard leaves window closed\n";
        hotkeyCloseCanBeDeferredUntilTheOwnerCompletesItsWork();
        std::cout << "PASS hotkey close defers until owner completion\n";
        unhandledCloseRequestPreservesImmediateHotkeyClose();
        std::cout << "PASS unhandled close request closes immediately\n";
        throwingCloseRequestKeepsTheWindowOpen();
        std::cout << "PASS throwing close request keeps window open\n";
        unavailableRegistrationCanBeRetriedAndClearsError();
        std::cout << "PASS unavailable registration can be retried and clears error\n";
        registrationErrorsHaveStableDiagnosticNames();
        std::cout << "PASS registration errors have stable diagnostic names\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
