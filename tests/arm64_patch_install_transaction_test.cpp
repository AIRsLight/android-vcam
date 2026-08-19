#include "vcam/Arm64PatchInstallTransaction.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uintptr_t kTargetAddress = 0x1000;
constexpr std::uintptr_t kTrampolineAddress = 0x2000;

const std::vector<std::uint8_t> kOriginal = {
    0x3f, 0x23, 0x03, 0xd5,
    0xff, 0x83, 0x02, 0xd1,
    0xfd, 0x7b, 0x04, 0xa9,
    0xfc, 0x6f, 0x05, 0xa9,
};

struct FakeMemory {
    std::vector<std::uint8_t> target = kOriginal;
    std::vector<std::uint8_t> trampoline = std::vector<std::uint8_t>(64, 0);
    std::vector<std::uint8_t> expectedEntry;
    std::vector<std::string> events;
    std::uintptr_t publishedTrampoline = 0;
    bool exclusive = false;
    bool allowExclusive = true;
    bool allowLeave = true;
    bool allowPublish = true;
    bool allowBind = true;
    bool failEntryWrite = false;
    bool failEntrySyncOnce = false;
    bool failRollbackWrite = false;
    std::uintptr_t boundResume = 0;
    std::size_t leaveCalls = 0;
};

bool readMemory(
        void* context, std::uintptr_t address, void* output, std::size_t size) noexcept {
    auto& memory = *static_cast<FakeMemory*>(context);
    memory.events.push_back("read");
    if (address != kTargetAddress || size != memory.target.size()) {
        return false;
    }
    std::memcpy(output, memory.target.data(), size);
    return true;
}

bool writeMemory(
        void* context, std::uintptr_t address, const void* input, std::size_t size) noexcept {
    auto& memory = *static_cast<FakeMemory*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(input);
    if (address == kTrampolineAddress && size <= memory.trampoline.size()) {
        memory.events.push_back("write_trampoline");
        std::copy(bytes, bytes + size, memory.trampoline.begin());
        return true;
    }
    if (address != kTargetAddress || size != memory.target.size()) {
        return false;
    }
    const bool isEntry = std::equal(bytes, bytes + size, memory.expectedEntry.begin());
    memory.events.push_back(isEntry ? "write_entry" : "write_original");
    if (isEntry && memory.failEntryWrite) {
        std::copy(bytes, bytes + 4, memory.target.begin());
        return false;
    }
    if (!isEntry && memory.failRollbackWrite) {
        return false;
    }
    std::copy(bytes, bytes + size, memory.target.begin());
    return true;
}

bool synchronizeInstructionCache(
        void* context, std::uintptr_t address, std::size_t) noexcept {
    auto& memory = *static_cast<FakeMemory*>(context);
    memory.events.push_back(address == kTargetAddress ? "sync_target" : "sync_trampoline");
    if (address == kTargetAddress && memory.failEntrySyncOnce &&
        memory.target == memory.expectedEntry) {
        memory.failEntrySyncOnce = false;
        return false;
    }
    return true;
}

bool enterExclusiveWindow(void* context) noexcept {
    auto& memory = *static_cast<FakeMemory*>(context);
    memory.events.push_back("enter");
    if (!memory.allowExclusive || memory.exclusive) {
        return false;
    }
    memory.exclusive = true;
    return true;
}

bool leaveExclusiveWindow(void* context) noexcept {
    auto& memory = *static_cast<FakeMemory*>(context);
    assert(memory.exclusive);
    memory.events.push_back("leave");
    ++memory.leaveCalls;
    if (!memory.allowLeave) {
        return false;
    }
    memory.exclusive = false;
    return true;
}

bool publishOriginalTrampoline(
        void* context, std::uintptr_t trampolineAddress) noexcept {
    auto& memory = *static_cast<FakeMemory*>(context);
    memory.events.push_back("publish");
    if (!memory.allowPublish) {
        return false;
    }
    memory.publishedTrampoline = trampolineAddress;
    return true;
}

bool bindPrecompiledResume(
        void* context, std::uintptr_t resumeAddress) noexcept {
    auto& memory = *static_cast<FakeMemory*>(context);
    memory.events.push_back("bind_resume");
    if (!memory.allowBind || resumeAddress != kTargetAddress + kOriginal.size()) {
        return false;
    }
    if (memory.boundResume == 0) {
        memory.boundResume = resumeAddress;
    }
    return memory.boundResume == resumeAddress;
}

