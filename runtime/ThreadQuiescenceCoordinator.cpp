#include "vcam/ThreadQuiescenceCoordinator.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace vcam::runtime {
namespace {

constexpr std::size_t kMaximumSupportedThreads = 4096;
constexpr std::size_t kMaximumSupportedStabilizationPasses = 16;

bool rangeEnd(std::uintptr_t start, std::size_t size, std::uintptr_t* end) noexcept {
    if (size == 0 || start > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }
    *end = start + size;
    return true;
}

}  // namespace

ThreadQuiescenceCoordinator::ThreadQuiescenceCoordinator(
        ThreadQuiescenceConfiguration configuration,
        ThreadQuiescenceBackend backend)
    : configuration_(configuration), backend_(backend) {}

ThreadQuiescenceCoordinator::~ThreadQuiescenceCoordinator() {
    if (parkAttempted_ && backend_.resumeParkedThreads != nullptr) {
        backend_.resumeParkedThreads(backend_.context, configuration_.epoch);
    }
}

ThreadQuiescenceResult ThreadQuiescenceCoordinator::result(
        ThreadQuiescenceStatus status, const char* message) const noexcept {
    ThreadQuiescenceResult value;
    value.status = status;
    value.state = state_;
    value.message = message;
    value.parkedThreadCount = parkedThreadCount_;
    return value;
}

bool ThreadQuiescenceCoordinator::readInventory(
        std::vector<std::int32_t>* buffer, std::size_t* count) noexcept {
    *count = 0;
    return backend_.enumerateThreads(
            backend_.context, buffer->data(), buffer->size(), count);
}

bool ThreadQuiescenceCoordinator::validInventory(
        std::vector<std::int32_t>* buffer, std::size_t count) const noexcept {
    if (count == 0 || count > buffer->size()) {
        return false;
    }
    std::sort(buffer->begin(), buffer->begin() + count);
    if ((*buffer)[0] <= 0 ||
        std::adjacent_find(buffer->begin(), buffer->begin() + count) !=
                buffer->begin() + count ||
        std::any_of(buffer->begin(), buffer->begin() + count,
                    [](std::int32_t threadId) { return threadId <= 0; })) {
        return false;
    }
    return std::binary_search(
            buffer->begin(), buffer->begin() + count,
            configuration_.currentThreadId);
}

bool ThreadQuiescenceCoordinator::finalInventoryContainsEveryRequested(
        std::size_t finalCount) const noexcept {
    for (std::size_t index = 0; index < requestedThreadCount_; ++index) {
        if (!std::binary_search(
                    finalInventory_.begin(), finalInventory_.begin() + finalCount,
                    requestedThreadIds_[index])) {
            return false;
        }
    }
    return true;
}

bool ThreadQuiescenceCoordinator::finalInventoryIsExact(
        std::size_t finalCount) const noexcept {
    if (finalCount != requestedThreadCount_ + 1) {
        return false;
    }
    std::size_t requestedIndex = 0;
    for (std::size_t index = 0; index < finalCount; ++index) {
        const std::int32_t threadId = finalInventory_[index];
        if (threadId == configuration_.currentThreadId) {
            continue;
        }
        if (requestedIndex >= requestedThreadCount_ ||
            requestedThreadIds_[requestedIndex] != threadId) {
            return false;
        }
        ++requestedIndex;
    }
    return requestedIndex == requestedThreadCount_;
}

