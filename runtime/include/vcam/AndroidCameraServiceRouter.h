#pragma once

#include <cstdint>

namespace vcam::runtime {

enum class AndroidCameraServiceRouterState {
    kNotStarted = 0,
    kWaitingForService,
    kPreflightReady,
    kPassThroughReady,
    kDisabled,
    kInvalidMode,
    kThreadStartFailed,
    kServiceManagerUnavailable,
    kServiceTimeout,
    kOriginalServiceNotLocal,
    kWrongInterface,
    kRegistrationFailed,
    kRegistrationVerificationFailed,
};

const char* androidCameraServiceRouterStateName(
        AndroidCameraServiceRouterState state) noexcept;

}  // namespace vcam::runtime

extern "C" {

// These exports are diagnostic only. Loading the library defaults to read-only
// preflight; Binder takeover requires VCAM_BINDER_ROUTER_MODE=passthrough.
int vcam_camera_service_router_state();
const char* vcam_camera_service_router_state_name();
const char* vcam_camera_service_router_observer_profile();
std::uint64_t vcam_camera_service_router_observed_transactions();
std::uint64_t vcam_camera_service_router_ignored_transactions();
std::uint64_t vcam_camera_service_router_rejected_transactions();
std::uint64_t vcam_camera_service_router_unsupported_transactions();

}
