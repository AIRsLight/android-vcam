#include "vcam/RuntimeAbiGuard.h"

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
