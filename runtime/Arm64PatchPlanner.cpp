#include "vcam/Arm64PatchPlanner.h"

#include <algorithm>
#include <limits>

namespace vcam::runtime {
namespace {

constexpr std::size_t kOverwriteSize = 16;
constexpr std::uint32_t kBtiCall = 0xd503245f;
constexpr std::uint32_t kLoadIp1FromPcPlus8 = 0x58000051;
constexpr std::uint32_t kBranchIp1 = 0xd61f0220;

void append32(std::vector<std::uint8_t>* output, std::uint32_t value) {
    for (unsigned int byte = 0; byte < 4; ++byte) {
        output->push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
    }
}

void append64(std::vector<std::uint8_t>* output, std::uint64_t value) {
    for (unsigned int byte = 0; byte < 8; ++byte) {
        output->push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
    }
}

std::uint32_t read32(const std::uint8_t* input) {
    return static_cast<std::uint32_t>(input[0]) |
           (static_cast<std::uint32_t>(input[1]) << 8) |
           (static_cast<std::uint32_t>(input[2]) << 16) |
           (static_cast<std::uint32_t>(input[3]) << 24);
}

bool isPcRelative(std::uint32_t instruction) {
    const bool address = (instruction & 0x1f000000u) == 0x10000000u;  // ADR/ADRP
    const bool literalLoad = (instruction & 0x3b000000u) == 0x18000000u;
    const bool directBranch = (instruction & 0x7c000000u) == 0x14000000u;  // B/BL
    const bool conditionalBranch =
            (instruction & 0xff000010u) == 0x54000000u;  // B.cond/BC.cond
    const bool compareBranch = (instruction & 0x7e000000u) == 0x34000000u;
    const bool testBranch = (instruction & 0x7e000000u) == 0x36000000u;
    return address || literalLoad || directBranch || conditionalBranch ||
           compareBranch || testBranch;
}

bool isRegisterControlFlow(std::uint32_t instruction) {
    return (instruction & 0xfe000000u) == 0xd6000000u;  // BR/BLR/RET/ERET family
}

Arm64PatchPlan failure(Arm64PlanStatus status, std::string message) {
    Arm64PatchPlan result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

void appendAbsoluteBranch(std::vector<std::uint8_t>* output, std::uintptr_t address) {
    append32(output, kLoadIp1FromPcPlus8);
    append32(output, kBranchIp1);
    append64(output, static_cast<std::uint64_t>(address));
}

}  // namespace

Arm64PatchPlan planArm64InlineHook(
        std::uintptr_t targetAddress,
        std::uintptr_t replacementAddress,
        const std::vector<std::uint8_t>& targetPrologue) {
    if (targetAddress == 0 || replacementAddress == 0 ||
        (targetAddress & 3u) != 0 || (replacementAddress & 3u) != 0 ||
        targetAddress > std::numeric_limits<std::uintptr_t>::max() - kOverwriteSize) {
        return failure(Arm64PlanStatus::kInvalidAddress,
                       "ARM64 target and replacement must be non-zero, aligned addresses");
    }
    if (targetPrologue.size() < kOverwriteSize) {
        return failure(Arm64PlanStatus::kInsufficientPrologue,
                       "ARM64 hook requires at least 16 verified prologue bytes");
    }
    for (std::size_t offset = 0; offset < kOverwriteSize; offset += 4) {
        const std::uint32_t instruction = read32(targetPrologue.data() + offset);
        if (isPcRelative(instruction)) {
            return failure(Arm64PlanStatus::kPcRelativeInstruction,
                           "stolen ARM64 prologue contains a PC-relative instruction");
        }
        if (isRegisterControlFlow(instruction)) {
            return failure(Arm64PlanStatus::kControlFlowInstruction,
                           "stolen ARM64 prologue contains a control-flow instruction");
        }
    }

    Arm64PatchPlan result;
    result.status = Arm64PlanStatus::kReady;
    result.message = "ARM64 patch bytes are relocatable; no executable memory was modified";
    result.overwriteSize = kOverwriteSize;
    result.resumeAddress = targetAddress + kOverwriteSize;
    result.entryPatch.reserve(kOverwriteSize);
    appendAbsoluteBranch(&result.entryPatch, replacementAddress);

    // A standalone BTI landing pad makes the trampoline valid as an indirect
    // call target. The exact OEM instructions, including PACIASP when present,
    // then execute before the absolute branch back to target+16.
    result.trampoline.reserve(4 + kOverwriteSize + 16);
    append32(&result.trampoline, kBtiCall);
    result.trampoline.insert(result.trampoline.end(), targetPrologue.begin(),
                             targetPrologue.begin() + kOverwriteSize);
    appendAbsoluteBranch(&result.trampoline, result.resumeAddress);
    return result;
}

const char* arm64PlanStatusName(Arm64PlanStatus status) {
    switch (status) {
        case Arm64PlanStatus::kReady: return "ready";
        case Arm64PlanStatus::kInvalidAddress: return "invalid_address";
        case Arm64PlanStatus::kInsufficientPrologue: return "insufficient_prologue";
        case Arm64PlanStatus::kPcRelativeInstruction: return "pc_relative_instruction";
        case Arm64PlanStatus::kControlFlowInstruction: return "control_flow_instruction";
    }
    return "unknown";
}

}  // namespace vcam::runtime
