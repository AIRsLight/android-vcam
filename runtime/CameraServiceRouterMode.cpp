#include "vcam/CameraServiceRouterMode.h"

#include <cstring>

namespace vcam::runtime {

CameraServiceRouterMode parseCameraServiceRouterMode(const char* value) noexcept {
    if (value == nullptr || value[0] == '\0' || std::strcmp(value, "preflight") == 0) {
        return CameraServiceRouterMode::kPreflight;
    }
    if (std::strcmp(value, "passthrough") == 0) {
        return CameraServiceRouterMode::kPassThrough;
    }
    if (std::strcmp(value, "physical-route") == 0) {
        return CameraServiceRouterMode::kPhysicalRoute;
    }
    if (std::strcmp(value, "disabled") == 0) {
        return CameraServiceRouterMode::kDisabled;
    }
    return CameraServiceRouterMode::kInvalid;
}

const char* cameraServiceRouterModeName(CameraServiceRouterMode mode) noexcept {
    switch (mode) {
        case CameraServiceRouterMode::kPreflight: return "preflight";
        case CameraServiceRouterMode::kPassThrough: return "passthrough";
        case CameraServiceRouterMode::kPhysicalRoute: return "physical-route";
        case CameraServiceRouterMode::kDisabled: return "disabled";
        case CameraServiceRouterMode::kInvalid: return "invalid";
    }
    return "invalid";
}

}  // namespace vcam::runtime
