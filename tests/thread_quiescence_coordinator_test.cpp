#include "vcam/ThreadQuiescenceCoordinator.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uintptr_t kTargetAddress = 0x1000;
constexpr std::int32_t kCurrentThreadId = 10;
constexpr std::uint32_t kEpoch = 7;

struct FakeThreads {
    std::vector<std::vector<std::int32_t>> inventories;
    std::size_t inventoryIndex = 0;
    std::vector<std::int32_t> requested;
    std::vector<std::string> events;
    std::int32_t failRequestThreadId = -1;
    std::int32_t unsafeThreadId = -1;
    std::uintptr_t unsafeProgramCounter = 0;
    bool failWait = false;
    bool mismatchEpoch = false;
    bool allowResume = true;
    std::size_t resumeCalls = 0;

    FakeThreads() {
        requested.reserve(16);
        events.reserve(64);
    }
};

bool enumerateThreads(
        void* context,
        std::int32_t* output,
        std::size_t capacity,
        std::size_t* count) noexcept {
    auto& threads = *static_cast<FakeThreads*>(context);
    threads.events.emplace_back("enumerate");
    if (threads.inventories.empty()) {
        return false;
    }
    const auto& inventory = threads.inventories[std::min(
            threads.inventoryIndex, threads.inventories.size() - 1)];
    ++threads.inventoryIndex;
    *count = inventory.size();
    if (inventory.size() > capacity) {
        return false;
    }
    std::copy(inventory.begin(), inventory.end(), output);
    return true;
}

bool requestThreadPark(
        void* context, std::int32_t threadId, std::uint32_t epoch) noexcept {
    auto& threads = *static_cast<FakeThreads*>(context);
    threads.events.emplace_back("request");
    assert(epoch == kEpoch);
    if (threadId == threads.failRequestThreadId) {
        return false;
    }
    if (std::find(threads.requested.begin(), threads.requested.end(), threadId) ==
        threads.requested.end()) {
        threads.requested.push_back(threadId);
    }
    return true;
}

bool waitForParkedThreads(
        void* context,
        std::uint32_t epoch,
        const std::int32_t* requestedThreadIds,
        std::size_t requestedThreadCount,
        vcam::runtime::ParkedThreadSnapshot* output,
        std::size_t capacity,
        std::size_t* parkedThreadCount,
        std::uint32_t timeoutMilliseconds) noexcept {
    auto& threads = *static_cast<FakeThreads*>(context);
    threads.events.emplace_back("wait");
    assert(epoch == kEpoch);
    assert(timeoutMilliseconds == 100);
    if (threads.failWait || requestedThreadCount > capacity) {
        return false;
    }
    *parkedThreadCount = requestedThreadCount;
    for (std::size_t index = 0; index < requestedThreadCount; ++index) {
        const std::int32_t threadId = requestedThreadIds[index];
        output[index].threadId = threadId;
        output[index].epoch = threads.mismatchEpoch ? epoch + 1 : epoch;
        output[index].programCounter =
                threadId == threads.unsafeThreadId
                        ? threads.unsafeProgramCounter
                        : 0x5000 + static_cast<std::uintptr_t>(threadId) * 4;
    }
    return true;
}

bool resumeParkedThreads(void* context, std::uint32_t epoch) noexcept {
    auto& threads = *static_cast<FakeThreads*>(context);
    threads.events.emplace_back("resume");
    assert(epoch == kEpoch);
    ++threads.resumeCalls;
    return threads.allowResume;
}

vcam::runtime::ThreadQuiescenceBackend backend(FakeThreads* threads) {
    return {
        threads,
        &enumerateThreads,
        &requestThreadPark,
        &waitForParkedThreads,
        &resumeParkedThreads,
    };
}

vcam::runtime::ThreadQuiescenceConfiguration configuration() {
    return {
        kTargetAddress,
        16,
        kCurrentThreadId,
        kEpoch,
        8,
        3,
        100,
    };
}

}  // namespace

