#pragma once

#include <string>

#include "vcam/Android14ParcelObserver.h"

namespace android {
class Parcel;
}

namespace vcam::runtime {

enum class CameraIdRewriteStatus {
    kRewritten = 0,
    kNotEligible,
    kInvalidOffsets,
    kInvalidReplacement,
    kEncodedSizeMismatch,
    kCopyFailed,
    kWriteFailed,
};

// Builds a separate Parcel and overwrites only the already-decoded camera ID.
// String replacements must have the same UTF-16 length. The input Parcel and
// its cursor are never modified, and binder objects are copied by appendFrom().
CameraIdRewriteStatus rewriteAndroid14CameraIdSameWidth(
        const ParcelObservation& observation,
        const std::string& replacementCameraId,
        const android::Parcel& input,
        android::Parcel* output) noexcept;

const char* cameraIdRewriteStatusName(CameraIdRewriteStatus status) noexcept;

}  // namespace vcam::runtime
