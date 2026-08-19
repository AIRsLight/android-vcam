#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "vcam/ThreadQuiescenceCoordinator.h"

namespace vcam::runtime {

struct ProcStatusSignalMasks {
    std::uint64_t blocked = 0;
    std::uint64_t pending = 0;
    std::uint64_t sharedPending = 0;
};

bool parseProcStatusSignalMasks(
        const std::string& text,
        ProcStatusSignalMasks* masks,
        std::string* error);

enum class SignalEligibilityStatus {
    kReady = 0,
    kInvalidSignal,
    kActionQueryFailed,
    kSignalAlreadyClaimed,
    kProcessStatusReadFailed,
    kThreadEnumerationFailed,
    kThreadInventoryChanged,
    kThreadStatusReadFailed,
    kSignalBlockedByThread,
    kSignalAlreadyPending,
    kNoEligibleRealtimeSignal,
    kUnsupportedPlatform,
};

struct SignalEligibilityResult {
    SignalEligibilityStatus status = SignalEligibilityStatus::kInvalidSignal;
    const char* message = "";
    int signalNumber = 0;
    std::int32_t offendingThreadId = -1;

    explicit operator bool() const { return status == SignalEligibilityStatus::kReady; }
};

SignalEligibilityResult inspectRealtimeSignalEligibility(int signalNumber);
SignalEligibilityResult selectEligibleRealtimeSignal();
const char* signalEligibilityStatusName(SignalEligibilityStatus status) noexcept;

enum class AndroidSignalBackendStatus {
    kReady = 0,
    kInvalidConfiguration,
    kUnsupportedPlatform,
    kSignalNotEligible,
    kAlreadyOwned,
    kHandlerInstallFailed,
    kHandlerVerificationFailed,
    kActiveEpoch,
    kSignalNotDrained,
    kHandlerRestoreFailed,
};

struct AndroidSignalBackendResult {
    AndroidSignalBackendStatus status = AndroidSignalBackendStatus::kInvalidConfiguration;
    const char* message = "";
    int signalNumber = 0;

    explicit operator bool() const { return status == AndroidSignalBackendStatus::kReady; }
};

// Android ARM64 implementation of ThreadQuiescenceBackend. Installation is
// explicit and process-global because a signal disposition is process-global.
// The owner must call shutdown() before unloading the containing shared object.
class AndroidSignalQuiescenceBackend final {
public:
    AndroidSignalQuiescenceBackend() = default;
    ~AndroidSignalQuiescenceBackend();

    AndroidSignalQuiescenceBackend(const AndroidSignalQuiescenceBackend&) = delete;
    AndroidSignalQuiescenceBackend& operator=(
            const AndroidSignalQuiescenceBackend&) = delete;

    AndroidSignalBackendResult prepare(
            int signalNumber,
            std::size_t maximumThreads,
            std::uint32_t resumeTimeoutMilliseconds);
    AndroidSignalBackendResult shutdown() noexcept;

    bool isPrepared() const noexcept { return prepared_; }
    int signalNumber() const noexcept { return signalNumber_; }
    ThreadQuiescenceBackend backend() noexcept;

private:
    static bool enumerateCallback(
            void* context,
            std::int32_t* output,
            std::size_t capacity,
            std::size_t* count) noexcept;
    static bool requestParkCallback(
            void* context, std::int32_t threadId, std::uint32_t epoch) noexcept;
    static bool waitForParkedCallback(
            void* context,
            std::uint32_t epoch,
            const std::int32_t* requestedThreadIds,
            std::size_t requestedThreadCount,
            ParkedThreadSnapshot* output,
            std::size_t capacity,
            std::size_t* parkedThreadCount,
            std::uint32_t timeoutMilliseconds) noexcept;
    static bool resumeCallback(void* context, std::uint32_t epoch) noexcept;

    bool ownsRuntime() const noexcept;

    bool prepared_ = false;
    int signalNumber_ = 0;
};

const char* androidSignalBackendStatusName(AndroidSignalBackendStatus status) noexcept;

}  // namespace vcam::runtime
