#include "vcam/Android14CameraIdRewriter.h"

#include <binder/Parcel.h>
#include <utils/String16.h>

#include <cstdint>
#include <limits>

namespace vcam::runtime {
namespace {

bool parseNonNegativeInteger(
        const std::string& value, std::int32_t* parsed) noexcept {
    if (value.empty()) return false;
    std::uint64_t result = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') return false;
        result = result * 10 + static_cast<unsigned int>(character - '0');
        if (result > static_cast<std::uint64_t>(
                             std::numeric_limits<std::int32_t>::max())) {
            return false;
        }
    }
    *parsed = static_cast<std::int32_t>(result);
    return true;
}

bool isAscii(const std::string& value) noexcept {
    if (value.empty()) return false;
    for (const unsigned char character : value) {
        if (character > 0x7f) return false;
    }
    return true;
}

}  // namespace

CameraIdRewriteStatus rewriteAndroid14CameraIdSameWidth(
        const ParcelObservation& observation,
        const std::string& replacementCameraId,
        const android::Parcel& input,
        android::Parcel* output) noexcept {
    if (output == nullptr || output == &input ||
        observation.status != ParcelObservationStatus::kObserved ||
        !observation.transaction.cameraScoped || observation.cameraId.empty()) {
        return CameraIdRewriteStatus::kNotEligible;
    }
    if (observation.cameraIdStart >= observation.cameraIdEnd ||
        observation.cameraIdEnd > input.dataSize()) {
        return CameraIdRewriteStatus::kInvalidOffsets;
    }
    if (!isAscii(replacementCameraId)) {
        return CameraIdRewriteStatus::kInvalidReplacement;
    }

    const BinderPayloadShape shape = observation.transaction.payloadShape;
    if (shape == BinderPayloadShape::kConnectDevice ||
        shape == BinderPayloadShape::kStringCameraId) {
        if (replacementCameraId.size() != observation.cameraId.size()) {
            return CameraIdRewriteStatus::kEncodedSizeMismatch;
        }
    } else if (shape != BinderPayloadShape::kConnectApi1 &&
               shape != BinderPayloadShape::kIntegerCameraId) {
        return CameraIdRewriteStatus::kNotEligible;
    }

    output->setDataPosition(0);
    if (output->setDataSize(0) != android::OK ||
        output->appendFrom(&input, 0, input.dataSize()) != android::OK) {
        return CameraIdRewriteStatus::kCopyFailed;
    }
    output->setDataPosition(observation.cameraIdStart);
    android::status_t writeStatus = android::BAD_VALUE;
    if (shape == BinderPayloadShape::kConnectDevice ||
        shape == BinderPayloadShape::kStringCameraId) {
        writeStatus = output->writeString16(
                android::String16(replacementCameraId.c_str()));
    } else {
        std::int32_t integerCameraId = -1;
        if (!parseNonNegativeInteger(replacementCameraId, &integerCameraId)) {
            return CameraIdRewriteStatus::kInvalidReplacement;
        }
        writeStatus = output->writeInt32(integerCameraId);
    }
    if (writeStatus != android::OK ||
        output->dataPosition() != observation.cameraIdEnd ||
        output->dataSize() != input.dataSize()) {
        return CameraIdRewriteStatus::kWriteFailed;
    }
    output->setDataPosition(observation.initialDataPosition);
    return CameraIdRewriteStatus::kRewritten;
}

const char* cameraIdRewriteStatusName(CameraIdRewriteStatus status) noexcept {
    switch (status) {
        case CameraIdRewriteStatus::kRewritten: return "rewritten";
        case CameraIdRewriteStatus::kNotEligible: return "not_eligible";
        case CameraIdRewriteStatus::kInvalidOffsets: return "invalid_offsets";
        case CameraIdRewriteStatus::kInvalidReplacement: return "invalid_replacement";
        case CameraIdRewriteStatus::kEncodedSizeMismatch: return "encoded_size_mismatch";
        case CameraIdRewriteStatus::kCopyFailed: return "copy_failed";
        case CameraIdRewriteStatus::kWriteFailed: return "write_failed";
    }
    return "unknown";
}

}  // namespace vcam::runtime
