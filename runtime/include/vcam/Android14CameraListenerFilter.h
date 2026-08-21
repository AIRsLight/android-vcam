#pragma once

#include <cstdint>

namespace android {
class Parcel;
}

namespace vcam::runtime {

enum class CameraListenerRequestRouteStatus {
    kWrapped = 0,
    kNoRegisteredWrapper,
    kMalformedRequest,
    kCopyFailed,
    kWriteFailed,
};

enum class CameraStatusReplyFilterStatus {
    kFiltered = 0,
    kUnchanged,
    kMalformedReply,
    kServiceError,
    kWriteFailed,
};

// Replaces the ICameraServiceListener Binder in an Android 14 addListener
// request. The wrapper drops callbacks whose logical camera ID is internal.
CameraListenerRequestRouteStatus wrapAndroid14CameraListenerRequest(
        const android::Parcel& input,
        android::Parcel* output) noexcept;

// Reuses the wrapper registered for the original listener Binder when routing
// removeListener. Returns kNoRegisteredWrapper without changing the input when
// no addListener request has been observed for that Binder.
CameraListenerRequestRouteStatus wrapAndroid14CameraListenerRemovalRequest(
        const android::Parcel& input,
        android::Parcel* output) noexcept;

// Removes internal camera records from a successful Android 14 addListener
// CameraStatus[] reply. The original reply is changed only after full decode
// and successful reconstruction.
CameraStatusReplyFilterStatus filterAndroid14CameraStatusReply(
        android::Parcel* reply) noexcept;

const char* cameraListenerRequestRouteStatusName(
        CameraListenerRequestRouteStatus status) noexcept;
const char* cameraStatusReplyFilterStatusName(
        CameraStatusReplyFilterStatus status) noexcept;

std::uint64_t android14CameraListenerWrappers() noexcept;
std::uint64_t android14CameraListenerCallbacksFiltered() noexcept;
std::uint64_t android14CameraStatusRecordsFiltered() noexcept;

}  // namespace vcam::runtime
