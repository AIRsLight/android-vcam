#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "vcam/BinderTransactionClassifier.h"

namespace vcam::runtime {

enum class ParcelObservationStatus {
    kObserved = 0,
    kNotRoutedTransaction,
    kNullParcel,
    kMalformedHeader,
    kWrongInterface,
    kMalformedPayload,
    kUnsupportedPayload,
};

struct ParcelObservation {
    ParcelObservationStatus status = ParcelObservationStatus::kNotRoutedTransaction;
    BinderTransactionClass transaction;
    std::int32_t callingUid = -1;
    std::int32_t callingPid = -1;
    std::string cameraId;
    std::string packageName;
    std::size_t initialDataPosition = 0;
    std::size_t finalDataPosition = 0;
    std::size_t cameraIdStart = 0;
    std::size_t cameraIdEnd = 0;

    explicit operator bool() const { return status == ParcelObservationStatus::kObserved; }
};

// Android platform implementation. dataParcel must point to android::Parcel.
// The function restores dataPosition() on every return path.
ParcelObservation observeAndroid14CameraServiceParcel(
        const AbiRecipe& recipe,
        std::uint32_t code,
        const void* dataParcel);

const char* parcelObservationStatusName(ParcelObservationStatus status);

}  // namespace vcam::runtime
