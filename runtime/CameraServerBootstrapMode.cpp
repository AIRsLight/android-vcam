#include "vcam/CameraServerBootstrapMode.h"

#include <cstring>

namespace vcam::runtime {

CameraServerBootstrapMode parseCameraServerBootstrapMode(const char* value) noexcept {
    if (value == nullptr || value[0] == '\0' || std::strcmp(value, "stock") == 0) {
        return CameraServerBootstrapMode::kStock;
    }
    if (std::strcmp(value, "preflight") == 0) {
        return CameraServerBootstrapMode::kPreflight;
    }
    if (std::strcmp(value, "passthrough") == 0) {
        return CameraServerBootstrapMode::kPassThrough;
    }
    if (std::strcmp(value, "physical-route") == 0) {
        return CameraServerBootstrapMode::kPhysicalRoute;
    }
    return CameraServerBootstrapMode::kInvalid;
}

const char* cameraServerBootstrapModeName(CameraServerBootstrapMode mode) noexcept {
    switch (mode) {
        case CameraServerBootstrapMode::kStock: return "stock";
        case CameraServerBootstrapMode::kPreflight: return "preflight";
        case CameraServerBootstrapMode::kPassThrough: return "passthrough";
        case CameraServerBootstrapMode::kPhysicalRoute: return "physical-route";
        case CameraServerBootstrapMode::kInvalid: return "invalid";
    }
    return "invalid";
}

}  // namespace vcam::runtime
