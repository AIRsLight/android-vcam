#include "vcam/Arm64PatchInstallTransaction.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace vcam::runtime {
namespace {

constexpr std::size_t kArm64OverwriteSize = 16;
constexpr std::size_t kAbsoluteBranchSize = 16;
constexpr std::uint32_t kBtiCall = 0xd503245f;
constexpr std::uint32_t kLoadIp1FromPcPlus8 = 0x58000051;
constexpr std::uint32_t kBranchIp1 = 0xd61f0220;

std::uint32_t read32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint64_t read64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned int byte = 0; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(bytes[offset + byte]) << (byte * 8);
    }
    return value;
}

bool rangeEnd(std::uintptr_t address, std::size_t size, std::uintptr_t* end) {
    if (size == 0 || address > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }
    *end = address + size;
    return true;
}

class ExclusiveWindow final {
public:
    explicit ExclusiveWindow(const PatchInstallBackend& backend) : backend_(backend) {}
    ~ExclusiveWindow() {
        if (entered_) {
            backend_.leaveExclusiveWindow(backend_.context);
        }
    }

    bool enter() {
        entered_ = backend_.enterExclusiveWindow(backend_.context);
        return entered_;
    }

    bool leave() {
        if (!entered_) {
            return true;
        }
        if (!backend_.leaveExclusiveWindow(backend_.context)) {
            return false;
        }
        entered_ = false;
        return true;
    }

private:
    const PatchInstallBackend& backend_;
    bool entered_ = false;
};

}  // namespace

Arm64PatchInstallTransaction::Arm64PatchInstallTransaction(
        std::uintptr_t targetAddress,
        std::uintptr_t trampolineAddress,
        Arm64PatchPlan plan,
        PatchInstallBackend backend)
    : targetAddress_(targetAddress),
      trampolineAddress_(trampolineAddress),
      plan_(std::move(plan)),
      backend_(backend) {}

Arm64PatchInstallTransaction::Arm64PatchInstallTransaction(
        std::uintptr_t targetAddress,
        PrecompiledArm64Trampoline trampoline,
        Arm64PatchPlan plan,
        PatchInstallBackend backend)
    : targetAddress_(targetAddress),
      trampolineAddress_(trampoline.entryAddress),
      precompiledTrampoline_(trampoline),
      usesPrecompiledTrampoline_(true),
      plan_(std::move(plan)),
      backend_(backend) {}

PatchInstallResult Arm64PatchInstallTransaction::result(
        PatchInstallStatus status, const char* message) const noexcept {
    PatchInstallResult value;
    value.status = status;
    value.state = state_;
    value.message = message;
    return value;
}

bool Arm64PatchInstallTransaction::validPlan() const {
    if (plan_.status != Arm64PlanStatus::kReady || targetAddress_ == 0 ||
        trampolineAddress_ == 0 || (targetAddress_ & 3u) != 0 ||
        (trampolineAddress_ & 3u) != 0 ||
        plan_.overwriteSize != kArm64OverwriteSize ||
        plan_.entryPatch.size() != plan_.overwriteSize ||
        plan_.originalBytes.size() != plan_.overwriteSize ||
        plan_.trampoline.size() !=
                sizeof(std::uint32_t) + plan_.overwriteSize + kAbsoluteBranchSize) {
        return false;
    }
    std::uintptr_t targetEnd = 0;
    if (!rangeEnd(targetAddress_, plan_.overwriteSize, &targetEnd) ||
        plan_.resumeAddress != targetEnd) {
        return false;
    }
    const std::size_t trampolineBranch = sizeof(std::uint32_t) + plan_.overwriteSize;
    const std::uint64_t replacementAddress = read64(plan_.entryPatch, 8);
    const bool exactPlannerShape =
            read32(plan_.entryPatch, 0) == kLoadIp1FromPcPlus8 &&
            read32(plan_.entryPatch, 4) == kBranchIp1 &&
            replacementAddress != 0 && (replacementAddress & 3u) == 0 &&
            read32(plan_.trampoline, 0) == kBtiCall &&
            std::equal(plan_.originalBytes.begin(), plan_.originalBytes.end(),
                       plan_.trampoline.begin() + sizeof(std::uint32_t)) &&
            read32(plan_.trampoline, trampolineBranch) == kLoadIp1FromPcPlus8 &&
            read32(plan_.trampoline, trampolineBranch + sizeof(std::uint32_t)) ==
                    kBranchIp1 &&
            read64(plan_.trampoline, trampolineBranch + 8) == plan_.resumeAddress;
    if (!exactPlannerShape) {
        return false;
    }
    if (usesPrecompiledTrampoline_) {
        std::uintptr_t precompiledEnd = 0;
        return precompiledTrampoline_.bindResumeAddress != nullptr &&
                rangeEnd(trampolineAddress_, precompiledTrampoline_.codeSize,
                         &precompiledEnd) &&
                std::equal(plan_.originalBytes.begin(), plan_.originalBytes.end(),
                           precompiledTrampoline_.relocatedOriginalBytes.begin()) &&
                (targetEnd <= trampolineAddress_ || precompiledEnd <= targetAddress_);
    }
    std::uintptr_t trampolineEnd = 0;
    return rangeEnd(trampolineAddress_, plan_.trampoline.size(), &trampolineEnd) &&
            (targetEnd <= trampolineAddress_ || trampolineEnd <= targetAddress_);
}

