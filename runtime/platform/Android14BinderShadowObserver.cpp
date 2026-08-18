#include "vcam/Android14BinderShadowObserver.h"

#include <utility>

namespace vcam::runtime {

Android14BinderShadowObserver::Android14BinderShadowObserver(AbiRecipe recipe)
    : recipe_(std::move(recipe)) {}

void Android14BinderShadowObserver::observe(
        std::uint32_t code, const void* dataParcel) noexcept {
    total_.fetch_add(1, std::memory_order_relaxed);
    const ParcelObservation observation =
            observeAndroid14CameraServiceParcel(recipe_, code, dataParcel);
    switch (observation.status) {
        case ParcelObservationStatus::kObserved:
            observed_.fetch_add(1, std::memory_order_relaxed);
            break;
        case ParcelObservationStatus::kNotRoutedTransaction:
            ignored_.fetch_add(1, std::memory_order_relaxed);
            break;
        case ParcelObservationStatus::kUnsupportedPayload:
            unsupported_.fetch_add(1, std::memory_order_relaxed);
            break;
        case ParcelObservationStatus::kNullParcel:
        case ParcelObservationStatus::kMalformedHeader:
        case ParcelObservationStatus::kWrongInterface:
        case ParcelObservationStatus::kMalformedPayload:
            rejected_.fetch_add(1, std::memory_order_relaxed);
            break;
    }
}

Android14ShadowObservationStats Android14BinderShadowObserver::stats() const noexcept {
    Android14ShadowObservationStats result;
    result.total = total_.load(std::memory_order_relaxed);
    result.observed = observed_.load(std::memory_order_relaxed);
    result.ignored = ignored_.load(std::memory_order_relaxed);
    result.rejected = rejected_.load(std::memory_order_relaxed);
    result.unsupported = unsupported_.load(std::memory_order_relaxed);
    return result;
}

void Android14BinderShadowObserver::bridgeCallback(
        std::uint32_t code, const void* dataParcel, void* context) noexcept {
    if (context != nullptr) {
        static_cast<Android14BinderShadowObserver*>(context)->observe(code, dataParcel);
    }
}

}  // namespace vcam::runtime
