#pragma once

namespace vcam::runtime::bootstrap {

inline constexpr char kStockCameraServerPath[] = "/system/bin/vcam/cameraserver";
inline constexpr char kRouterLibraryPath[] =
        "/system/lib64/libvcam_cameraserver_router.so";
inline constexpr char kModePath[] = "/data/vendor/camera/vcam/bootstrap.mode";
inline constexpr char kPendingPath[] = "/data/vendor/camera/vcam/bootstrap.pending";
inline constexpr char kExpectedDomainPrefix[] = "u:r:cameraserver:";

}  // namespace vcam::runtime::bootstrap
