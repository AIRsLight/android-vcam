#include "vcam/CameraServiceRouterMode.h"

#include <cassert>
#include <cstring>

int main() {
    using vcam::runtime::CameraServiceRouterMode;
    using vcam::runtime::cameraServiceRouterModeName;
    using vcam::runtime::parseCameraServiceRouterMode;

    assert(parseCameraServiceRouterMode(nullptr) ==
           CameraServiceRouterMode::kPreflight);
    assert(parseCameraServiceRouterMode("") ==
           CameraServiceRouterMode::kPreflight);
    assert(parseCameraServiceRouterMode("preflight") ==
           CameraServiceRouterMode::kPreflight);
    assert(parseCameraServiceRouterMode("passthrough") ==
           CameraServiceRouterMode::kPassThrough);
    assert(parseCameraServiceRouterMode("physical-route") ==
           CameraServiceRouterMode::kPhysicalRoute);
    assert(parseCameraServiceRouterMode("disabled") ==
           CameraServiceRouterMode::kDisabled);
    assert(parseCameraServiceRouterMode("route") ==
           CameraServiceRouterMode::kInvalid);

    assert(std::strcmp(cameraServiceRouterModeName(
                               CameraServiceRouterMode::kPreflight),
                       "preflight") == 0);
    assert(std::strcmp(cameraServiceRouterModeName(
                               CameraServiceRouterMode::kPassThrough),
                       "passthrough") == 0);
    assert(std::strcmp(cameraServiceRouterModeName(
                               CameraServiceRouterMode::kPhysicalRoute),
                       "physical-route") == 0);
    assert(std::strcmp(cameraServiceRouterModeName(
                               CameraServiceRouterMode::kDisabled),
                       "disabled") == 0);
    assert(std::strcmp(cameraServiceRouterModeName(
                               CameraServiceRouterMode::kInvalid),
                       "invalid") == 0);
    return 0;
}