ThreadQuiescenceResult ThreadQuiescenceCoordinator::prepare() {
    if (state_ != ThreadQuiescenceState::kEmpty) {
        return result(ThreadQuiescenceStatus::kInvalidState,
                      "thread quiescence can only be prepared once");
    }
    std::uintptr_t targetEnd = 0;
    if (configuration_.targetAddress == 0 ||
        (configuration_.targetAddress & 3u) != 0 ||
        !rangeEnd(configuration_.targetAddress, configuration_.targetSize, &targetEnd) ||
        configuration_.currentThreadId <= 0 || configuration_.epoch == 0 ||
        configuration_.maximumThreads == 0 ||
        configuration_.maximumThreads > kMaximumSupportedThreads ||
        configuration_.maximumStabilizationPasses == 0 ||
        configuration_.maximumStabilizationPasses >
                kMaximumSupportedStabilizationPasses ||
        configuration_.waitTimeoutMilliseconds == 0) {
        state_ = ThreadQuiescenceState::kFailed;
        return result(ThreadQuiescenceStatus::kInvalidConfiguration,
                      "target range, epoch, limits or current thread is invalid");
    }
    if (backend_.enumerateThreads == nullptr || backend_.requestThreadPark == nullptr ||
        backend_.waitForParkedThreads == nullptr ||
        backend_.resumeParkedThreads == nullptr) {
        state_ = ThreadQuiescenceState::kFailed;
        return result(ThreadQuiescenceStatus::kInvalidBackend,
                      "thread quiescence backend is incomplete");
    }

    inventory_.assign(configuration_.maximumThreads, 0);
    finalInventory_.assign(configuration_.maximumThreads, 0);
    requestedThreadIds_.assign(configuration_.maximumThreads, 0);
    parkedThreads_.assign(configuration_.maximumThreads, {});

    std::size_t count = 0;
    if (!readInventory(&inventory_, &count)) {
        state_ = ThreadQuiescenceState::kFailed;
        return result(
                count > configuration_.maximumThreads
                        ? ThreadQuiescenceStatus::kThreadCapacityExceeded
                        : ThreadQuiescenceStatus::kEnumerationFailed,
                count > configuration_.maximumThreads
                        ? "thread inventory exceeds the prepared capacity"
                        : "initial thread enumeration failed");
    }
    if (!validInventory(&inventory_, count)) {
        state_ = ThreadQuiescenceState::kFailed;
        return result(ThreadQuiescenceStatus::kThreadInventoryInvalid,
                      "initial thread inventory is empty, duplicated or missing the caller");
    }
    state_ = ThreadQuiescenceState::kPrepared;
    return result(ThreadQuiescenceStatus::kOk,
                  "thread quiescence buffers prepared without stopping a thread");
}

ThreadQuiescenceResult ThreadQuiescenceCoordinator::abortEnter(
        ThreadQuiescenceStatus status, const char* message) noexcept {
    if (parkAttempted_) {
        if (!backend_.resumeParkedThreads(backend_.context, configuration_.epoch)) {
            state_ = ThreadQuiescenceState::kFailed;
            return result(ThreadQuiescenceStatus::kResumeFailed,
                          "thread quiescence failed and parked threads could not be resumed");
        }
        parkAttempted_ = false;
    }
    state_ = ThreadQuiescenceState::kFailed;
    return result(status, message);
}

