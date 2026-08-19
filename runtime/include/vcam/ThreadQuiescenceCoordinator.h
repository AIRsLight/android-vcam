#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vcam::runtime {

enum class ThreadQuiescenceState {
    kEmpty = 0,
    kPrepared,
    kExclusive,
    kReleased,
    kFailed,
};

enum class ThreadQuiescenceStatus {
    kOk = 0,
    kInvalidState,
    kInvalidConfiguration,
    kInvalidBackend,
    kEnumerationFailed,
    kThreadCapacityExceeded,
    kThreadInventoryInvalid,
    kParkRequestFailed,
    kParkWaitFailed,
    kParkedInventoryMismatch,
    kParkedProgramCounterInvalid,
    kThreadInTargetRange,
    kThreadInventoryChanged,
    kThreadSetUnstable,
    kResumeFailed,
};

struct ThreadQuiescenceResult {
    ThreadQuiescenceStatus status = ThreadQuiescenceStatus::kInvalidState;
    ThreadQuiescenceState state = ThreadQuiescenceState::kEmpty;
    const char* message = "";
    std::size_t parkedThreadCount = 0;
    std::int32_t unsafeThreadId = -1;
    std::uintptr_t unsafeProgramCounter = 0;

    explicit operator bool() const { return status == ThreadQuiescenceStatus::kOk; }
};

struct ParkedThreadSnapshot {
    std::int32_t threadId = -1;
    std::uintptr_t programCounter = 0;
    std::uint32_t epoch = 0;
};

using EnumerateProcessThreads = bool (*)(
        void* context,
        std::int32_t* output,
        std::size_t capacity,
        std::size_t* count) noexcept;
using RequestThreadPark = bool (*)(
        void* context, std::int32_t threadId, std::uint32_t epoch) noexcept;
using WaitForParkedThreads = bool (*)(
        void* context,
        std::uint32_t epoch,
        const std::int32_t* requestedThreadIds,
        std::size_t requestedThreadCount,
        ParkedThreadSnapshot* output,
        std::size_t capacity,
        std::size_t* parkedThreadCount,
        std::uint32_t timeoutMilliseconds) noexcept;
using ResumeParkedThreads = bool (*)(void* context, std::uint32_t epoch) noexcept;

struct ThreadQuiescenceBackend {
    // Every callback used by enter()/leave() must avoid dynamic allocation and
    // must not create a process thread. A failed park request is treated as
    // potentially delivered, so resumeParkedThreads must be idempotent.
    void* context = nullptr;
    EnumerateProcessThreads enumerateThreads = nullptr;
    RequestThreadPark requestThreadPark = nullptr;
    WaitForParkedThreads waitForParkedThreads = nullptr;
    ResumeParkedThreads resumeParkedThreads = nullptr;
};

struct ThreadQuiescenceConfiguration {
    std::uintptr_t targetAddress = 0;
    std::size_t targetSize = 0;
    std::int32_t currentThreadId = -1;
    std::uint32_t epoch = 0;
    std::size_t maximumThreads = 0;
    std::size_t maximumStabilizationPasses = 0;
    std::uint32_t waitTimeoutMilliseconds = 0;
};

// Platform-neutral stop-the-world protocol. prepare() performs allocation and a
// read-only inventory check. enter() uses only prepared buffers, parks every
// peer, rejects PCs in the patch range and repeats enumeration until no new
// thread can be observed. No OS signal or futex implementation is supplied here.
class ThreadQuiescenceCoordinator final {
public:
    ThreadQuiescenceCoordinator(
            ThreadQuiescenceConfiguration configuration,
            ThreadQuiescenceBackend backend);
    ~ThreadQuiescenceCoordinator();

    ThreadQuiescenceCoordinator(const ThreadQuiescenceCoordinator&) = delete;
    ThreadQuiescenceCoordinator& operator=(const ThreadQuiescenceCoordinator&) = delete;

    ThreadQuiescenceState state() const noexcept { return state_; }
    ThreadQuiescenceResult prepare();
    ThreadQuiescenceResult enter() noexcept;
    ThreadQuiescenceResult leave() noexcept;

private:
    ThreadQuiescenceResult result(
            ThreadQuiescenceStatus status, const char* message) const noexcept;
    ThreadQuiescenceResult abortEnter(
            ThreadQuiescenceStatus status, const char* message) noexcept;
    bool readInventory(std::vector<std::int32_t>* buffer, std::size_t* count) noexcept;
    bool validInventory(std::vector<std::int32_t>* buffer, std::size_t count) const noexcept;
    bool finalInventoryContainsEveryRequested(std::size_t finalCount) const noexcept;
    bool finalInventoryIsExact(std::size_t finalCount) const noexcept;

    ThreadQuiescenceConfiguration configuration_;
    ThreadQuiescenceBackend backend_;
    std::vector<std::int32_t> inventory_;
    std::vector<std::int32_t> finalInventory_;
    std::vector<std::int32_t> requestedThreadIds_;
    std::vector<ParkedThreadSnapshot> parkedThreads_;
    std::size_t requestedThreadCount_ = 0;
    std::size_t parkedThreadCount_ = 0;
    bool parkAttempted_ = false;
    ThreadQuiescenceState state_ = ThreadQuiescenceState::kEmpty;
};

const char* threadQuiescenceStateName(ThreadQuiescenceState state) noexcept;
const char* threadQuiescenceStatusName(ThreadQuiescenceStatus status) noexcept;

}  // namespace vcam::runtime
