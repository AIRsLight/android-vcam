#include "vcam/Arm64PatchPlanner.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

std::uint64_t read64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned int byte = 0; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(bytes[offset + byte]) << (byte * 8);
    }
    return value;
}

void write32(std::vector<std::uint8_t>* bytes, std::uint32_t instruction) {
    for (unsigned int byte = 0; byte < 4; ++byte) {
        (*bytes)[byte] = static_cast<std::uint8_t>(instruction >> (byte * 8));
    }
}

}  // namespace

int main() {
    // Exact NX769J UKQ1 CameraService::onTransact prefix:
    // PACIASP; SUB SP,#0xa0; STP X29,X30,[SP,#0x40]; STP X28,X27,[SP,#0x50].
    const std::vector<std::uint8_t> nx769jPrologue = {
        0x3f, 0x23, 0x03, 0xd5,
        0xff, 0x83, 0x02, 0xd1,
        0xfd, 0x7b, 0x04, 0xa9,
        0xfc, 0x6f, 0x05, 0xa9,
    };
    constexpr std::uintptr_t target = 0x7100101798;
    constexpr std::uintptr_t replacement = 0x7200204000;
    const auto plan = vcam::runtime::planArm64InlineHook(
            target, replacement, nx769jPrologue);
    assert(plan.status == vcam::runtime::Arm64PlanStatus::kReady);
    assert(plan.overwriteSize == 16);
    assert(plan.resumeAddress == target + 16);
    assert(plan.originalBytes == nx769jPrologue);
    assert(plan.entryPatch.size() == 16);
    assert(plan.trampoline.size() == 36);
    assert(read64(plan.entryPatch, 8) == replacement);
    assert(read64(plan.trampoline, 28) == target + 16);
    assert(plan.trampoline[0] == 0x5f && plan.trampoline[1] == 0x24 &&
           plan.trampoline[2] == 0x03 && plan.trampoline[3] == 0xd5);  // BTI C

    for (const std::uint32_t instruction : {
            0x14000000u,  // B +0
            0x90000000u,  // ADRP X0,+0
            0x58000000u,  // LDR X0,+0
            0x54000000u,  // B.EQ +0
            0xb4000000u,  // CBZ X0,+0
            0x36000000u,  // TBZ W0,#0,+0
    }) {
        std::vector<std::uint8_t> pcRelative = nx769jPrologue;
        write32(&pcRelative, instruction);
        assert(vcam::runtime::planArm64InlineHook(
                target, replacement, pcRelative).status ==
               vcam::runtime::Arm64PlanStatus::kPcRelativeInstruction);
    }

    std::vector<std::uint8_t> earlyReturn = nx769jPrologue;
    write32(&earlyReturn, 0xd65f03c0);  // RET
    assert(vcam::runtime::planArm64InlineHook(target, replacement, earlyReturn).status ==
           vcam::runtime::Arm64PlanStatus::kControlFlowInstruction);

    assert(vcam::runtime::planArm64InlineHook(
            target, replacement, {0x1f, 0x20, 0x03, 0xd5}).status ==
           vcam::runtime::Arm64PlanStatus::kInsufficientPrologue);
    assert(vcam::runtime::planArm64InlineHook(
            target + 1, replacement, nx769jPrologue).status ==
           vcam::runtime::Arm64PlanStatus::kInvalidAddress);
    return 0;
}