ThreadQuiescenceResult ThreadQuiescenceCoordinator::enter() noexcept {
    if (state_ != ThreadQuiescenceState::kPrepared) {
        return result(ThreadQuiescenceStatus::kInvalidState,
                      "thread quiescence is not prepared");
    }
    requestedThreadCount_ = 0;
    parkedThreadCount_ = 0;

    std::size_t inventoryCount = 0;
    if (!readInventory(&inventory_, &inventoryCount)) {
        return abortEnter(
                inventoryCount > configuration_.maximumThreads
                        ? ThreadQuiescenceStatus::kThreadCapacityExceeded
                        : ThreadQuiescenceStatus::kEnumerationFailed,
                inventoryCount > configuration_.maximumThreads
                        ? "thread inventory exceeds the prepared capacity"
                        : "thread enumeration failed before parking");
    }
    if (!validInventory(&inventory_, inventoryCount)) {
        return abortEnter(ThreadQuiescenceStatus::kThreadInventoryInvalid,
                          "thread inventory became invalid before parking");
    }

    for (std::size_t pass = 0;
         pass < configuration_.maximumStabilizationPasses; ++pass) {
        for (std::size_t index = 0; index < inventoryCount; ++index) {
            const std::int32_t threadId = inventory_[index];
            if (threadId == configuration_.currentThreadId ||
                std::binary_search(
                        requestedThreadIds_.begin(),
                        requestedThreadIds_.begin() + requestedThreadCount_, threadId)) {
                continue;
            }
            if (requestedThreadCount_ == requestedThreadIds_.size()) {
                return abortEnter(ThreadQuiescenceStatus::kThreadCapacityExceeded,
                                  "park request set exceeds the prepared capacity");
            }
            parkAttempted_ = true;
            if (!backend_.requestThreadPark(
                        backend_.context, threadId, configuration_.epoch)) {
                return abortEnter(ThreadQuiescenceStatus::kParkRequestFailed,
                                  "a peer thread park request failed or was uncertain");
            }
            requestedThreadIds_[requestedThreadCount_++] = threadId;
            std::sort(requestedThreadIds_.begin(),
                      requestedThreadIds_.begin() + requestedThreadCount_);
        }

        parkedThreadCount_ = 0;
        if (requestedThreadCount_ != 0 &&
            !backend_.waitForParkedThreads(
                    backend_.context,
                    configuration_.epoch,
                    requestedThreadIds_.data(),
                    requestedThreadCount_,
                    parkedThreads_.data(),
                    parkedThreads_.size(),
                    &parkedThreadCount_,
                    configuration_.waitTimeoutMilliseconds)) {
            return abortEnter(ThreadQuiescenceStatus::kParkWaitFailed,
                              "not every requested thread parked before the timeout");
        }
        if (parkedThreadCount_ != requestedThreadCount_) {
            return abortEnter(ThreadQuiescenceStatus::kParkedInventoryMismatch,
                              "parked snapshot count does not match the request set");
        }
        std::sort(parkedThreads_.begin(), parkedThreads_.begin() + parkedThreadCount_,
                [](const ParkedThreadSnapshot& left, const ParkedThreadSnapshot& right) {
                    return left.threadId < right.threadId;
                });
        std::uintptr_t targetEnd = 0;
        rangeEnd(configuration_.targetAddress, configuration_.targetSize, &targetEnd);
        for (std::size_t index = 0; index < parkedThreadCount_; ++index) {
            const ParkedThreadSnapshot& parked = parkedThreads_[index];
            if (parked.threadId != requestedThreadIds_[index] ||
                parked.epoch != configuration_.epoch) {
                return abortEnter(ThreadQuiescenceStatus::kParkedInventoryMismatch,
                                  "parked snapshots do not exactly match the request epoch");
            }
            if (parked.programCounter == 0 || (parked.programCounter & 3u) != 0) {
                ThreadQuiescenceResult failure = abortEnter(
                        ThreadQuiescenceStatus::kParkedProgramCounterInvalid,
                        "a parked thread reported an invalid ARM64 program counter");
                failure.unsafeThreadId = parked.threadId;
                failure.unsafeProgramCounter = parked.programCounter;
                return failure;
            }
            if (configuration_.targetAddress <= parked.programCounter &&
                parked.programCounter < targetEnd) {
                ThreadQuiescenceResult failure = abortEnter(
                        ThreadQuiescenceStatus::kThreadInTargetRange,
                        "a parked thread is executing inside the entry patch range");
                failure.unsafeThreadId = parked.threadId;
                failure.unsafeProgramCounter = parked.programCounter;
                return failure;
            }
        }

        std::size_t finalCount = 0;
        if (!readInventory(&finalInventory_, &finalCount)) {
            return abortEnter(
                    finalCount > configuration_.maximumThreads
                            ? ThreadQuiescenceStatus::kThreadCapacityExceeded
                            : ThreadQuiescenceStatus::kEnumerationFailed,
                    finalCount > configuration_.maximumThreads
                            ? "final thread inventory exceeds the prepared capacity"
                            : "thread enumeration failed while peers were parked");
        }
        if (!validInventory(&finalInventory_, finalCount)) {
            return abortEnter(ThreadQuiescenceStatus::kThreadInventoryInvalid,
                              "final thread inventory is invalid while peers are parked");
        }
        if (!finalInventoryContainsEveryRequested(finalCount)) {
            return abortEnter(ThreadQuiescenceStatus::kThreadInventoryChanged,
                              "a requested parked thread disappeared from the final inventory");
        }
        if (finalInventoryIsExact(finalCount)) {
            state_ = ThreadQuiescenceState::kExclusive;
            return result(ThreadQuiescenceStatus::kOk,
                          "all peer threads are parked outside the target range");
        }
        if (pass + 1 == configuration_.maximumStabilizationPasses) {
            return abortEnter(ThreadQuiescenceStatus::kThreadSetUnstable,
                              "thread set did not stabilize within the configured passes");
        }
        std::copy(finalInventory_.begin(), finalInventory_.begin() + finalCount,
                  inventory_.begin());
        inventoryCount = finalCount;
    }
    return abortEnter(ThreadQuiescenceStatus::kThreadSetUnstable,
                      "thread set did not stabilize");
}

