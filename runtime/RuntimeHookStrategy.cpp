#include "vcam/RuntimeHookStrategy.h"

#include <algorithm>

namespace vcam::runtime {
namespace {

OnTransactStrategyPlan failure(HookStrategyStatus status, std::string message) {
    OnTransactStrategyPlan result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

}  // namespace

OnTransactStrategyPlan planOnTransactStrategy(
        const AbiRecipe& recipe,
        const ProbeResult& probe,
        std::uintptr_t replacementAddress) {
    if (probe.status != ProbeStatus::kAllowed) {
        return failure(HookStrategyStatus::kAbiNotAllowed,
                       "ABI guard did not allow this loaded module");
    }
    if (recipe.schema != 2 || recipe.hooks.empty() || recipe.transactions.empty()) {
        return failure(HookStrategyStatus::kIncompleteRecipe,
                       "schema 2 hook and transaction data are required");
    }
    if (recipe.architecture != "arm64") {
        return failure(HookStrategyStatus::kUnsupportedArchitecture,
                       "only a fail-closed ARM64 planner is implemented");
    }

    const auto hook = std::find_if(recipe.hooks.begin(), recipe.hooks.end(),
            [](const HookRequirement& requirement) {
                return requirement.role == "on_transact";
            });
    if (hook == recipe.hooks.end()) {
        return failure(HookStrategyStatus::kIncompleteRecipe,
                       "recipe has no on_transact hook role");
    }
    const auto requirement = std::find_if(recipe.symbols.begin(), recipe.symbols.end(),
            [&](const SymbolRequirement& symbol) { return symbol.name == hook->symbol; });
    const auto resolved = std::find_if(probe.resolvedSymbols.begin(), probe.resolvedSymbols.end(),
            [&](const ResolvedSymbol& symbol) { return symbol.name == hook->symbol; });
    if (requirement == recipe.symbols.end() || resolved == probe.resolvedSymbols.end()) {
        return failure(HookStrategyStatus::kHookSymbolNotResolved,
                       "on_transact symbol was not resolved by the ABI guard");
    }

    Arm64PatchPlan patch = planArm64InlineHook(
            resolved->address, replacementAddress, requirement->codePrefix);
    if (!patch) {
        OnTransactStrategyPlan result = failure(
                HookStrategyStatus::kPatchNotRelocatable, patch.message);
        result.patch = std::move(patch);
        return result;
    }

    OnTransactStrategyPlan result;
    result.status = HookStrategyStatus::kReady;
    result.message = "onTransact strategy is validated and planned; no memory was modified";
    result.targetAddress = resolved->address;
    result.transactions = recipe.transactions;
    result.patch = std::move(patch);
    return result;
}

const char* hookStrategyStatusName(HookStrategyStatus status) {
    switch (status) {
        case HookStrategyStatus::kReady: return "ready";
        case HookStrategyStatus::kAbiNotAllowed: return "abi_not_allowed";
        case HookStrategyStatus::kIncompleteRecipe: return "incomplete_recipe";
        case HookStrategyStatus::kUnsupportedArchitecture: return "unsupported_architecture";
        case HookStrategyStatus::kHookSymbolNotResolved: return "hook_symbol_not_resolved";
        case HookStrategyStatus::kPatchNotRelocatable: return "patch_not_relocatable";
    }
    return "unknown";
}

}  // namespace vcam::runtime
