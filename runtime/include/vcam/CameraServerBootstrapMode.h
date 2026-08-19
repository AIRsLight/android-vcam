#pragma once

namespace vcam::runtime {

enum class CameraServerBootstrapMode {
    kStock = 0,
    kPreflight,
    kPassThrough,
    kPhysicalRoute,
    kInvalid,
};

// Missing configuration is deliberately physical-only. The launcher must
// never preload the router merely because its files are present.
CameraServerBootstrapMode parseCameraServerBootstrapMode(const char* value) noexcept;
const char* cameraServerBootstrapModeName(CameraServerBootstrapMode mode) noexcept;

}  // namespace vcam::runtime
