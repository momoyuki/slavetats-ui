#include "native/FlickLauncher.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

class FakePort final : public stui::native::FlickPort {
public:
    bool connect() noexcept override {
        ++connectCalls;
        return available;
    }
    bool registerTool(stui::native::FlickLauncher&) noexcept override {
        ++registrations;
        return registrationSucceeds;
    }
    void closeMenu() noexcept override { ++closeCalls; }

    bool available{true};
    bool registrationSucceeds{true};
    int connectCalls{};
    int registrations{};
    int closeCalls{};
};

void unavailableHostDoesNotRegisterOrQueue() {
    FakePort port;
    port.available = false;
    int scheduled = 0;
    stui::native::FlickLauncher launcher(
        port, [&](auto) { ++scheduled; return true; }, [] {});

    expect(!launcher.registerTool(), "unavailable host must not register");
    expect(!launcher.requestOpen(), "unavailable host must not queue");
    expect(port.registrations == 0 && scheduled == 0, "unavailable host had side effects");
}

void registrationIsIdempotentAndRetryable() {
    FakePort port;
    stui::native::FlickLauncher launcher(port, [](auto) { return true; }, [] {});
    port.available = false;
    expect(!launcher.registerTool(), "first connection must fail");
    port.available = true;
    expect(launcher.registerTool(), "connection retry must succeed");
    expect(launcher.registerTool(), "repeat registration must succeed");
    expect(port.connectCalls == 2 && port.registrations == 1, "registration was duplicated");
}

void failedRegistrationCanRetry() {
    FakePort port;
    port.registrationSucceeds = false;
    stui::native::FlickLauncher launcher(port, [](auto) { return true; }, [] {});
    expect(!launcher.registerTool(), "failed registration appeared successful");
    expect(!launcher.requestOpen(), "failed registration accepted a request");
    port.registrationSucceeds = true;
    expect(launcher.registerTool(), "registration retry failed");
    expect(port.registrations == 2, "registration retry was skipped");
}

void rapidClickQueuesOneOpenAfterClose() {
    FakePort port;
    std::function<void()> scheduledTask;
    int opens = 0;
    stui::native::FlickLauncher launcher(
        port,
        [&](std::function<void()> task) {
            scheduledTask = std::move(task);
            return true;
        },
        [&] { ++opens; });
    expect(launcher.registerTool(), "registration failed");
    expect(launcher.requestOpen(), "first click was ignored");
    expect(!launcher.requestOpen(), "second click queued another open");
    expect(port.closeCalls == 1 && opens == 0, "menu opened before scheduled task");
    expect(static_cast<bool>(scheduledTask), "missing scheduled task");
    scheduledTask();
    expect(opens == 1 && !launcher.openPending(), "scheduled open failed");
}

void schedulerFailureCanRetry() {
    FakePort port;
    bool canSchedule = false;
    int opens = 0;
    stui::native::FlickLauncher launcher(
        port,
        [&](std::function<void()> task) {
            if (!canSchedule) return false;
            task();
            return true;
        },
        [&] { ++opens; });
    expect(launcher.registerTool(), "registration failed");
    expect(!launcher.requestOpen(), "failed scheduler appeared successful");
    expect(!launcher.openPending() && opens == 0, "failed scheduler left pending state");
    canSchedule = true;
    expect(launcher.requestOpen() && opens == 1, "retry failed");
}

void openCallbackCannotQueueAnotherOpen() {
    FakePort port;
    std::function<void()> scheduledTask;
    int schedules = 0;
    bool reentrantAccepted = true;
    stui::native::FlickLauncher* activeLauncher = nullptr;
    stui::native::FlickLauncher launcher(
        port,
        [&](std::function<void()> task) {
            ++schedules;
            scheduledTask = std::move(task);
            return true;
        },
        [&] { reentrantAccepted = activeLauncher->requestOpen(); });
    activeLauncher = &launcher;
    expect(launcher.registerTool(), "registration failed");
    expect(launcher.requestOpen(), "initial open request failed");
    scheduledTask();
    expect(!reentrantAccepted && schedules == 1 && !launcher.openPending(),
        "open callback queued a duplicate request");
}

}  // namespace

int main() {
    try {
        unavailableHostDoesNotRegisterOrQueue();
        registrationIsIdempotentAndRetryable();
        failedRegistrationCanRetry();
        rapidClickQueuesOneOpenAfterClose();
        schedulerFailureCanRetry();
        openCallbackCannotQueueAnotherOpen();
        std::cout << "FlickLauncherTests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
