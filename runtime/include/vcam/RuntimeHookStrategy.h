#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vcam/Arm64PatchPlanner.h"
#include "vcam/RuntimeAbiGuard.h"

namespace vcam::runtime {

enum class HookStrategyStatus {
    kReady = 0,
    kAbiNotAllowed,
    kIncompleteRecipe,
    kUnsupportedArchitecture,
    kHookSymbolNotResolved,
    kPatchNotRelocatable,
};

struct OnTransactStrategyPlan {
    HookStrategyStatus status = HookStrategyStatus::kIncompleteRecipe;
    std::string message;
    std::uintptr_t targetAddress = 0;
    std::vector<BinderTransaction> transactions;
    Arm64PatchPlan patch;

    explicit operator bool() const { return status == HookStrategyStatus::kReady; }
};

OnTransactStrategyPlan planOnTransactStrategy(
        const AbiRecipe& recipe,
        const ProbeResult& probe,
        std::uintptr_t replacementAddress);

const char* hookStrategyStatusName(HookStrategyStatus status);

}  // namespace vcam::runtime
