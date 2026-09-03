#pragma once

#include <string>

#include "vcam/RuntimeAbiGuard.h"

namespace vcam::runtime {

constexpr char kNx769jAndroid14Fingerprint[] =
        "nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys";
constexpr char kNx769jAndroid14ProfileName[] = "nx769j-ukq1-20240417";
constexpr char kAndroid14InitialCandidateProfileName[] =
        "android14-aosp-initial-candidate";

enum class Android14CameraServiceProtocolConfidence {
    kUnsupported = 0,
    kProbeCandidate,
    kQualified,
};

struct Android14CameraServiceProtocolSelection {
    AbiRecipe recipe;
    const char* profileName = "none";
    Android14CameraServiceProtocolConfidence confidence =
            Android14CameraServiceProtocolConfidence::kUnsupported;
    bool observationAllowed = false;
    bool routingAllowed = false;
};

// The AOSP initial-release layout is an observation template, not a routing
// authorization. Unknown Android 14 builds may use it only in pass-through
// mode until deterministic live probes establish protocol compatibility.
AbiRecipe makeAndroid14InitialCameraServiceRecipe();

Android14CameraServiceProtocolSelection selectAndroid14CameraServiceProtocol(
        int sdk,
        const std::string& fingerprint);

const char* android14CameraServiceProtocolConfidenceName(
        Android14CameraServiceProtocolConfidence confidence) noexcept;

// Returns the Binder transaction mapping independently confirmed from the
// NX769J Android 14 libcamera_client.so client stubs.
AbiRecipe makeNx769jAndroid14CameraServiceRecipe();

bool matchesNx769jAndroid14CameraServiceProfile(
        const std::string& fingerprint) noexcept;

}  // namespace vcam::runtime
