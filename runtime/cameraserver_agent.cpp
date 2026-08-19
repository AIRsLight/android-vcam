#include "vcam/AndroidSignalQuiescenceBackend.h"
#include "vcam/RuntimeAbiGuard.h"
#include "vcam/BinderPassThroughBridge.h"
#include "vcam/CameraServerAgent.h"
#include "vcam/RuntimeActivationPreflight.h"
#include "vcam/RuntimeHookStrategy.h"
#include "vcam/StaticArm64Trampoline.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

void copyMessage(const std::string& message, char* output, std::size_t capacity) {
    if (output == nullptr || capacity == 0) {
        return;
    }
    std::snprintf(output, capacity, "%s", message.c_str());
}

std::string moduleBasename(const std::string& suffix) {
    const std::size_t slash = suffix.find_last_of('/');
    return slash == std::string::npos ? suffix : suffix.substr(slash + 1);
}

}  // namespace

// This entry point deliberately performs validation only. Loading the library has no
// constructor side effects and cannot alter cameraserver until a future, explicit
// activation entry point is implemented.
extern "C" __attribute__((visibility("default"))) int vcam_cameraserver_agent_validate(
        const char* recipePath, char* message, std::size_t messageCapacity) {
    if (recipePath == nullptr || recipePath[0] == '\0') {
        copyMessage("recipe path is empty", message, messageCapacity);
        return static_cast<int>(vcam::runtime::ProbeStatus::kInvalidRecipe);
    }
    vcam::runtime::AbiRecipe recipe;
    std::string error;
    if (!vcam::runtime::parseAbiRecipe(recipePath, &recipe, &error)) {
        copyMessage(error, message, messageCapacity);
        return static_cast<int>(vcam::runtime::ProbeStatus::kInvalidRecipe);
    }
    const vcam::runtime::ProbeResult result = vcam::runtime::validateLoadedModule(recipe);
    copyMessage(result.message, message, messageCapacity);
    return static_cast<int>(result.status);
}

extern "C" __attribute__((visibility("default"))) int vcam_cameraserver_agent_plan(
        const char* recipePath, char* message, std::size_t messageCapacity) {
    if (recipePath == nullptr || recipePath[0] == '\0') {
        copyMessage("recipe path is empty", message, messageCapacity);
        return 1000 + static_cast<int>(vcam::runtime::ProbeStatus::kInvalidRecipe);
    }
    vcam::runtime::AbiRecipe recipe;
    std::string error;
    if (!vcam::runtime::parseAbiRecipe(recipePath, &recipe, &error)) {
        copyMessage(error, message, messageCapacity);
        return 1000 + static_cast<int>(vcam::runtime::ProbeStatus::kInvalidRecipe);
    }
    const vcam::runtime::ProbeResult probe = vcam::runtime::validateLoadedModule(recipe);
    if (!probe) {
        copyMessage(probe.message, message, messageCapacity);
        return 1000 + static_cast<int>(probe.status);
    }
    const vcam::runtime::OnTransactStrategyPlan plan =
            vcam::runtime::planOnTransactStrategy(
                    recipe, probe, vcam::runtime::binderPassThroughEntryAddress());
    if (!plan) {
        copyMessage(plan.message, message, messageCapacity);
        return 2000 + static_cast<int>(plan.status);
    }
    const vcam::runtime::StaticTrampolineSelection trampoline =
            vcam::runtime::selectStaticArm64Trampoline(recipe);
    copyMessage(trampoline.message, message, messageCapacity);
    return trampoline ? 0 : 6000 + static_cast<int>(trampoline.status);
}

extern "C" __attribute__((visibility("default"))) int vcam_cameraserver_agent_preflight(
        const char* recipePath, char* message, std::size_t messageCapacity) {
    if (recipePath == nullptr || recipePath[0] == '\0') {
        copyMessage("recipe path is empty", message, messageCapacity);
        return 3000 + static_cast<int>(vcam::runtime::ProbeStatus::kInvalidRecipe);
    }
    vcam::runtime::AbiRecipe recipe;
    std::string error;
    if (!vcam::runtime::parseAbiRecipe(recipePath, &recipe, &error)) {
        copyMessage(error, message, messageCapacity);
        return 3000 + static_cast<int>(vcam::runtime::ProbeStatus::kInvalidRecipe);
    }
    const vcam::runtime::ProbeResult probe = vcam::runtime::validateLoadedModule(recipe);
    if (!probe) {
        copyMessage(probe.message, message, messageCapacity);
        return 3000 + static_cast<int>(probe.status);
    }
    const vcam::runtime::OnTransactStrategyPlan plan =
            vcam::runtime::planOnTransactStrategy(
                    recipe, probe, vcam::runtime::binderPassThroughEntryAddress());
    if (!plan) {
        copyMessage(plan.message, message, messageCapacity);
        return 4000 + static_cast<int>(plan.status);
    }
    const vcam::runtime::StaticTrampolineSelection trampoline =
            vcam::runtime::selectStaticArm64Trampoline(recipe);
    if (!trampoline) {
        copyMessage(trampoline.message, message, messageCapacity);
        return 6000 + static_cast<int>(trampoline.status);
    }
    const vcam::runtime::ActivationSnapshot snapshot =
            vcam::runtime::collectCurrentProcessActivationSnapshot(
                    plan.targetAddress, plan.patch.overwriteSize);
    const vcam::runtime::ActivationPreflightResult preflight =
            vcam::runtime::evaluateActivationPreflight(
                    plan.patch, plan.targetAddress, moduleBasename(recipe.moduleSuffix), snapshot);
    copyMessage(
            preflight.status == vcam::runtime::ActivationPreflightStatus::kSnapshotError &&
                    !snapshot.collectionError.empty()
                    ? snapshot.collectionError
                    : preflight.message,
            message, messageCapacity);
    return preflight ? 0 : 5000 + static_cast<int>(preflight.status);
}

extern "C" __attribute__((visibility("default"))) int
vcam_cameraserver_agent_signal_preflight(
        const char* recipePath, char* message, std::size_t messageCapacity) {
    const int activationStatus = vcam_cameraserver_agent_preflight(
            recipePath, message, messageCapacity);
    if (activationStatus != 0) {
        return activationStatus;
    }
    const vcam::runtime::SignalEligibilityResult signal =
            vcam::runtime::selectEligibleRealtimeSignal();
    copyMessage(signal.message, message, messageCapacity);
    return signal ? 0 : 7000 + static_cast<int>(signal.status);
}
