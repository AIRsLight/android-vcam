#include "vcam/StaticArm64Trampoline.h"

#include <cassert>
#include <cstdint>

namespace {

vcam::runtime::AbiRecipe nx769jRecipe() {
    vcam::runtime::AbiRecipe recipe;
    recipe.schema = 2;
    recipe.architecture = "arm64";
    recipe.moduleSuffix = "libcameraservice.so";
    recipe.fileSize = 3132936;
    recipe.sha256Hex =
            "a26f8ee10002769428871e042c7993e87ad769703897dd75a2fb93a725c64438";
    recipe.buildIdHex = "747dab9fa491b5af026f143c2c967789";
    const char* onTransact =
            "_ZN7android13CameraService10onTransactEjRKNS_6ParcelEPS1_j";
    const char* characteristics =
            "_ZN7android13CameraService24getCameraCharacteristicsERKNS_8String16EibPNS_14CameraMetadataE";
    recipe.symbols.push_back({characteristics, {
        0x3f, 0x23, 0x03, 0xd5, 0xff, 0x03, 0x07, 0xd1,
        0xfd, 0x7b, 0x16, 0xa9, 0xfc, 0x6f, 0x17, 0xa9,
    }});
    recipe.symbols.push_back({onTransact, {
        0x3f, 0x23, 0x03, 0xd5, 0xff, 0x83, 0x02, 0xd1,
        0xfd, 0x7b, 0x04, 0xa9, 0xfc, 0x6f, 0x05, 0xa9,
    }});
    recipe.hooks.push_back({"on_transact", onTransact});
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
    recipe.dependencies.push_back({
        "libcamera_client.so",
        596328,
        "1cf518e86a2e5461e585d8dbd7a1dbc93e7ba2bcc95c3e254ebdcc72ee0433c5",
        "f7cea72167468ee2dfac06e4433d8fa8",
    });
    return recipe;
}

}  // namespace

int main() {
    {
        const auto selection =
                vcam::runtime::selectStaticArm64Trampoline(nx769jRecipe());
#if defined(__ANDROID__) && defined(__aarch64__)
        assert(selection.status == vcam::runtime::StaticTrampolineStatus::kReady);
        assert(selection.trampoline.entryAddress != 0);
        assert(selection.trampoline.codeSize >= 48);
        assert(selection.trampoline.bindResumeAddress != nullptr);
        assert(selection.trampoline.bindResumeAddress(nullptr, 0x71001017a8));
        assert(selection.trampoline.bindResumeAddress(nullptr, 0x71001017a8));
        assert(!selection.trampoline.bindResumeAddress(nullptr, 0x71001027a8));
#else
        assert(selection.status ==
               vcam::runtime::StaticTrampolineStatus::kUnsupportedPlatform);
        assert(selection.trampoline.entryAddress == 0);
#endif
        assert(selection.name != nullptr);
        assert(selection.trampoline.relocatedOriginalBytes[0] == 0x3f);
    }

    {
        auto recipe = nx769jRecipe();
        recipe.sha256Hex[0] = '0';
        assert(vcam::runtime::selectStaticArm64Trampoline(recipe).status ==
               vcam::runtime::StaticTrampolineStatus::kNoExactRecipe);
    }
    {
        auto recipe = nx769jRecipe();
        recipe.symbols[1].codePrefix[4] ^= 0xff;
        assert(vcam::runtime::selectStaticArm64Trampoline(recipe).status ==
               vcam::runtime::StaticTrampolineStatus::kNoExactRecipe);
    }
    {
        auto recipe = nx769jRecipe();
        recipe.transactions[1].code = 99;
        assert(vcam::runtime::selectStaticArm64Trampoline(recipe).status ==
               vcam::runtime::StaticTrampolineStatus::kNoExactRecipe);
    }
    {
        auto recipe = nx769jRecipe();
        recipe.dependencies[0].buildIdHex[0] = '0';
        assert(vcam::runtime::selectStaticArm64Trampoline(recipe).status ==
               vcam::runtime::StaticTrampolineStatus::kNoExactRecipe);
    }

    assert(vcam::runtime::staticTrampolineStatusName(
                   vcam::runtime::StaticTrampolineStatus::kReady) != nullptr);
    return 0;
}
