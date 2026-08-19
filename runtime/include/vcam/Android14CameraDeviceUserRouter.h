#pragma once

#include <cstdint>
#include <string>

namespace android {
class Parcel;
}

namespace vcam::runtime {

enum class CameraDeviceUserReplyRouteStatus {
    kWrapped = 0,
    kNotEligible,
    kMalformedReply,
    kServiceError,
    kWriteFailed,
};

// Replaces a successful Android 14 connectDevice reply's ICameraDeviceUser
// with a local delegator. The delegator keeps the public camera ID seen by the
// client consistent with the actual device ID in CaptureRequest settings.
CameraDeviceUserReplyRouteStatus wrapAndroid14CameraDeviceUserReply(
        android::Parcel* reply,
        const std::string& publicCameraId,
        const std::string& deviceCameraId) noexcept;

const char* cameraDeviceUserReplyRouteStatusName(
        CameraDeviceUserReplyRouteStatus status) noexcept;

std::uint64_t android14CameraDeviceUserWrappers() noexcept;
std::uint64_t android14CameraRequestBatchesRewritten() noexcept;
std::uint64_t android14CameraRequestsRewritten() noexcept;
std::uint64_t android14CameraRequestBatchesSkipped() noexcept;

}  // namespace vcam::runtime
