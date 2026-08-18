#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vcam::runtime {

enum class Arm64PlanStatus {
    kReady = 0,
    kInvalidAddress,
    kInsufficientPrologue,
    kPcRelativeInstruction,
    kControlFlowInstruction,
};

struct Arm64PatchPlan {
    Arm64PlanStatus status = Arm64PlanStatus::kInvalidAddress;
    std::string message;
    std::size_t overwriteSize = 0;
    std::uintptr_t resumeAddress = 0;
    std::vector<std::uint8_t> originalBytes;
    std::vector<std::uint8_t> entryPatch;
    std::vector<std::uint8_t> trampoline;

    explicit operator bool() const { return status == Arm64PlanStatus::kReady; }
};

// Produces bytes only. This function never changes page permissions or writes to
// executable memory. The caller must separately coordinate threads and verify
// the recipe again immediately before any future installation step.
Arm64PatchPlan planArm64InlineHook(
        std::uintptr_t targetAddress,
        std::uintptr_t replacementAddress,
        const std::vector<std::uint8_t>& targetPrologue);

const char* arm64PlanStatusName(Arm64PlanStatus status);

}  // namespace vcam::runtime
