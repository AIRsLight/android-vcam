#pragma once

namespace vcam::runtime::bootstrap {

inline constexpr char kRouterLibraryPath[] =
        "/system/lib64/libvcam_cameraserver_router.so";
inline constexpr char kModePath[] = "/system/etc/android_vcam/bootstrap.mode";
inline constexpr char kPendingPath[] = "/dev/vcam/bootstrap.pending";
inline constexpr char kRouterStatsPath[] = "/dev/vcam/router.stats";
inline constexpr char kExpectedDomainPrefix[] = "u:r:cameraserver:";

}  // namespace vcam::runtime::bootstrap
