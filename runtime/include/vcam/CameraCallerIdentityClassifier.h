#pragma once

#include <cstdint>
#include <string>

namespace vcam::runtime {

enum class CameraCallerIdentityKind {
    kNotApplicable = 0,
    kClaimedPackage,
    kUidOnly,
    kUnavailable,
};

struct CameraCallerIdentityClassification {
    CameraCallerIdentityKind kind = CameraCallerIdentityKind::kNotApplicable;
    bool requiresPackageVerification = false;
};

// This classifier describes only which identity material is present. A package
// name carried by Binder is a claim until a platform adapter verifies that the
// calling UID owns it; this function never authorizes scoped routing.
CameraCallerIdentityClassification classifyCameraCallerIdentity(
        bool cameraScoped,
        bool carriesPackageName,
        std::int32_t callingUid,
        std::int32_t callingPid,
        const std::string& packageName) noexcept;

const char* cameraCallerIdentityKindName(CameraCallerIdentityKind kind) noexcept;

}  // namespace vcam::runtime
