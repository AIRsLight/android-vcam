#include "vcam/CameraCallerIdentityClassifier.h"

namespace vcam::runtime {

CameraCallerIdentityClassification classifyCameraCallerIdentity(
        bool cameraScoped,
        bool carriesPackageName,
        std::int32_t callingUid,
        std::int32_t callingPid,
        const std::string& packageName) noexcept {
    CameraCallerIdentityClassification result;
    if (!cameraScoped) {
        return result;
    }
    if (callingUid < 0 || callingPid < 0) {
        result.kind = CameraCallerIdentityKind::kUnavailable;
        return result;
    }
    if (carriesPackageName && !packageName.empty()) {
        result.kind = CameraCallerIdentityKind::kClaimedPackage;
        result.requiresPackageVerification = true;
        return result;
    }
    result.kind = CameraCallerIdentityKind::kUidOnly;
    return result;
}

const char* cameraCallerIdentityKindName(CameraCallerIdentityKind kind) noexcept {
    switch (kind) {
        case CameraCallerIdentityKind::kNotApplicable: return "not_applicable";
        case CameraCallerIdentityKind::kClaimedPackage: return "claimed_package";
        case CameraCallerIdentityKind::kUidOnly: return "uid_only";
        case CameraCallerIdentityKind::kUnavailable: return "unavailable";
    }
    return "unknown";
}

}  // namespace vcam::runtime
