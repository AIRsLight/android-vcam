#include "vcam/Android14CameraServiceProfile.h"

namespace vcam::runtime {

AbiRecipe makeNx769jAndroid14CameraServiceRecipe() {
    AbiRecipe recipe;
    recipe.schema = 2;
    recipe.architecture = "arm64";
    recipe.transactions = {
            {"connect_api1", 3},
            {"connect_device", 4},
            {"add_listener", 5},
            {"get_concurrent_camera_ids", 6},
            {"concurrent_session_support", 7},
            {"get_camera_characteristics", 9},
            {"get_legacy_parameters", 12},
            {"supports_camera_api", 13},
            {"set_torch_mode", 16},
            {"turn_on_torch_with_strength", 17},
            {"get_torch_strength", 18},
    };
    return recipe;
}

bool matchesNx769jAndroid14CameraServiceProfile(
        const std::string& fingerprint) noexcept {
    return fingerprint == kNx769jAndroid14Fingerprint;
}

}  // namespace vcam::runtime