vcam::runtime::PatchInstallBackend backend(FakeMemory* memory) {
    return {
        memory,
        &readMemory,
        &writeMemory,
        &synchronizeInstructionCache,
        &enterExclusiveWindow,
        &leaveExclusiveWindow,
        &publishOriginalTrampoline,
    };
}

vcam::runtime::Arm64PatchPlan plan() {
    return vcam::runtime::planArm64InlineHook(
            kTargetAddress, 0x3000, kOriginal);
}

vcam::runtime::Arm64PatchInstallTransaction transaction(
        FakeMemory* memory, vcam::runtime::Arm64PatchPlan value) {
    memory->expectedEntry = value.entryPatch;
    return {kTargetAddress, kTrampolineAddress, std::move(value), backend(memory)};
}

vcam::runtime::Arm64PatchInstallTransaction precompiledTransaction(
        FakeMemory* memory, vcam::runtime::Arm64PatchPlan value) {
    memory->expectedEntry = value.entryPatch;
    vcam::runtime::PrecompiledArm64Trampoline trampoline;
    trampoline.context = memory;
    trampoline.entryAddress = 0x4000;
    trampoline.codeSize = 48;
    std::copy(kOriginal.begin(), kOriginal.end(),
              trampoline.relocatedOriginalBytes.begin());
    trampoline.bindResumeAddress = &bindPrecompiledResume;
    return {kTargetAddress, trampoline, std::move(value), backend(memory)};
}

bool contains(const std::vector<std::string>& events, const std::string& event) {
    return std::find(events.begin(), events.end(), event) != events.end();
}

}  // namespace