ThreadQuiescenceResult ThreadQuiescenceCoordinator::leave() noexcept {
    if (state_ != ThreadQuiescenceState::kExclusive) {
        return result(ThreadQuiescenceStatus::kInvalidState,
                      "thread quiescence is not exclusive");
    }
    if (parkAttempted_ &&
        !backend_.resumeParkedThreads(backend_.context, configuration_.epoch)) {
        state_ = ThreadQuiescenceState::kFailed;
        return result(ThreadQuiescenceStatus::kResumeFailed,
                      "parked threads could not be resumed");
    }
    parkAttempted_ = false;
    state_ = ThreadQuiescenceState::kReleased;
    return result(ThreadQuiescenceStatus::kOk,
                  "all parked threads were released from the exclusive window");
}

const char* threadQuiescenceStateName(ThreadQuiescenceState state) noexcept {
    switch (state) {
        case ThreadQuiescenceState::kEmpty: return "empty";
        case ThreadQuiescenceState::kPrepared: return "prepared";
        case ThreadQuiescenceState::kExclusive: return "exclusive";
        case ThreadQuiescenceState::kReleased: return "released";
        case ThreadQuiescenceState::kFailed: return "failed";
    }
    return "unknown";
}

const char* threadQuiescenceStatusName(ThreadQuiescenceStatus status) noexcept {
    switch (status) {
        case ThreadQuiescenceStatus::kOk: return "ok";
        case ThreadQuiescenceStatus::kInvalidState: return "invalid_state";
        case ThreadQuiescenceStatus::kInvalidConfiguration:
            return "invalid_configuration";
        case ThreadQuiescenceStatus::kInvalidBackend: return "invalid_backend";
        case ThreadQuiescenceStatus::kEnumerationFailed: return "enumeration_failed";
        case ThreadQuiescenceStatus::kThreadCapacityExceeded:
            return "thread_capacity_exceeded";
        case ThreadQuiescenceStatus::kThreadInventoryInvalid:
            return "thread_inventory_invalid";
        case ThreadQuiescenceStatus::kParkRequestFailed: return "park_request_failed";
        case ThreadQuiescenceStatus::kParkWaitFailed: return "park_wait_failed";
        case ThreadQuiescenceStatus::kParkedInventoryMismatch:
            return "parked_inventory_mismatch";
        case ThreadQuiescenceStatus::kParkedProgramCounterInvalid:
            return "parked_program_counter_invalid";
        case ThreadQuiescenceStatus::kThreadInTargetRange:
            return "thread_in_target_range";
        case ThreadQuiescenceStatus::kThreadInventoryChanged:
            return "thread_inventory_changed";
        case ThreadQuiescenceStatus::kThreadSetUnstable: return "thread_set_unstable";
        case ThreadQuiescenceStatus::kResumeFailed: return "resume_failed";
    }
    return "unknown";
}

}  // namespace vcam::runtime