int main() {
    {
        FakeThreads threads;
        threads.inventories = {{10, 11, 12}};
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                configuration(), backend(&threads));
        assert(coordinator.prepare());
        assert(coordinator.state() ==
               vcam::runtime::ThreadQuiescenceState::kPrepared);
        const auto entered = coordinator.enter();
        assert(entered);
        assert(entered.parkedThreadCount == 2);
        assert(coordinator.state() ==
               vcam::runtime::ThreadQuiescenceState::kExclusive);
        assert(coordinator.leave());
        assert(coordinator.state() ==
               vcam::runtime::ThreadQuiescenceState::kReleased);
        assert(threads.resumeCalls == 1);
        assert((threads.events == std::vector<std::string>{
                "enumerate", "enumerate", "request", "request", "wait",
                "enumerate", "resume"}));
    }

    {
        FakeThreads threads;
        threads.inventories = {
            {10, 11},
            {10, 11},
            {10, 11, 12},
            {10, 11, 12},
        };
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                configuration(), backend(&threads));
        assert(coordinator.prepare());
        const auto entered = coordinator.enter();
        assert(entered);
        assert(entered.parkedThreadCount == 2);
        assert(threads.requested == std::vector<std::int32_t>({11, 12}));
        assert(coordinator.leave());
    }

    {
        FakeThreads threads;
        threads.inventories = {{10, 11}};
        threads.unsafeThreadId = 11;
        threads.unsafeProgramCounter = kTargetAddress + 4;
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                configuration(), backend(&threads));
        assert(coordinator.prepare());
        const auto entered = coordinator.enter();
        assert(entered.status ==
               vcam::runtime::ThreadQuiescenceStatus::kThreadInTargetRange);
        assert(entered.unsafeThreadId == 11);
        assert(entered.unsafeProgramCounter == kTargetAddress + 4);
        assert(threads.resumeCalls == 1);
    }

    {
        FakeThreads threads;
        threads.inventories = {{10, 11}};
        threads.unsafeThreadId = 11;
        threads.unsafeProgramCounter = 0;
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                configuration(), backend(&threads));
        assert(coordinator.prepare());
        assert(coordinator.enter().status ==
               vcam::runtime::ThreadQuiescenceStatus::kParkedProgramCounterInvalid);
        assert(threads.resumeCalls == 1);
    }

    {
        FakeThreads threads;
        threads.inventories = {{10, 11, 12}};
        threads.failRequestThreadId = 12;
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                configuration(), backend(&threads));
        assert(coordinator.prepare());
        assert(coordinator.enter().status ==
               vcam::runtime::ThreadQuiescenceStatus::kParkRequestFailed);
        assert(threads.resumeCalls == 1);
    }

    {
        FakeThreads threads;
        threads.inventories = {{10, 11}};
        threads.failWait = true;
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                configuration(), backend(&threads));
        assert(coordinator.prepare());
        assert(coordinator.enter().status ==
               vcam::runtime::ThreadQuiescenceStatus::kParkWaitFailed);
        assert(threads.resumeCalls == 1);
    }

    {
        FakeThreads threads;
        threads.inventories = {{10, 11}};
        threads.mismatchEpoch = true;
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                configuration(), backend(&threads));
        assert(coordinator.prepare());
        assert(coordinator.enter().status ==
               vcam::runtime::ThreadQuiescenceStatus::kParkedInventoryMismatch);
        assert(threads.resumeCalls == 1);
    }

    {
        FakeThreads threads;
        threads.inventories = {
            {10, 11},
            {10, 11},
            {10},
        };
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                configuration(), backend(&threads));
        assert(coordinator.prepare());
        assert(coordinator.enter().status ==
               vcam::runtime::ThreadQuiescenceStatus::kThreadInventoryChanged);
        assert(threads.resumeCalls == 1);
    }

    {
        FakeThreads threads;
        threads.inventories = {
            {10, 11},
            {10, 11},
            {10, 11, 12},
            {10, 11, 12, 13},
        };
        auto config = configuration();
        config.maximumStabilizationPasses = 2;
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                config, backend(&threads));
        assert(coordinator.prepare());
        assert(coordinator.enter().status ==
               vcam::runtime::ThreadQuiescenceStatus::kThreadSetUnstable);
        assert(threads.resumeCalls == 1);
    }

    {
        FakeThreads threads;
        threads.inventories = {{10, 11, 12}};
        auto config = configuration();
        config.maximumThreads = 2;
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                config, backend(&threads));
        assert(coordinator.prepare().status ==
               vcam::runtime::ThreadQuiescenceStatus::kThreadCapacityExceeded);
    }

    {
        FakeThreads threads;
        threads.inventories = {{10, 11}};
        threads.allowResume = false;
        vcam::runtime::ThreadQuiescenceCoordinator coordinator(
                configuration(), backend(&threads));
        assert(coordinator.prepare());
        assert(coordinator.enter());
        assert(coordinator.leave().status ==
               vcam::runtime::ThreadQuiescenceStatus::kResumeFailed);
        assert(coordinator.state() == vcam::runtime::ThreadQuiescenceState::kFailed);
    }

    assert(vcam::runtime::threadQuiescenceStateName(
                   vcam::runtime::ThreadQuiescenceState::kExclusive) != nullptr);
    assert(vcam::runtime::threadQuiescenceStatusName(
                   vcam::runtime::ThreadQuiescenceStatus::kOk) != nullptr);
    return 0;
}
