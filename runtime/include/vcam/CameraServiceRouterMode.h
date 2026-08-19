#pragma once

namespace vcam::runtime {

enum class CameraServiceRouterMode {
    kPreflight = 0,
    kPassThrough,
    kPhysicalRoute,
    kDisabled,
    kInvalid,
};

// A missing value deliberately selects read-only preflight. Taking over the
// Binder name always requires an explicit passthrough or physical-route value.
CameraServiceRouterMode parseCameraServiceRouterMode(const char* value) noexcept;
const char* cameraServiceRouterModeName(CameraServiceRouterMode mode) noexcept;

}  // namespace vcam::runtime
