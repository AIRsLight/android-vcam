#include "vcam/Android14CameraServiceProfile.h"

namespace vcam::runtime {

AbiRecipe makeAndroid14InitialCameraServiceRecipe() {
    AbiRecipe recipe;
    recipe.schema = 2;
    recipe.architecture = "arm64";
    recipe.transactions = {
            {"connect_api1", 3},
            {"connect_device", 4},
            {"add_listener", 5},
            {"get_concurrent_camera_ids", 6},
            {"concurrent_session_support", 7},
            {"remove_listener", 8},
            {"get_camera_characteristics", 9},
            {"get_legacy_parameters", 12},
            {"supports_camera_api", 13},
            {"set_torch_mode", 16},
            {"turn_on_torch_with_strength", 17},
            {"get_torch_strength", 18},
    };
    return recipe;
}

AbiRecipe makeNx769jAndroid14CameraServiceRecipe() {
    return makeAndroid14InitialCameraServiceRecipe();
}

Android14CameraServiceProtocolSelection selectAndroid14CameraServiceProtocol(
        int sdk,
        const std::string& fingerprint) {
    Android14CameraServiceProtocolSelection selection;
    if (sdk != 34) return selection;

    selection.recipe = makeAndroid14InitialCameraServiceRecipe();
    selection.observationAllowed = true;
    if (matchesNx769jAndroid14CameraServiceProfile(fingerprint)) {
        selection.profileName = kNx769jAndroid14ProfileName;
        selection.confidence = Android14CameraServiceProtocolConfidence::kQualified;
        selection.routingAllowed = true;
    } else {
        selection.profileName = kAndroid14InitialCandidateProfileName;
        selection.confidence =
                Android14CameraServiceProtocolConfidence::kProbeCandidate;
    }
    return selection;
}

const char* android14CameraServiceProtocolConfidenceName(
        Android14CameraServiceProtocolConfidence confidence) noexcept {
    switch (confidence) {
        case Android14CameraServiceProtocolConfidence::kUnsupported:
            return "unsupported";
        case Android14CameraServiceProtocolConfidence::kProbeCandidate:
            return "probe_candidate";
        case Android14CameraServiceProtocolConfidence::kQualified:
            return "qualified";
    }
    return "unsupported";
}

bool matchesNx769jAndroid14CameraServiceProfile(
        const std::string& fingerprint) noexcept {
    return fingerprint == kNx769jAndroid14Fingerprint;
}

}  // namespace vcam::runtime
