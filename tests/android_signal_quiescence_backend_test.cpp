#include "vcam/AndroidSignalQuiescenceBackend.h"

#include <cassert>
#include <csignal>
#include <cstdint>
#include <string>

#if defined(__ANDROID__) && defined(__aarch64__)
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

#if defined(__ANDROID__) && defined(__aarch64__)

__attribute__((noinline, aligned(16))) void patchTargetMarker() {
    asm volatile("" ::: "memory");
}

extern "C" void vcam_test_unsafe_target_loop(
        const std::uint32_t* stop, std::uint32_t* ready);

asm(
    ".text\n"
    ".p2align 4\n"
    ".global vcam_test_unsafe_target_loop\n"
    ".hidden vcam_test_unsafe_target_loop\n"
    ".type vcam_test_unsafe_target_loop, %function\n"
    "vcam_test_unsafe_target_loop:\n"
    ".inst 0xd503245f\n"
    "mov w2, #1\n"
    "str w2, [x1]\n"
    "1:\n"
    "ldar w1, [x0]\n"
    "cbnz w1, 2f\n"
    "b 1b\n"
    "2:\n"
    "ret\n"
    ".size vcam_test_unsafe_target_loop, .-vcam_test_unsafe_target_loop\n");

#endif

}  // namespace

int main() {
    const std::string status =
            "Name:\ttest\n"
            "SigPnd:\t0000000000000002\n"
            "ShdPnd:\t0000000000000004\n"
            "SigBlk:\t0000000000000008\n";
    vcam::runtime::ProcStatusSignalMasks masks;
    std::string error;
    assert(vcam::runtime::parseProcStatusSignalMasks(status, &masks, &error));
    assert(error.empty());
    assert(masks.pending == 2);
    assert(masks.sharedPending == 4);
    assert(masks.blocked == 8);
    assert(!vcam::runtime::parseProcStatusSignalMasks(
            "SigPnd:\t0\nShdPnd:\t0\n", &masks, &error));
    assert(!vcam::runtime::parseProcStatusSignalMasks(
            "SigPnd:\tnot-hex\nShdPnd:\t0\nSigBlk:\t0\n", &masks, &error));
    assert(vcam::runtime::inspectRealtimeSignalEligibility(SIGUSR1).status ==
           vcam::runtime::SignalEligibilityStatus::kInvalidSignal);
    const vcam::runtime::SignalEligibilityResult liveEligibility =
            vcam::runtime::selectEligibleRealtimeSignal();
    assert(liveEligibility ||
           liveEligibility.status ==
                   vcam::runtime::SignalEligibilityStatus::kNoEligibleRealtimeSignal);

#if defined(__ANDROID__) && defined(__aarch64__)
    constexpr std::size_t kWorkerCount = 4;
    std::atomic<bool> stop {false};
    std::vector<std::atomic<std::uint64_t>> counters(kWorkerCount);
    std::vector<std::thread> workers;
    workers.reserve(kWorkerCount);
    for (std::size_t index = 0; index < kWorkerCount; ++index) {
        counters[index].store(0, std::memory_order_relaxed);
        workers.emplace_back([&, index]() {
            while (!stop.load(std::memory_order_relaxed)) {
                counters[index].fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    const vcam::runtime::SignalEligibilityResult eligibility =
            vcam::runtime::selectEligibleRealtimeSignal();
    assert(eligibility);
    vcam::runtime::AndroidSignalQuiescenceBackend signalBackend;
    assert(signalBackend.prepare(eligibility.signalNumber, 64, 1000));

    vcam::runtime::ThreadQuiescenceConfiguration configuration;
    configuration.targetAddress =
            reinterpret_cast<std::uintptr_t>(&patchTargetMarker);
    configuration.targetSize = 16;
    configuration.currentThreadId =
            static_cast<std::int32_t>(syscall(SYS_gettid));
    configuration.epoch = 1;
    configuration.maximumThreads = 64;
    configuration.maximumStabilizationPasses = 4;
    configuration.waitTimeoutMilliseconds = 1000;
    vcam::runtime::ThreadQuiescenceCoordinator coordinator(
            configuration, signalBackend.backend());
    assert(coordinator.prepare());
    assert(coordinator.enter());

    std::vector<std::uint64_t> parkedCounters(kWorkerCount);
    for (std::size_t index = 0; index < kWorkerCount; ++index) {
        parkedCounters[index] = counters[index].load(std::memory_order_relaxed);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    for (std::size_t index = 0; index < kWorkerCount; ++index) {
        assert(counters[index].load(std::memory_order_relaxed) ==
               parkedCounters[index]);
    }
    assert(coordinator.leave());

    bool resumed = false;
    for (int attempt = 0; attempt < 100 && !resumed; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for (std::size_t index = 0; index < kWorkerCount; ++index) {
            resumed = resumed ||
                    counters[index].load(std::memory_order_relaxed) >
                            parkedCounters[index];
        }
    }
    assert(resumed);
    assert(signalBackend.shutdown());
    stop.store(true, std::memory_order_relaxed);
    for (std::thread& worker : workers) {
        worker.join();
    }

    alignas(4) std::uint32_t unsafeStop = 0;
    alignas(4) std::uint32_t unsafeReady = 0;
    std::thread unsafeWorker([&]() {
        vcam_test_unsafe_target_loop(&unsafeStop, &unsafeReady);
    });
    while (__atomic_load_n(&unsafeReady, __ATOMIC_ACQUIRE) == 0) {
        std::this_thread::yield();
    }
    const vcam::runtime::SignalEligibilityResult unsafeEligibility =
            vcam::runtime::selectEligibleRealtimeSignal();
    assert(unsafeEligibility);
    vcam::runtime::AndroidSignalQuiescenceBackend unsafeBackend;
    assert(unsafeBackend.prepare(unsafeEligibility.signalNumber, 64, 1000));
    configuration.targetAddress =
            reinterpret_cast<std::uintptr_t>(&vcam_test_unsafe_target_loop);
    configuration.targetSize = 24;
    configuration.epoch = 2;
    vcam::runtime::ThreadQuiescenceCoordinator unsafeCoordinator(
            configuration, unsafeBackend.backend());
    assert(unsafeCoordinator.prepare());
    const vcam::runtime::ThreadQuiescenceResult unsafe = unsafeCoordinator.enter();
    assert(unsafe.status ==
           vcam::runtime::ThreadQuiescenceStatus::kThreadInTargetRange);
    assert(unsafe.unsafeProgramCounter >= configuration.targetAddress);
    assert(unsafe.unsafeProgramCounter <
           configuration.targetAddress + configuration.targetSize);
    assert(unsafeBackend.shutdown());
    __atomic_store_n(&unsafeStop, std::uint32_t{1}, __ATOMIC_RELEASE);
    unsafeWorker.join();
#else
    vcam::runtime::AndroidSignalQuiescenceBackend signalBackend;
    assert(signalBackend.prepare(63, 64, 1000).status ==
           vcam::runtime::AndroidSignalBackendStatus::kUnsupportedPlatform);
#endif

    assert(vcam::runtime::signalEligibilityStatusName(
                   vcam::runtime::SignalEligibilityStatus::kReady) != nullptr);
    assert(vcam::runtime::androidSignalBackendStatusName(
                   vcam::runtime::AndroidSignalBackendStatus::kReady) != nullptr);
    return 0;
}