PatchInstallResult Arm64PatchInstallTransaction::prepare() {
    if (state_ != PatchInstallState::kEmpty) {
        return result(PatchInstallStatus::kInvalidState,
                      "patch transaction can only be prepared once");
    }
    if (!validPlan()) {
        state_ = PatchInstallState::kFailed;
        return result(PatchInstallStatus::kInvalidPlan,
                      "patch plan or target/trampoline range is invalid");
    }
    if (backend_.readMemory == nullptr || backend_.writeMemory == nullptr ||
        backend_.synchronizeInstructionCache == nullptr ||
        backend_.enterExclusiveWindow == nullptr ||
        backend_.leaveExclusiveWindow == nullptr ||
        backend_.publishOriginalTrampoline == nullptr) {
        state_ = PatchInstallState::kFailed;
        return result(PatchInstallStatus::kInvalidBackend,
                      "patch backend is missing a required operation");
    }
    revalidationBuffer_.assign(plan_.overwriteSize, 0);
    if (!backend_.readMemory(
                backend_.context, targetAddress_, revalidationBuffer_.data(),
                revalidationBuffer_.size())) {
        state_ = PatchInstallState::kFailed;
        return result(PatchInstallStatus::kReadFailed,
                      "could not snapshot target bytes during prepare");
    }
    if (revalidationBuffer_ != plan_.originalBytes) {
        state_ = PatchInstallState::kFailed;
        return result(PatchInstallStatus::kTargetMismatch,
                      "target bytes no longer match the reviewed plan");
    }
    state_ = PatchInstallState::kPrepared;
    return result(PatchInstallStatus::kOk,
                  "patch transaction prepared without writing memory");
}

PatchInstallResult Arm64PatchInstallTransaction::automaticRollback(
        const char* restoredMessage, const char* failedMessage) noexcept {
    const bool wroteOriginal = backend_.writeMemory(
            backend_.context, targetAddress_, plan_.originalBytes.data(),
            plan_.originalBytes.size());
    const bool synchronized = wroteOriginal && backend_.synchronizeInstructionCache(
            backend_.context, targetAddress_, plan_.originalBytes.size());
    if (synchronized) {
        state_ = PatchInstallState::kRolledBack;
        return result(PatchInstallStatus::kCommitRolledBack, restoredMessage);
    }
    state_ = PatchInstallState::kFailed;
    return result(PatchInstallStatus::kRollbackFailed, failedMessage);
}

PatchInstallResult Arm64PatchInstallTransaction::commit() noexcept {
    if (state_ != PatchInstallState::kPrepared) {
        return result(PatchInstallStatus::kInvalidState,
                      "patch transaction is not prepared");
    }
    ExclusiveWindow exclusive(backend_);
    if (!exclusive.enter()) {
        return result(PatchInstallStatus::kCoordinationFailed,
                      "exclusive patch window was not acquired");
    }
    const auto finishExclusive = [&](PatchInstallResult outcome) noexcept {
        if (exclusive.leave()) {
            return outcome;
        }
        state_ = PatchInstallState::kFailed;
        return result(PatchInstallStatus::kCoordinationReleaseFailed,
                      "exclusive patch window could not release every peer thread");
    };

    if (!backend_.readMemory(
                backend_.context, targetAddress_, revalidationBuffer_.data(),
                revalidationBuffer_.size())) {
        state_ = PatchInstallState::kFailed;
        return finishExclusive(result(
                PatchInstallStatus::kReadFailed,
                "could not revalidate target bytes inside exclusive window"));
    }
    if (revalidationBuffer_ != plan_.originalBytes) {
        state_ = PatchInstallState::kFailed;
        return finishExclusive(result(
                PatchInstallStatus::kTargetMismatch,
                "target changed between prepare and commit"));
    }
    if (usesPrecompiledTrampoline_) {
        if (!precompiledTrampoline_.bindResumeAddress(
                    precompiledTrampoline_.context, plan_.resumeAddress)) {
            state_ = PatchInstallState::kFailed;
            return finishExclusive(result(
                    PatchInstallStatus::kTrampolineBindFailed,
                    "precompiled trampoline rejected the resume address"));
        }
    } else {
        if (!backend_.writeMemory(
                    backend_.context, trampolineAddress_, plan_.trampoline.data(),
                    plan_.trampoline.size())) {
            state_ = PatchInstallState::kFailed;
            return finishExclusive(result(
                    PatchInstallStatus::kTrampolineWriteFailed,
                    "trampoline write failed before target modification"));
        }
        if (!backend_.synchronizeInstructionCache(
                    backend_.context, trampolineAddress_, plan_.trampoline.size())) {
            state_ = PatchInstallState::kFailed;
            return finishExclusive(result(
                    PatchInstallStatus::kCacheSyncFailed,
                    "trampoline cache synchronization failed before target modification"));
        }
    }
    if (!backend_.publishOriginalTrampoline(backend_.context, trampolineAddress_)) {
        state_ = PatchInstallState::kFailed;
        return finishExclusive(result(
                PatchInstallStatus::kPublishFailed,
                "original trampoline publication failed before target modification"));
    }
    if (!backend_.writeMemory(
                backend_.context, targetAddress_, plan_.entryPatch.data(),
                plan_.entryPatch.size())) {
        return finishExclusive(automaticRollback(
                "entry patch write failed; original bytes restored",
                "entry patch write failed and automatic rollback failed"));
    }
    if (!backend_.synchronizeInstructionCache(
                backend_.context, targetAddress_, plan_.entryPatch.size())) {
        return finishExclusive(automaticRollback(
                "entry cache synchronization failed; original bytes restored",
                "entry cache synchronization and automatic rollback failed"));
    }
    state_ = PatchInstallState::kCommitted;
    return finishExclusive(result(
            PatchInstallStatus::kOk,
            "entry patch committed inside the exclusive window"));
}

