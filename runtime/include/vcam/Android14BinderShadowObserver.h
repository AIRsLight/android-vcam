#pragma once

#include <atomic>
#include <cstdint>

#include "vcam/Android14ParcelObserver.h"
#include "vcam/BinderPassThroughBridge.h"

namespace vcam::runtime {

struct Android14ShadowObservationStats {
    std::uint64_t total = 0;
    std::uint64_t observed = 0;
    std::uint64_t ignored = 0;
    std::uint64_t rejected = 0;
    std::uint64_t unsupported = 0;
    std::uint64_t claimedPackage = 0;
    std::uint64_t uidOnly = 0;
    std::uint64_t identityUnavailable = 0;
};

// Thread-safe telemetry adapter for the pass-through bridge. It intentionally
// retains counters only and does not persist observed identity fields.
class Android14BinderShadowObserver final {
public:
    explicit Android14BinderShadowObserver(AbiRecipe recipe);

    void observe(std::uint32_t code, const void* dataParcel) noexcept;
    Android14ShadowObservationStats stats() const noexcept;

    static void bridgeCallback(
            std::uint32_t code, const void* dataParcel, void* context) noexcept;

private:
    const AbiRecipe recipe_;
    std::atomic<std::uint64_t> total_{0};
    std::atomic<std::uint64_t> observed_{0};
    std::atomic<std::uint64_t> ignored_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> unsupported_{0};
    std::atomic<std::uint64_t> claimedPackage_{0};
    std::atomic<std::uint64_t> uidOnly_{0};
    std::atomic<std::uint64_t> identityUnavailable_{0};
};

}  // namespace vcam::runtime
