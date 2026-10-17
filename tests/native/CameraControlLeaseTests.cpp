#include "native/CameraControlLease.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void expect(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

void acceptedLeaseReturnsToGoalAndReleasesExactlyOnce() {
    int requested = 0;
    int goals = 0;
    int released = 0;
    stui::native::CameraControlLease lease(
        [&] { ++requested; return stui::native::CameraControlResult::granted; },
        [&] { ++released; }, [&] { ++goals; });
    expect(lease.acquire() && lease.acquire(), "granted lease should stay held");
    lease.release();
    lease.release();
    expect(requested == 1 && goals == 1 && released == 1,
        "accepted lease should request once, return to goal, and release once");
}

void refusedLeaseNeverReleasesCamera() {
    int released = 0;
    stui::native::CameraControlLease lease(
        [] { return stui::native::CameraControlResult::denied; },
        [&] { ++released; });
    expect(!lease.acquire(), "denied SmoothCam ownership should be reported");
    lease.release();
    expect(released == 0, "denied lease must never release another plugin's camera");
}

}  // namespace

int main() {
    try {
        acceptedLeaseReturnsToGoalAndReleasesExactlyOnce();
        refusedLeaseNeverReleasesCamera();
        std::cout << "PASS camera control lease\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