PatchInstallResult Arm64PatchInstallTransaction::rollback() noexcept {
    if (state_ != PatchInstallState::kCommitted) {
        return result(PatchInstallStatus::kInvalidState,
                      "only a committed patch can be rolled back");
    }
    ExclusiveWindow exclusive(backend_);
    if (!exclusive.enter()) {
        return result(PatchInstallStatus::kCoordinationFailed,
                      "exclusive rollback window was not acquired");
    }
    const auto finishExclusive = [&](PatchInstallResult outcome) noexcept {
        if (exclusive.leave()) {
            return outcome;
        }
        state_ = PatchInstallState::kFailed;
        return result(PatchInstallStatus::kCoordinationReleaseFailed,
                      "exclusive rollback window could not release every peer thread");
    };
    if (!backend_.readMemory(
                backend_.context, targetAddress_, revalidationBuffer_.data(),
                revalidationBuffer_.size())) {
        return finishExclusive(result(
                PatchInstallStatus::kReadFailed,
                "could not verify entry patch before rollback"));
    }
    if (revalidationBuffer_ != plan_.entryPatch) {
        state_ = PatchInstallState::kFailed;
        return finishExclusive(result(
                PatchInstallStatus::kRollbackTargetMismatch,
                "target no longer contains this transaction's entry patch"));
    }
    if (!backend_.writeMemory(
                backend_.context, targetAddress_, plan_.originalBytes.data(),
                plan_.originalBytes.size())) {
        state_ = PatchInstallState::kFailed;
        return finishExclusive(result(
                PatchInstallStatus::kRollbackWriteFailed,
                "rollback write failed or may be partial"));
    }
    if (!backend_.synchronizeInstructionCache(
                backend_.context, targetAddress_, plan_.originalBytes.size())) {
        state_ = PatchInstallState::kFailed;
        return finishExclusive(result(
                PatchInstallStatus::kRollbackFailed,
                "rollback cache synchronization failed"));
    }
    state_ = PatchInstallState::kRolledBack;
    return finishExclusive(result(
            PatchInstallStatus::kOk,
            "original target bytes restored inside the exclusive window"));
}

const char* patchInstallStateName(PatchInstallState state) {
    switch (state) {
        case PatchInstallState::kEmpty: return "empty";
        case PatchInstallState::kPrepared: return "prepared";
        case PatchInstallState::kCommitted: return "committed";
        case PatchInstallState::kRolledBack: return "rolled_back";
        case PatchInstallState::kFailed: return "failed";
    }
    return "unknown";
}

const char* patchInstallStatusName(PatchInstallStatus status) {
    switch (status) {
        case PatchInstallStatus::kOk: return "ok";
        case PatchInstallStatus::kInvalidState: return "invalid_state";
        case PatchInstallStatus::kInvalidPlan: return "invalid_plan";
        case PatchInstallStatus::kInvalidBackend: return "invalid_backend";
        case PatchInstallStatus::kReadFailed: return "read_failed";
        case PatchInstallStatus::kTargetMismatch: return "target_mismatch";
        case PatchInstallStatus::kCoordinationFailed: return "coordination_failed";
        case PatchInstallStatus::kCoordinationReleaseFailed:
            return "coordination_release_failed";
        case PatchInstallStatus::kTrampolineWriteFailed: return "trampoline_write_failed";
        case PatchInstallStatus::kTrampolineBindFailed: return "trampoline_bind_failed";
        case PatchInstallStatus::kCacheSyncFailed: return "cache_sync_failed";
        case PatchInstallStatus::kPublishFailed: return "publish_failed";
        case PatchInstallStatus::kEntryWriteFailed: return "entry_write_failed";
        case PatchInstallStatus::kCommitRolledBack: return "commit_rolled_back";
        case PatchInstallStatus::kRollbackTargetMismatch:
            return "rollback_target_mismatch";
        case PatchInstallStatus::kRollbackWriteFailed: return "rollback_write_failed";
        case PatchInstallStatus::kRollbackFailed: return "rollback_failed";
    }
    return "unknown";
}

}  // namespace vcam::runtime
