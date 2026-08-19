#pragma once

#include "vcam/Arm64PatchInstallTransaction.h"
#include "vcam/RuntimeAbiGuard.h"

namespace vcam::runtime {

enum class StaticTrampolineStatus {
    kReady = 0,
    kNoExactRecipe,
    kUnsupportedPlatform,
};

struct StaticTrampolineSelection {
    StaticTrampolineStatus status = StaticTrampolineStatus::kNoExactRecipe;
    const char* name = "";
    const char* message = "";
    PrecompiledArm64Trampoline trampoline;

    explicit operator bool() const { return status == StaticTrampolineStatus::kReady; }
};

// Selects only a trampoline whose embedded OEM prologue and Binder ABI metadata
// exactly match the complete reviewed recipe. Unknown or edited recipes fail closed.
StaticTrampolineSelection selectStaticArm64Trampoline(const AbiRecipe& recipe) noexcept;

const char* staticTrampolineStatusName(StaticTrampolineStatus status) noexcept;

}  // namespace vcam::runtime