int main() {
    {
        FakeMemory memory;
        const auto expectedPlan = plan();
        auto install = transaction(&memory, expectedPlan);
        assert(install.prepare());
        assert(install.state() == vcam::runtime::PatchInstallState::kPrepared);
        assert(memory.events == std::vector<std::string>{"read"});
        assert(!contains(memory.events, "write_entry"));
        assert(!contains(memory.events, "write_trampoline"));

        assert(install.commit());
        assert(install.state() == vcam::runtime::PatchInstallState::kCommitted);
        assert(memory.target == expectedPlan.entryPatch);
        assert(std::equal(expectedPlan.trampoline.begin(), expectedPlan.trampoline.end(),
                          memory.trampoline.begin()));
        assert(memory.publishedTrampoline == kTrampolineAddress);
        assert(!memory.exclusive);
        assert((memory.events == std::vector<std::string>{
                "read", "enter", "read", "write_trampoline", "sync_trampoline",
                "publish", "write_entry", "sync_target", "leave"}));

        assert(install.rollback());
        assert(install.state() == vcam::runtime::PatchInstallState::kRolledBack);
        assert(memory.target == kOriginal);
        assert((memory.events == std::vector<std::string>{
                "read", "enter", "read", "write_trampoline", "sync_trampoline",
                "publish", "write_entry", "sync_target", "leave", "enter", "read",
                "write_original", "sync_target", "leave"}));
        assert(!install.rollback());
    }

    {
        FakeMemory memory;
        const auto expectedPlan = plan();
        auto install = precompiledTransaction(&memory, expectedPlan);
        assert(install.prepare());
        assert(install.commit());
        assert(memory.target == expectedPlan.entryPatch);
        assert(memory.boundResume == expectedPlan.resumeAddress);
        assert(memory.publishedTrampoline == 0x4000);
        assert(!contains(memory.events, "write_trampoline"));
        assert(!contains(memory.events, "sync_trampoline"));
        assert((memory.events == std::vector<std::string>{
                "read", "enter", "read", "bind_resume", "publish",
                "write_entry", "sync_target", "leave"}));
    }

    {
        FakeMemory memory;
        memory.allowBind = false;
        auto install = precompiledTransaction(&memory, plan());
        assert(install.prepare());
        const auto commit = install.commit();
        assert(commit.status ==
               vcam::runtime::PatchInstallStatus::kTrampolineBindFailed);
        assert(commit.state == vcam::runtime::PatchInstallState::kFailed);
        assert(memory.target == kOriginal);
        assert(!contains(memory.events, "publish"));
        assert(!contains(memory.events, "write_entry"));
    }

    {
        FakeMemory memory;
        auto install = transaction(&memory, plan());
        assert(install.prepare());
        memory.target[0] ^= 0xff;
        const auto commit = install.commit();
        assert(commit.status == vcam::runtime::PatchInstallStatus::kTargetMismatch);
        assert(commit.state == vcam::runtime::PatchInstallState::kFailed);
        assert(!contains(memory.events, "write_entry"));
    }

    {
        FakeMemory memory;
        memory.failEntrySyncOnce = true;
        auto install = transaction(&memory, plan());
        assert(install.prepare());
        const auto commit = install.commit();
        assert(commit.status == vcam::runtime::PatchInstallStatus::kCommitRolledBack);
        assert(commit.state == vcam::runtime::PatchInstallState::kRolledBack);
        assert(memory.target == kOriginal);
        assert(contains(memory.events, "write_original"));
    }

    {
        FakeMemory memory;
        memory.failEntryWrite = true;
        memory.failRollbackWrite = true;
        auto install = transaction(&memory, plan());
        assert(install.prepare());
        const auto commit = install.commit();
        assert(commit.status == vcam::runtime::PatchInstallStatus::kRollbackFailed);
        assert(commit.state == vcam::runtime::PatchInstallState::kFailed);
    }

    {
        FakeMemory memory;
        memory.target[0] ^= 0xff;
        auto install = transaction(&memory, plan());
        const auto prepare = install.prepare();
        assert(prepare.status == vcam::runtime::PatchInstallStatus::kTargetMismatch);
        assert(prepare.state == vcam::runtime::PatchInstallState::kFailed);
    }

    {
        FakeMemory memory;
        memory.allowExclusive = false;
        auto install = transaction(&memory, plan());
        assert(install.prepare());
        const auto commit = install.commit();
        assert(commit.status == vcam::runtime::PatchInstallStatus::kCoordinationFailed);
        assert(commit.state == vcam::runtime::PatchInstallState::kPrepared);
        assert(memory.target == kOriginal);
    }

    {
        FakeMemory memory;
        memory.allowLeave = false;
        auto install = transaction(&memory, plan());
        assert(install.prepare());
        const auto commit = install.commit();
        assert(commit.status ==
               vcam::runtime::PatchInstallStatus::kCoordinationReleaseFailed);
        assert(commit.state == vcam::runtime::PatchInstallState::kFailed);
        assert(memory.target == memory.expectedEntry);
        assert(memory.leaveCalls >= 2);
    }

    {
        FakeMemory memory;
        auto overlapping = plan();
        memory.expectedEntry = overlapping.entryPatch;
        vcam::runtime::Arm64PatchInstallTransaction install(
                kTargetAddress, kTargetAddress + 4, std::move(overlapping), backend(&memory));
        const auto prepare = install.prepare();
        assert(prepare.status == vcam::runtime::PatchInstallStatus::kInvalidPlan);
    }

    {
        FakeMemory memory;
        auto corrupted = plan();
        corrupted.trampoline[4] ^= 0xff;
        auto install = transaction(&memory, std::move(corrupted));
        const auto prepare = install.prepare();
        assert(prepare.status == vcam::runtime::PatchInstallStatus::kInvalidPlan);
        assert(!contains(memory.events, "read"));
    }

    {
        FakeMemory memory;
        auto expectedPlan = plan();
        memory.expectedEntry = expectedPlan.entryPatch;
        vcam::runtime::PrecompiledArm64Trampoline trampoline;
        trampoline.context = &memory;
        trampoline.entryAddress = 0x4000;
        trampoline.codeSize = 48;
        std::copy(kOriginal.begin(), kOriginal.end(),
                  trampoline.relocatedOriginalBytes.begin());
        trampoline.relocatedOriginalBytes[0] ^= 0xff;
        trampoline.bindResumeAddress = &bindPrecompiledResume;
        vcam::runtime::Arm64PatchInstallTransaction install(
                kTargetAddress, trampoline, std::move(expectedPlan), backend(&memory));
        const auto prepare = install.prepare();
        assert(prepare.status == vcam::runtime::PatchInstallStatus::kInvalidPlan);
        assert(!contains(memory.events, "read"));
    }
    return 0;
}
