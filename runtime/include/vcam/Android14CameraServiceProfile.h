#pragma once

#include <string>

#include "vcam/RuntimeAbiGuard.h"

namespace vcam::runtime {

constexpr char kNx769jAndroid14Fingerprint[] =
        "nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys";
constexpr char kNx769jAndroid14ProfileName[] = "nx769j-ukq1-20240417";

// Returns the Binder transaction mapping independently confirmed from the
// NX769J Android 14 libcamera_client.so client stubs.
AbiRecipe makeNx769jAndroid14CameraServiceRecipe();

bool matchesNx769jAndroid14CameraServiceProfile(
        const std::string& fingerprint) noexcept;

}  // namespace vcam::runtime
