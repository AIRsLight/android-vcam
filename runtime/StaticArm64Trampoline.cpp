#include "vcam/StaticArm64Trampoline.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace vcam::runtime {
namespace {

constexpr char kProfileName[] = "nx769j-ukq1-20240417";
constexpr char kModule[] = "libcameraservice.so";
constexpr std::uint64_t kModuleSize = 3132936;
constexpr char kModuleSha256[] =
        "a26f8ee10002769428871e042c7993e87ad769703897dd75a2fb93a725c64438";
constexpr char kModuleBuildId[] = "747dab9fa491b5af026f143c2c967789";
constexpr char kClientModule[] = "libcamera_client.so";
constexpr std::uint64_t kClientModuleSize = 596328;
constexpr char kClientModuleSha256[] =
        "1cf518e86a2e5461e585d8dbd7a1dbc93e7ba2bcc95c3e254ebdcc72ee0433c5";
constexpr char kClientModuleBuildId[] = "f7cea72167468ee2dfac06e4433d8fa8";
constexpr char kOnTransactSymbol[] =
        "_ZN7android13CameraService10onTransactEjRKNS_6ParcelEPS1_j";
constexpr char kCharacteristicsSymbol[] =
        "_ZN7android13CameraService24getCameraCharacteristicsERKNS_8String16EibPNS_14CameraMetadataE";
constexpr std::array<std::uint8_t, 16> kOnTransactPrologue = {
    0x3f, 0x23, 0x03, 0xd5,
    0xff, 0x83, 0x02, 0xd1,
    0xfd, 0x7b, 0x04, 0xa9,
    0xfc, 0x6f, 0x05, 0xa9,
};
constexpr std::array<std::uint8_t, 16> kCharacteristicsPrologue = {
    0x3f, 0x23, 0x03, 0xd5,
    0xff, 0x03, 0x07, 0xd1,
    0xfd, 0x7b, 0x16, 0xa9,
    0xfc, 0x6f, 0x17, 0xa9,
};

struct ExpectedTransaction {
    const char* role;
    std::uint32_t code;
};

constexpr std::array<ExpectedTransaction, 12> kTransactions = {{
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
}};

bool hasSymbol(
        const AbiRecipe& recipe,
        const char* name,
        const std::array<std::uint8_t, 16>& prefix) {
    return std::any_of(recipe.symbols.begin(), recipe.symbols.end(),
            [&](const SymbolRequirement& symbol) {
                return symbol.name == name &&
                        symbol.codePrefix.size() == prefix.size() &&
                        std::equal(prefix.begin(), prefix.end(), symbol.codePrefix.begin());
            });
}

bool hasTransaction(const AbiRecipe& recipe, const ExpectedTransaction& expected) {
    return std::any_of(recipe.transactions.begin(), recipe.transactions.end(),
            [&](const BinderTransaction& transaction) {
                return transaction.role == expected.role && transaction.code == expected.code;
            });
}

bool exactlyMatchesNx769j(const AbiRecipe& recipe) {
    if (recipe.schema != 2 || recipe.architecture != "arm64" ||
        recipe.moduleSuffix != kModule || recipe.fileSize != kModuleSize ||
        recipe.sha256Hex != kModuleSha256 || recipe.buildIdHex != kModuleBuildId ||
        recipe.symbols.size() != 2 || recipe.hooks.size() != 1 ||
        recipe.transactions.size() != kTransactions.size() ||
        recipe.dependencies.size() != 1) {
        return false;
    }
    const HookRequirement& hook = recipe.hooks.front();
    const ModuleIdentity& dependency = recipe.dependencies.front();
    if (hook.role != "on_transact" || hook.symbol != kOnTransactSymbol ||
        dependency.moduleSuffix != kClientModule ||
        dependency.fileSize != kClientModuleSize ||
        dependency.sha256Hex != kClientModuleSha256 ||
        dependency.buildIdHex != kClientModuleBuildId ||
        !hasSymbol(recipe, kOnTransactSymbol, kOnTransactPrologue) ||
        !hasSymbol(recipe, kCharacteristicsSymbol, kCharacteristicsPrologue)) {
        return false;
    }
    return std::all_of(kTransactions.begin(), kTransactions.end(),
            [&](const ExpectedTransaction& expected) {
                return hasTransaction(recipe, expected);
            });
}

#if defined(__ANDROID__) && defined(__aarch64__)

extern "C" const unsigned char vcam_nx769j_on_transact_trampoline[];
extern "C" const unsigned char vcam_nx769j_on_transact_trampoline_end[];

extern "C" {
__attribute__((visibility("hidden"), aligned(8)))
std::uintptr_t vcam_nx769j_on_transact_resume_address = 0;
}

bool bindNx769jResumeAddress(void*, std::uintptr_t resumeAddress) noexcept {
    if (resumeAddress == 0 || (resumeAddress & 3u) != 0) {
        return false;
    }
    std::uintptr_t expected = 0;
    if (__atomic_compare_exchange_n(
                &vcam_nx769j_on_transact_resume_address,
                &expected,
                resumeAddress,
                false,
                __ATOMIC_RELEASE,
                __ATOMIC_ACQUIRE)) {
        return true;
    }
    return expected == resumeAddress;
}

#endif

StaticTrampolineSelection noMatch() noexcept {
    StaticTrampolineSelection result;
    result.status = StaticTrampolineStatus::kNoExactRecipe;
    result.message = "no precompiled trampoline exactly matches the complete ABI recipe";
    return result;
}

}  // namespace

StaticTrampolineSelection selectStaticArm64Trampoline(const AbiRecipe& recipe) noexcept {
    if (!exactlyMatchesNx769j(recipe)) {
        return noMatch();
    }

    StaticTrampolineSelection result;
    result.name = kProfileName;
    result.trampoline.relocatedOriginalBytes = kOnTransactPrologue;
#if defined(__ANDROID__) && defined(__aarch64__)
    const std::uintptr_t entry =
            reinterpret_cast<std::uintptr_t>(vcam_nx769j_on_transact_trampoline);
    const std::uintptr_t end =
            reinterpret_cast<std::uintptr_t>(vcam_nx769j_on_transact_trampoline_end);
    if (entry == 0 || end <= entry) {
        result.status = StaticTrampolineStatus::kUnsupportedPlatform;
        result.message = "precompiled trampoline symbols are unavailable";
        return result;
    }
    result.status = StaticTrampolineStatus::kReady;
    result.message = "exact NX769J precompiled trampoline selected; no memory was modified";
    result.trampoline.entryAddress = entry;
    result.trampoline.codeSize = end - entry;
    result.trampoline.bindResumeAddress = &bindNx769jResumeAddress;
#else
    result.status = StaticTrampolineStatus::kUnsupportedPlatform;
    result.message = "precompiled trampoline is available only in Android ARM64 builds";
#endif
    return result;
}

const char* staticTrampolineStatusName(StaticTrampolineStatus status) noexcept {
    switch (status) {
        case StaticTrampolineStatus::kReady: return "ready";
        case StaticTrampolineStatus::kNoExactRecipe: return "no_exact_recipe";
        case StaticTrampolineStatus::kUnsupportedPlatform: return "unsupported_platform";
    }
    return "unknown";
}

}  // namespace vcam::runtime
