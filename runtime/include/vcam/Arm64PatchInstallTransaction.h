#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "vcam/Arm64PatchPlanner.h"

namespace vcam::runtime {

enum class PatchInstallState {
    kEmpty = 0,
    kPrepared,
    kCommitted,
    kRolledBack,
    kFailed,
};

enum class PatchInstallStatus {
    kOk = 0,
    kInvalidState,
    kInvalidPlan,
    kInvalidBackend,
    kReadFailed,
    kTargetMismatch,
    kCoordinationFailed,
    kCoordinationReleaseFailed,
    kTrampolineWriteFailed,
    kTrampolineBindFailed,
    kCacheSyncFailed,
    kPublishFailed,
    kEntryWriteFailed,
    kCommitRolledBack,
    kRollbackTargetMismatch,
    kRollbackWriteFailed,
    kRollbackFailed,
};

struct PatchInstallResult {
    PatchInstallStatus status = PatchInstallStatus::kInvalidState;
    PatchInstallState state = PatchInstallState::kEmpty;
    const char* message = "";

    explicit operator bool() const { return status == PatchInstallStatus::kOk; }
};

using ReadPatchMemory = bool (*)(
        void* context, std::uintptr_t address, void* output, std::size_t size) noexcept;
using WritePatchMemory = bool (*)(
        void* context, std::uintptr_t address, const void* input, std::size_t size) noexcept;
using SyncPatchInstructionCache = bool (*)(
        void* context, std::uintptr_t address, std::size_t size) noexcept;
using EnterPatchExclusiveWindow = bool (*)(void* context) noexcept;
using LeavePatchExclusiveWindow = bool (*)(void* context) noexcept;
using PublishOriginalTrampoline = bool (*)(
        void* context, std::uintptr_t trampolineAddress) noexcept;
using BindPrecompiledTrampolineResume = bool (*)(
        void* context, std::uintptr_t resumeAddress) noexcept;

struct PrecompiledArm64Trampoline {
    void* context = nullptr;
    std::uintptr_t entryAddress = 0;
    std::size_t codeSize = 0;
    std::array<std::uint8_t, 16> relocatedOriginalBytes {};
    BindPrecompiledTrampolineResume bindResumeAddress = nullptr;
};

struct PatchInstallBackend {
    // A false write result is treated as potentially partial. enterExclusiveWindow
    // must keep every other thread out of the target and trampoline ranges until
    // leaveExclusiveWindow. Once published, trampoline storage must remain valid
    // for the rest of the process lifetime.
    void* context = nullptr;
    ReadPatchMemory readMemory = nullptr;
    WritePatchMemory writeMemory = nullptr;
    SyncPatchInstructionCache synchronizeInstructionCache = nullptr;
    EnterPatchExclusiveWindow enterExclusiveWindow = nullptr;
    LeavePatchExclusiveWindow leaveExclusiveWindow = nullptr;
    PublishOriginalTrampoline publishOriginalTrampoline = nullptr;
};

// Transactional state machine only. It never changes page permissions, allocates
// executable memory or coordinates threads itself. All such operations must be
// supplied explicitly by a reviewed backend.
class Arm64PatchInstallTransaction final {
public:
    Arm64PatchInstallTransaction(
            std::uintptr_t targetAddress,
            std::uintptr_t trampolineAddress,
            Arm64PatchPlan plan,
            PatchInstallBackend backend);
    Arm64PatchInstallTransaction(
            std::uintptr_t targetAddress,
            PrecompiledArm64Trampoline trampoline,
            Arm64PatchPlan plan,
            PatchInstallBackend backend);

    PatchInstallState state() const noexcept { return state_; }
    PatchInstallResult prepare();
    PatchInstallResult commit() noexcept;
    PatchInstallResult rollback() noexcept;

private:
    PatchInstallResult result(PatchInstallStatus status, const char* message) const noexcept;
    PatchInstallResult automaticRollback(
            const char* restoredMessage, const char* failedMessage) noexcept;
    bool validPlan() const;

    std::uintptr_t targetAddress_ = 0;
    std::uintptr_t trampolineAddress_ = 0;
    PrecompiledArm64Trampoline precompiledTrampoline_;
    bool usesPrecompiledTrampoline_ = false;
    Arm64PatchPlan plan_;
    PatchInstallBackend backend_;
    std::vector<std::uint8_t> revalidationBuffer_;
    PatchInstallState state_ = PatchInstallState::kEmpty;
};

const char* patchInstallStateName(PatchInstallState state);
const char* patchInstallStatusName(PatchInstallStatus status);

}  // namespace vcam::runtime
