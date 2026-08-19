#include "vcam/CameraServerBootstrapMode.h"

#include <cassert>
#include <cstring>

int main() {
    using vcam::runtime::CameraServerBootstrapMode;
    using vcam::runtime::cameraServerBootstrapModeName;
    using vcam::runtime::parseCameraServerBootstrapMode;

    assert(parseCameraServerBootstrapMode(nullptr) == CameraServerBootstrapMode::kStock);
    assert(parseCameraServerBootstrapMode("") == CameraServerBootstrapMode::kStock);
    assert(parseCameraServerBootstrapMode("stock") == CameraServerBootstrapMode::kStock);
    assert(parseCameraServerBootstrapMode("preflight") ==
           CameraServerBootstrapMode::kPreflight);
    assert(parseCameraServerBootstrapMode("passthrough") ==
           CameraServerBootstrapMode::kPassThrough);
    assert(parseCameraServerBootstrapMode("physical-route") ==
           CameraServerBootstrapMode::kPhysicalRoute);
    assert(parseCameraServerBootstrapMode("disabled") ==
           CameraServerBootstrapMode::kInvalid);

    assert(std::strcmp(cameraServerBootstrapModeName(CameraServerBootstrapMode::kStock),
                       "stock") == 0);
    assert(std::strcmp(cameraServerBootstrapModeName(
                               CameraServerBootstrapMode::kPreflight),
                       "preflight") == 0);
    assert(std::strcmp(cameraServerBootstrapModeName(
                               CameraServerBootstrapMode::kPassThrough),
                       "passthrough") == 0);
    assert(std::strcmp(cameraServerBootstrapModeName(
                               CameraServerBootstrapMode::kPhysicalRoute),
                       "physical-route") == 0);
    assert(std::strcmp(cameraServerBootstrapModeName(CameraServerBootstrapMode::kInvalid),
                       "invalid") == 0);
    return 0;
}
