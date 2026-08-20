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
    kCopyFailed,
    kWriteFailed,
};

// Builds a separate Parcel around the already-decoded camera ID. String IDs
// may change encoded size; prefix/suffix segments and their binder objects are
// copied with appendFrom(). The input Parcel and its cursor are never modified.
CameraIdRewriteStatus rewriteAndroid14CameraId(
        const ParcelObservation& observation,
        const std::string& replacementCameraId,
        const android::Parcel& input,
        android::Parcel* output) noexcept;

const char* cameraIdRewriteStatusName(CameraIdRewriteStatus status) noexcept;

}  // namespace vcam::runtime
