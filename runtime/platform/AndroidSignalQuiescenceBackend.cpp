#include "vcam/AndroidSignalQuiescenceBackend.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#endif

#if defined(__ANDROID__) && defined(__aarch64__)
#include <linux/futex.h>
#include <sys/ucontext.h>
#endif

namespace vcam::runtime {
namespace {

SignalEligibilityResult eligibilityFailure(
        SignalEligibilityStatus status,
        const char* message,
        int signalNumber,
        std::int32_t offendingThreadId = -1) {
    SignalEligibilityResult result;
    result.status = status;
    result.message = message;
    result.signalNumber = signalNumber;
    result.offendingThreadId = offendingThreadId;
    return result;
}

bool parseHexMask(const std::string& value, std::uint64_t* output) {
    const std::size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return false;
    }
    const std::size_t last = value.find_last_not_of(" \t\r");
    const std::string trimmed = value.substr(first, last - first + 1);
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(trimmed.c_str(), &end, 16);
    if (errno != 0 || end == trimmed.c_str() || *end != '\0') {
        return false;
    }
    *output = static_cast<std::uint64_t>(parsed);
    return true;
}

bool readTextFile(const std::string& path, std::string* output) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    output->assign(
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.eof();
}

bool enumerateThreadsAllocated(std::vector<std::int32_t>* output) {
#if !defined(__linux__)
    (void)output;
    return false;
#else
    output->clear();
    DIR* directory = opendir("/proc/self/task");
    if (directory == nullptr) {
        return false;
    }
    bool success = true;
    while (true) {
        errno = 0;
        dirent* entry = readdir(directory);
        if (entry == nullptr) {
            if (errno != 0) {
                success = false;
            }
            break;
        }
        errno = 0;
        char* end = nullptr;
        const long parsed = std::strtol(entry->d_name, &end, 10);
        if (errno == 0 && end != entry->d_name && *end == '\0' && parsed > 0 &&
            parsed <= std::numeric_limits<std::int32_t>::max()) {
            output->push_back(static_cast<std::int32_t>(parsed));
        }
    }
    closedir(directory);
    if (!success || output->empty()) {
        output->clear();
        return false;
    }
    std::sort(output->begin(), output->end());
    if (std::adjacent_find(output->begin(), output->end()) != output->end()) {
        output->clear();
        return false;
    }
    return true;
#endif
}

bool signalIsDefault(const struct sigaction& action) {
    return action.sa_handler == SIG_DFL;
}

#if defined(__ANDROID__) && defined(__aarch64__)

enum class PendingSignalScan {
    kClear = 0,
    kPending,
    kSnapshotError,
};

PendingSignalScan scanPendingSignal(int signalNumber) {
#if !defined(__linux__)
    (void)signalNumber;
    return PendingSignalScan::kSnapshotError;
#else
    const std::uint64_t bit = std::uint64_t{1} << (signalNumber - 1);
    std::string text;
    std::string parseError;
    ProcStatusSignalMasks masks;
    if (!readTextFile("/proc/self/status", &text) ||
        !parseProcStatusSignalMasks(text, &masks, &parseError)) {
        return PendingSignalScan::kSnapshotError;
    }
    if (((masks.pending | masks.sharedPending) & bit) != 0) {
        return PendingSignalScan::kPending;
    }
    std::vector<std::int32_t> before;
    std::vector<std::int32_t> after;
    if (!enumerateThreadsAllocated(&before)) {
        return PendingSignalScan::kSnapshotError;
    }
    for (const std::int32_t threadId : before) {
        text.clear();
        const std::string path =
                "/proc/self/task/" + std::to_string(threadId) + "/status";
        if (!readTextFile(path, &text) ||
            !parseProcStatusSignalMasks(text, &masks, &parseError)) {
            return PendingSignalScan::kSnapshotError;
        }
        if (((masks.pending | masks.sharedPending) & bit) != 0) {
            return PendingSignalScan::kPending;
        }
    }
    if (!enumerateThreadsAllocated(&after) || before != after) {
        return PendingSignalScan::kSnapshotError;
    }
    return PendingSignalScan::kClear;
#endif
}

#endif

#if defined(__ANDROID__) && defined(__aarch64__)

constexpr std::size_t kMaximumSignalParkSlots = 1024;

struct SignalParkSlot {
    alignas(4) std::int32_t threadId = 0;
    alignas(8) std::uintptr_t programCounter = 0;
    alignas(4) std::uint32_t observedEpoch = 0;
};

struct SignalParkRuntime {
    alignas(8) std::uintptr_t owner = 0;
    alignas(4) std::int32_t signalNumber = 0;
    alignas(4) std::uint32_t maximumThreads = 0;
    alignas(4) std::uint32_t resumeTimeoutMilliseconds = 0;
    alignas(4) std::uint32_t activeEpoch = 0;
    alignas(4) std::uint32_t releaseEpoch = 0;
    alignas(4) std::uint32_t parkedSequence = 0;
    alignas(4) std::uint32_t handlerCount = 0;
    struct sigaction previousAction {};
    SignalParkSlot slots[kMaximumSignalParkSlots];
};

SignalParkRuntime gSignalParkRuntime;

__attribute__((always_inline)) inline long rawSystemCall(
        long number,
        long argument0 = 0,
        long argument1 = 0,
        long argument2 = 0,
        long argument3 = 0,
        long argument4 = 0,
        long argument5 = 0) noexcept {
    register long x0 asm("x0") = argument0;
    register long x1 asm("x1") = argument1;
    register long x2 asm("x2") = argument2;
    register long x3 asm("x3") = argument3;
    register long x4 asm("x4") = argument4;
    register long x5 asm("x5") = argument5;
    register long x8 asm("x8") = number;
    asm volatile(
            "svc #0"
            : "+r"(x0)
            : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
            : "memory", "cc");
    return x0;
}

template <typename T>
T atomicLoad(const T* value, int order = __ATOMIC_ACQUIRE) noexcept {
    return __atomic_load_n(value, order);
}

template <typename T>
void atomicStore(T* value, T desired, int order = __ATOMIC_RELEASE) noexcept {
    __atomic_store_n(value, desired, order);
}

template <typename T>
T atomicAdd(T* value, T increment, int order = __ATOMIC_ACQ_REL) noexcept {
    return __atomic_add_fetch(value, increment, order);
}

template <typename T>
T atomicSubtract(T* value, T decrement, int order = __ATOMIC_ACQ_REL) noexcept {
    return __atomic_sub_fetch(value, decrement, order);
}

template <typename T>
bool atomicCompareExchange(T* value, T* expected, T desired) noexcept {
    return __atomic_compare_exchange_n(
            value, expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

int futexWake(std::uint32_t* address, int count) noexcept {
    return static_cast<int>(rawSystemCall(
            SYS_futex,
            reinterpret_cast<long>(address),
            FUTEX_WAKE_PRIVATE,
            count));
}

int futexWait(
        std::uint32_t* address,
        std::uint32_t expected,
        const struct timespec* timeout) noexcept {
    return static_cast<int>(rawSystemCall(
            SYS_futex,
            reinterpret_cast<long>(address),
            FUTEX_WAIT_PRIVATE,
            expected,
            reinterpret_cast<long>(timeout)));
}

std::int64_t monotonicNanoseconds() noexcept {
    struct timespec now {};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    return static_cast<std::int64_t>(now.tv_sec) * 1000000000LL + now.tv_nsec;
}

bool remainingTimeout(
        std::int64_t deadlineNanoseconds, struct timespec* remaining) noexcept {
    const std::int64_t now = monotonicNanoseconds();
    if (now < 0 || now >= deadlineNanoseconds) {
        return false;
    }
    const std::int64_t delta = deadlineNanoseconds - now;
    remaining->tv_sec = static_cast<time_t>(delta / 1000000000LL);
    remaining->tv_nsec = static_cast<long>(delta % 1000000000LL);
    return true;
}

SignalParkSlot* findSlot(std::int32_t threadId) noexcept {
    const std::uint32_t count = atomicLoad(&gSignalParkRuntime.maximumThreads);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (atomicLoad(&gSignalParkRuntime.slots[index].threadId) == threadId) {
            return &gSignalParkRuntime.slots[index];
        }
    }
    return nullptr;
}

void leaveSignalHandler() noexcept {
    atomicSubtract(&gSignalParkRuntime.handlerCount, std::uint32_t{1});
    futexWake(&gSignalParkRuntime.handlerCount, std::numeric_limits<int>::max());
}

void parkSignalHandler(int signalNumber, siginfo_t*, void* rawContext) {
    atomicAdd(&gSignalParkRuntime.handlerCount, std::uint32_t{1});
    const std::uint32_t epoch = atomicLoad(&gSignalParkRuntime.activeEpoch);
    if (epoch == 0 ||
        signalNumber != atomicLoad(&gSignalParkRuntime.signalNumber)) {
        leaveSignalHandler();
        return;
    }
    const auto threadId = static_cast<std::int32_t>(rawSystemCall(SYS_gettid));
    SignalParkSlot* slot = findSlot(threadId);
    if (slot == nullptr || rawContext == nullptr) {
        leaveSignalHandler();
        return;
    }
    const auto* context = static_cast<const ucontext_t*>(rawContext);
    atomicStore(&slot->programCounter,
                static_cast<std::uintptr_t>(context->uc_mcontext.pc));
    atomicStore(&slot->observedEpoch, epoch);
    atomicAdd(&gSignalParkRuntime.parkedSequence, std::uint32_t{1});
    futexWake(&gSignalParkRuntime.parkedSequence, std::numeric_limits<int>::max());

    while (atomicLoad(&gSignalParkRuntime.activeEpoch) == epoch &&
           atomicLoad(&gSignalParkRuntime.releaseEpoch) != epoch) {
        const std::uint32_t observed = atomicLoad(&gSignalParkRuntime.releaseEpoch);
        futexWait(&gSignalParkRuntime.releaseEpoch, observed, nullptr);
    }
    leaveSignalHandler();
}

bool handlerIsInstalled(const struct sigaction& action) noexcept {
    return (action.sa_flags & SA_SIGINFO) != 0 &&
            action.sa_sigaction == &parkSignalHandler;
}

void clearSlots(std::uint32_t count) noexcept {
    for (std::uint32_t index = 0; index < count; ++index) {
        atomicStore(&gSignalParkRuntime.slots[index].observedEpoch,
                    std::uint32_t{0});
        atomicStore(&gSignalParkRuntime.slots[index].programCounter,
                    std::uintptr_t{0});
        atomicStore(&gSignalParkRuntime.slots[index].threadId,
                    std::int32_t{0});
    }
}

bool waitForHandlersToExit(std::uint32_t timeoutMilliseconds) noexcept {
    const std::int64_t now = monotonicNanoseconds();
    if (now < 0) {
        return false;
    }
    const std::int64_t deadline =
            now + static_cast<std::int64_t>(timeoutMilliseconds) * 1000000LL;
    while (true) {
        const std::uint32_t count = atomicLoad(&gSignalParkRuntime.handlerCount);
        if (count == 0) {
            return true;
        }
        struct timespec remaining {};
        if (!remainingTimeout(deadline, &remaining)) {
            return false;
        }
        const int waitResult = futexWait(
                &gSignalParkRuntime.handlerCount, count, &remaining);
        if (waitResult < 0 && waitResult != -EAGAIN && waitResult != -EINTR) {
            return false;
        }
    }
}

bool resumeEpoch(std::uint32_t epoch) noexcept {
    const std::uint32_t active = atomicLoad(&gSignalParkRuntime.activeEpoch);
    if (active != 0 && active != epoch) {
        return false;
    }
    atomicStore(&gSignalParkRuntime.releaseEpoch, epoch);
    atomicStore(&gSignalParkRuntime.activeEpoch, std::uint32_t{0});
    futexWake(&gSignalParkRuntime.releaseEpoch, std::numeric_limits<int>::max());
    return waitForHandlersToExit(
            atomicLoad(&gSignalParkRuntime.resumeTimeoutMilliseconds));
}

bool enumerateThreadsRaw(
        std::int32_t* output,
        std::size_t capacity,
        std::size_t* count) noexcept {
    *count = 0;
    const int directory = static_cast<int>(rawSystemCall(
            SYS_openat,
            AT_FDCWD,
            reinterpret_cast<long>("/proc/self/task"),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC,
            0));
    if (directory < 0) {
        return false;
    }
    struct LinuxDirectoryEntry64 {
        std::uint64_t inode;
        std::int64_t offset;
        unsigned short recordLength;
        unsigned char type;
        char name[];
    };
    alignas(8) unsigned char buffer[8192];
    bool success = true;
    while (true) {
        const long bytes = rawSystemCall(
                SYS_getdents64,
                directory,
                reinterpret_cast<long>(buffer),
                sizeof(buffer));
        if (bytes == 0) {
            break;
        }
        if (bytes < 0) {
            success = false;
            break;
        }
        long offset = 0;
        while (offset < bytes) {
            const auto* entry = reinterpret_cast<const LinuxDirectoryEntry64*>(
                    buffer + offset);
            if (entry->recordLength < sizeof(LinuxDirectoryEntry64) + 1 ||
                offset + entry->recordLength > bytes) {
                success = false;
                break;
            }
            std::int64_t parsed = 0;
            bool numeric = entry->name[0] != '\0';
            for (const char* character = entry->name; numeric && *character != '\0';
                 ++character) {
                if (*character < '0' || *character > '9' ||
                    parsed > (std::numeric_limits<std::int32_t>::max() - 9) / 10) {
                    numeric = false;
                    break;
                }
                parsed = parsed * 10 + (*character - '0');
            }
            if (numeric && parsed > 0) {
                if (*count == capacity) {
                    *count = capacity + 1;
                    success = false;
                    break;
                }
                output[(*count)++] = static_cast<std::int32_t>(parsed);
            }
            offset += entry->recordLength;
        }
        if (!success) {
            break;
        }
    }
    rawSystemCall(SYS_close, directory);
    return success && *count != 0;
}

#endif

AndroidSignalBackendResult backendResult(
        AndroidSignalBackendStatus status,
        const char* message,
        int signalNumber) noexcept {
    AndroidSignalBackendResult result;
    result.status = status;
    result.message = message;
    result.signalNumber = signalNumber;
    return result;
}

}  // namespace

bool parseProcStatusSignalMasks(
        const std::string& text,
        ProcStatusSignalMasks* masks,
        std::string* error) {
    if (masks == nullptr) {
        if (error != nullptr) *error = "signal mask output is null";
        return false;
    }
    ProcStatusSignalMasks parsed;
    bool foundBlocked = false;
    bool foundPending = false;
    bool foundSharedPending = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string line = text.substr(start, end - start);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string key = line.substr(0, colon);
            std::uint64_t value = 0;
            if (key == "SigBlk" || key == "SigPnd" || key == "ShdPnd") {
                if (!parseHexMask(line.substr(colon + 1), &value)) {
                    if (error != nullptr) *error = "invalid hexadecimal signal mask";
                    return false;
                }
                if (key == "SigBlk") {
                    if (foundBlocked) return false;
                    parsed.blocked = value;
                    foundBlocked = true;
                } else if (key == "SigPnd") {
                    if (foundPending) return false;
                    parsed.pending = value;
                    foundPending = true;
                } else {
                    if (foundSharedPending) return false;
                    parsed.sharedPending = value;
                    foundSharedPending = true;
                }
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!foundBlocked || !foundPending || !foundSharedPending) {
        if (error != nullptr) *error = "status is missing a required signal mask";
        return false;
    }
    *masks = parsed;
    if (error != nullptr) error->clear();
    return true;
}

SignalEligibilityResult inspectRealtimeSignalEligibility(int signalNumber) {
#if !defined(__linux__)
    return eligibilityFailure(
            SignalEligibilityStatus::kUnsupportedPlatform,
            "signal eligibility requires Linux procfs and sigaction",
            signalNumber);
#else
    if (signalNumber < SIGRTMIN || signalNumber > SIGRTMAX || signalNumber > 64) {
        return eligibilityFailure(
                SignalEligibilityStatus::kInvalidSignal,
                "signal is outside the usable 64-bit real-time range",
                signalNumber);
    }
    struct sigaction action {};
    if (sigaction(signalNumber, nullptr, &action) != 0) {
        return eligibilityFailure(
                SignalEligibilityStatus::kActionQueryFailed,
                "could not query the real-time signal disposition",
                signalNumber);
    }
    if (!signalIsDefault(action)) {
        return eligibilityFailure(
                SignalEligibilityStatus::kSignalAlreadyClaimed,
                "real-time signal already has a process handler",
                signalNumber);
    }
    std::string processStatus;
    ProcStatusSignalMasks processMasks;
    std::string parseError;
    if (!readTextFile("/proc/self/status", &processStatus) ||
        !parseProcStatusSignalMasks(processStatus, &processMasks, &parseError)) {
        return eligibilityFailure(
                SignalEligibilityStatus::kProcessStatusReadFailed,
                "could not read process signal masks",
                signalNumber);
    }
    const std::uint64_t bit = std::uint64_t{1} << (signalNumber - 1);
    if (((processMasks.pending | processMasks.sharedPending) & bit) != 0) {
        return eligibilityFailure(
                SignalEligibilityStatus::kSignalAlreadyPending,
                "real-time signal is already pending for the process",
                signalNumber);
    }

    std::vector<std::int32_t> before;
    std::vector<std::int32_t> after;
    if (!enumerateThreadsAllocated(&before)) {
        return eligibilityFailure(
                SignalEligibilityStatus::kThreadEnumerationFailed,
                "could not enumerate threads for signal eligibility",
                signalNumber);
    }
    for (const std::int32_t threadId : before) {
        std::string status;
        ProcStatusSignalMasks masks;
        const std::string path =
                "/proc/self/task/" + std::to_string(threadId) + "/status";
        if (!readTextFile(path, &status) ||
            !parseProcStatusSignalMasks(status, &masks, &parseError)) {
            return eligibilityFailure(
                    SignalEligibilityStatus::kThreadStatusReadFailed,
                    "could not read a complete per-thread signal mask",
                    signalNumber,
                    threadId);
        }
        if ((masks.blocked & bit) != 0) {
            return eligibilityFailure(
                    SignalEligibilityStatus::kSignalBlockedByThread,
                    "real-time signal is blocked by a process thread",
                    signalNumber,
                    threadId);
        }
        if (((masks.pending | masks.sharedPending) & bit) != 0) {
            return eligibilityFailure(
                    SignalEligibilityStatus::kSignalAlreadyPending,
                    "real-time signal is pending for a process thread",
                    signalNumber,
                    threadId);
        }
    }
    if (!enumerateThreadsAllocated(&after)) {
        return eligibilityFailure(
                SignalEligibilityStatus::kThreadEnumerationFailed,
                "could not repeat the thread inventory for signal eligibility",
                signalNumber);
    }
    if (before != after) {
        return eligibilityFailure(
                SignalEligibilityStatus::kThreadInventoryChanged,
                "thread inventory changed during signal eligibility inspection",
                signalNumber);
    }
    SignalEligibilityResult ready;
    ready.status = SignalEligibilityStatus::kReady;
    ready.message = "real-time signal is default, unblocked and not pending";
    ready.signalNumber = signalNumber;
    return ready;
#endif
}

SignalEligibilityResult selectEligibleRealtimeSignal() {
#if !defined(__linux__)
    return eligibilityFailure(
            SignalEligibilityStatus::kUnsupportedPlatform,
            "real-time signal selection requires Linux",
            0);
#else
    for (int signalNumber = SIGRTMAX; signalNumber >= SIGRTMIN; --signalNumber) {
        SignalEligibilityResult candidate = inspectRealtimeSignalEligibility(signalNumber);
        if (candidate) {
            return candidate;
        }
    }
    return eligibilityFailure(
            SignalEligibilityStatus::kNoEligibleRealtimeSignal,
            "no process-wide eligible real-time signal was found",
            0);
#endif
}

const char* signalEligibilityStatusName(SignalEligibilityStatus status) noexcept {
    switch (status) {
        case SignalEligibilityStatus::kReady: return "ready";
        case SignalEligibilityStatus::kInvalidSignal: return "invalid_signal";
        case SignalEligibilityStatus::kActionQueryFailed: return "action_query_failed";
        case SignalEligibilityStatus::kSignalAlreadyClaimed: return "signal_already_claimed";
        case SignalEligibilityStatus::kProcessStatusReadFailed:
            return "process_status_read_failed";
        case SignalEligibilityStatus::kThreadEnumerationFailed:
            return "thread_enumeration_failed";
        case SignalEligibilityStatus::kThreadInventoryChanged:
            return "thread_inventory_changed";
        case SignalEligibilityStatus::kThreadStatusReadFailed:
            return "thread_status_read_failed";
        case SignalEligibilityStatus::kSignalBlockedByThread:
            return "signal_blocked_by_thread";
        case SignalEligibilityStatus::kSignalAlreadyPending:
            return "signal_already_pending";
        case SignalEligibilityStatus::kNoEligibleRealtimeSignal:
            return "no_eligible_realtime_signal";
        case SignalEligibilityStatus::kUnsupportedPlatform: return "unsupported_platform";
    }
    return "unknown";
}

AndroidSignalQuiescenceBackend::~AndroidSignalQuiescenceBackend() {
    shutdown();
}

bool AndroidSignalQuiescenceBackend::ownsRuntime() const noexcept {
#if defined(__ANDROID__) && defined(__aarch64__)
    return atomicLoad(&gSignalParkRuntime.owner) ==
            reinterpret_cast<std::uintptr_t>(this);
#else
    return false;
#endif
}

AndroidSignalBackendResult AndroidSignalQuiescenceBackend::prepare(
        int signalNumber,
        std::size_t maximumThreads,
        std::uint32_t resumeTimeoutMilliseconds) {
    if (prepared_ || maximumThreads == 0 ||
        maximumThreads > 1024 || resumeTimeoutMilliseconds == 0) {
        return backendResult(
                AndroidSignalBackendStatus::kInvalidConfiguration,
                "signal backend configuration or lifecycle state is invalid",
                signalNumber);
    }
#if !defined(__ANDROID__) || !defined(__aarch64__)
    (void)signalNumber;
    (void)maximumThreads;
    (void)resumeTimeoutMilliseconds;
    return backendResult(
            AndroidSignalBackendStatus::kUnsupportedPlatform,
            "signal parking backend is implemented only for Android ARM64",
            0);
#else
    const SignalEligibilityResult eligibility =
            inspectRealtimeSignalEligibility(signalNumber);
    if (!eligibility) {
        return backendResult(
                AndroidSignalBackendStatus::kSignalNotEligible,
                eligibility.message,
                signalNumber);
    }
    std::uintptr_t expectedOwner = 0;
    const std::uintptr_t owner = reinterpret_cast<std::uintptr_t>(this);
    if (!atomicCompareExchange(&gSignalParkRuntime.owner, &expectedOwner, owner)) {
        return backendResult(
                AndroidSignalBackendStatus::kAlreadyOwned,
                "another signal parking backend owns the process runtime",
                signalNumber);
    }

    atomicStore(&gSignalParkRuntime.signalNumber,
                static_cast<std::int32_t>(signalNumber));
    atomicStore(&gSignalParkRuntime.maximumThreads,
                static_cast<std::uint32_t>(maximumThreads));
    atomicStore(&gSignalParkRuntime.resumeTimeoutMilliseconds,
                resumeTimeoutMilliseconds);
    atomicStore(&gSignalParkRuntime.activeEpoch, std::uint32_t{0});
    atomicStore(&gSignalParkRuntime.releaseEpoch, std::uint32_t{0});
    atomicStore(&gSignalParkRuntime.parkedSequence, std::uint32_t{0});
    clearSlots(static_cast<std::uint32_t>(maximumThreads));

    struct sigaction action {};
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = &parkSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
    if (sigaction(signalNumber, &action, &gSignalParkRuntime.previousAction) != 0) {
        atomicStore(&gSignalParkRuntime.owner, std::uintptr_t{0});
        return backendResult(
                AndroidSignalBackendStatus::kHandlerInstallFailed,
                "could not install the signal parking handler",
                signalNumber);
    }
    if (!signalIsDefault(gSignalParkRuntime.previousAction)) {
        sigaction(signalNumber, &gSignalParkRuntime.previousAction, nullptr);
        atomicStore(&gSignalParkRuntime.owner, std::uintptr_t{0});
        return backendResult(
                AndroidSignalBackendStatus::kSignalNotEligible,
                "real-time signal was claimed during handler installation",
                signalNumber);
    }
    struct sigaction observed {};
    if (sigaction(signalNumber, nullptr, &observed) != 0 ||
        !handlerIsInstalled(observed)) {
        sigaction(signalNumber, &gSignalParkRuntime.previousAction, nullptr);
        atomicStore(&gSignalParkRuntime.owner, std::uintptr_t{0});
        return backendResult(
                AndroidSignalBackendStatus::kHandlerVerificationFailed,
                "installed signal handler could not be verified",
                signalNumber);
    }
    prepared_ = true;
    signalNumber_ = signalNumber;
    return backendResult(
            AndroidSignalBackendStatus::kReady,
            "Android ARM64 signal parking backend is prepared",
            signalNumber);
#endif
}

AndroidSignalBackendResult AndroidSignalQuiescenceBackend::shutdown() noexcept {
    if (!prepared_) {
        return backendResult(
                AndroidSignalBackendStatus::kReady,
                "signal parking backend is already inactive",
                signalNumber_);
    }
#if !defined(__ANDROID__) || !defined(__aarch64__)
    prepared_ = false;
    signalNumber_ = 0;
    return backendResult(
            AndroidSignalBackendStatus::kReady,
            "signal parking backend is inactive on this platform",
            0);
#else
    if (!ownsRuntime()) {
        prepared_ = false;
        signalNumber_ = 0;
        return backendResult(
                AndroidSignalBackendStatus::kAlreadyOwned,
                "signal parking runtime ownership changed unexpectedly",
                0);
    }
    const std::uint32_t activeEpoch = atomicLoad(&gSignalParkRuntime.activeEpoch);
    if (activeEpoch != 0 && !resumeEpoch(activeEpoch)) {
        return backendResult(
                AndroidSignalBackendStatus::kActiveEpoch,
                "active parked threads could not be resumed before shutdown",
                signalNumber_);
    }
    const std::int64_t drainStart = monotonicNanoseconds();
    const std::int64_t drainDeadline = drainStart < 0 ? -1 :
            drainStart + static_cast<std::int64_t>(
                    atomicLoad(&gSignalParkRuntime.resumeTimeoutMilliseconds)) *
                    1000000LL;
    while (scanPendingSignal(signalNumber_) != PendingSignalScan::kClear) {
        if (drainDeadline < 0 || monotonicNanoseconds() >= drainDeadline) {
            return backendResult(
                    AndroidSignalBackendStatus::kSignalNotDrained,
                    "signal remained pending or its stable snapshot kept failing",
                    signalNumber_);
        }
        const struct timespec pause {0, 1000000L};
        nanosleep(&pause, nullptr);
    }
    struct sigaction observed {};
    const bool ours = sigaction(signalNumber_, nullptr, &observed) == 0 &&
            handlerIsInstalled(observed);
    if (!ours ||
        sigaction(signalNumber_, &gSignalParkRuntime.previousAction, nullptr) != 0) {
        prepared_ = false;
        atomicStore(&gSignalParkRuntime.owner, std::uintptr_t{0});
        const int failedSignal = signalNumber_;
        signalNumber_ = 0;
        return backendResult(
                AndroidSignalBackendStatus::kHandlerRestoreFailed,
                "signal disposition changed or could not be restored",
                failedSignal);
    }
    prepared_ = false;
    atomicStore(&gSignalParkRuntime.signalNumber, std::int32_t{0});
    atomicStore(&gSignalParkRuntime.owner, std::uintptr_t{0});
    const int releasedSignal = signalNumber_;
    signalNumber_ = 0;
    return backendResult(
            AndroidSignalBackendStatus::kReady,
            "signal parking handler was restored",
            releasedSignal);
#endif
}

ThreadQuiescenceBackend AndroidSignalQuiescenceBackend::backend() noexcept {
    return {
        this,
        &AndroidSignalQuiescenceBackend::enumerateCallback,
        &AndroidSignalQuiescenceBackend::requestParkCallback,
        &AndroidSignalQuiescenceBackend::waitForParkedCallback,
        &AndroidSignalQuiescenceBackend::resumeCallback,
    };
}

bool AndroidSignalQuiescenceBackend::enumerateCallback(
        void* context,
        std::int32_t* output,
        std::size_t capacity,
        std::size_t* count) noexcept {
#if !defined(__ANDROID__) || !defined(__aarch64__)
    (void)context;
    (void)output;
    (void)capacity;
    if (count != nullptr) *count = 0;
    return false;
#else
    auto* owner = static_cast<AndroidSignalQuiescenceBackend*>(context);
    return owner != nullptr && owner->prepared_ && owner->ownsRuntime() &&
            output != nullptr && count != nullptr &&
            enumerateThreadsRaw(output, capacity, count);
#endif
}

bool AndroidSignalQuiescenceBackend::requestParkCallback(
        void* context, std::int32_t threadId, std::uint32_t epoch) noexcept {
#if !defined(__ANDROID__) || !defined(__aarch64__)
    (void)context;
    (void)threadId;
    (void)epoch;
    return false;
#else
    auto* owner = static_cast<AndroidSignalQuiescenceBackend*>(context);
    if (owner == nullptr || !owner->prepared_ || !owner->ownsRuntime() ||
        threadId <= 0 || epoch == 0) {
        return false;
    }
    std::uint32_t active = atomicLoad(&gSignalParkRuntime.activeEpoch);
    if (active == 0) {
        struct sigaction observed {};
        if (sigaction(owner->signalNumber_, nullptr, &observed) != 0 ||
            !handlerIsInstalled(observed)) {
            return false;
        }
        if (atomicLoad(&gSignalParkRuntime.handlerCount) != 0) {
            return false;
        }
        const std::uint32_t maximumThreads =
                atomicLoad(&gSignalParkRuntime.maximumThreads);
        clearSlots(maximumThreads);
        atomicStore(&gSignalParkRuntime.releaseEpoch, std::uint32_t{0});
        std::uint32_t expected = 0;
        if (!atomicCompareExchange(&gSignalParkRuntime.activeEpoch, &expected, epoch)) {
            active = expected;
        } else {
            active = epoch;
        }
    }
    if (active != epoch) {
        return false;
    }

    SignalParkSlot* slot = findSlot(threadId);
    if (slot == nullptr) {
        const std::uint32_t maximumThreads =
                atomicLoad(&gSignalParkRuntime.maximumThreads);
        for (std::uint32_t index = 0; index < maximumThreads; ++index) {
            std::int32_t expected = 0;
            if (atomicCompareExchange(
                        &gSignalParkRuntime.slots[index].threadId,
                        &expected,
                        threadId)) {
                slot = &gSignalParkRuntime.slots[index];
                break;
            }
        }
    }
    if (slot == nullptr) {
        return false;
    }
    atomicStore(&slot->programCounter, std::uintptr_t{0});
    atomicStore(&slot->observedEpoch, std::uint32_t{0});
    const long processId = rawSystemCall(SYS_getpid);
    return rawSystemCall(
            SYS_tgkill,
            processId,
            static_cast<pid_t>(threadId),
            owner->signalNumber_) == 0;
#endif
}

bool AndroidSignalQuiescenceBackend::waitForParkedCallback(
        void* context,
        std::uint32_t epoch,
        const std::int32_t* requestedThreadIds,
        std::size_t requestedThreadCount,
        ParkedThreadSnapshot* output,
        std::size_t capacity,
        std::size_t* parkedThreadCount,
        std::uint32_t timeoutMilliseconds) noexcept {
#if !defined(__ANDROID__) || !defined(__aarch64__)
    (void)context;
    (void)epoch;
    (void)requestedThreadIds;
    (void)requestedThreadCount;
    (void)output;
    (void)capacity;
    (void)timeoutMilliseconds;
    if (parkedThreadCount != nullptr) *parkedThreadCount = 0;
    return false;
#else
    auto* owner = static_cast<AndroidSignalQuiescenceBackend*>(context);
    if (parkedThreadCount == nullptr) {
        return false;
    }
    *parkedThreadCount = 0;
    if (owner == nullptr || !owner->prepared_ || !owner->ownsRuntime() ||
        epoch == 0 || requestedThreadIds == nullptr || output == nullptr ||
        requestedThreadCount > capacity || timeoutMilliseconds == 0 ||
        atomicLoad(&gSignalParkRuntime.activeEpoch) != epoch) {
        return false;
    }
    const std::int64_t now = monotonicNanoseconds();
    if (now < 0) {
        return false;
    }
    const std::int64_t deadline =
            now + static_cast<std::int64_t>(timeoutMilliseconds) * 1000000LL;
    while (true) {
        const std::uint32_t sequence =
                atomicLoad(&gSignalParkRuntime.parkedSequence);
        bool complete = true;
        for (std::size_t index = 0; index < requestedThreadCount; ++index) {
            SignalParkSlot* slot = findSlot(requestedThreadIds[index]);
            if (slot == nullptr || atomicLoad(&slot->observedEpoch) != epoch) {
                complete = false;
                break;
            }
            output[index].threadId = requestedThreadIds[index];
            output[index].programCounter = atomicLoad(&slot->programCounter);
            output[index].epoch = epoch;
        }
        if (complete) {
            *parkedThreadCount = requestedThreadCount;
            return true;
        }
        struct timespec remaining {};
        if (!remainingTimeout(deadline, &remaining)) {
            return false;
        }
        const int waitResult = futexWait(
                &gSignalParkRuntime.parkedSequence, sequence, &remaining);
        if (waitResult < 0 && waitResult != -EAGAIN && waitResult != -EINTR) {
            return false;
        }
    }
#endif
}

bool AndroidSignalQuiescenceBackend::resumeCallback(
        void* context, std::uint32_t epoch) noexcept {
#if !defined(__ANDROID__) || !defined(__aarch64__)
    (void)context;
    (void)epoch;
    return false;
#else
    auto* owner = static_cast<AndroidSignalQuiescenceBackend*>(context);
    return owner != nullptr && owner->prepared_ && owner->ownsRuntime() &&
            epoch != 0 && resumeEpoch(epoch);
#endif
}

const char* androidSignalBackendStatusName(AndroidSignalBackendStatus status) noexcept {
    switch (status) {
        case AndroidSignalBackendStatus::kReady: return "ready";
        case AndroidSignalBackendStatus::kInvalidConfiguration:
            return "invalid_configuration";
        case AndroidSignalBackendStatus::kUnsupportedPlatform:
            return "unsupported_platform";
        case AndroidSignalBackendStatus::kSignalNotEligible:
            return "signal_not_eligible";
        case AndroidSignalBackendStatus::kAlreadyOwned: return "already_owned";
        case AndroidSignalBackendStatus::kHandlerInstallFailed:
            return "handler_install_failed";
        case AndroidSignalBackendStatus::kHandlerVerificationFailed:
            return "handler_verification_failed";
        case AndroidSignalBackendStatus::kActiveEpoch: return "active_epoch";
        case AndroidSignalBackendStatus::kSignalNotDrained:
            return "signal_not_drained";
        case AndroidSignalBackendStatus::kHandlerRestoreFailed:
            return "handler_restore_failed";
    }
    return "unknown";
}

}  // namespace vcam::runtime
