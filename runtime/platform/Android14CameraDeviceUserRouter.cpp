#include "vcam/Android14CameraDeviceUserRouter.h"

#include <binder/Binder.h>
#include <binder/IBinder.h>
#include <binder/Parcel.h>
#include <binder/Status.h>
#include <utils/String16.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace vcam::runtime {
namespace {

constexpr char16_t kDeviceUserDescriptor[] =
        u"android.hardware.camera2.ICameraDeviceUser";
constexpr std::uint32_t kSubmitRequestCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 1;
constexpr std::uint32_t kSubmitRequestListCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 2;
constexpr std::int32_t kParcelablePresent = 1;
constexpr std::int32_t kMaxRequestCount = 256;
constexpr std::int32_t kMaxSurfaceCount = 64;
constexpr std::int32_t kMaxMetadataBlobSize = 64 * 1024 * 1024;
constexpr std::int32_t kMetadataAlignment = 8;
constexpr std::uint32_t kUseBufferQueue = 0x62717565;  // 'bque'

std::atomic<std::uint64_t> gDeviceUserWrappers {0};
std::atomic<std::uint64_t> gRequestBatchesRewritten {0};
std::atomic<std::uint64_t> gRequestsRewritten {0};
std::atomic<std::uint64_t> gRequestBatchesSkipped {0};

class ParcelPositionGuard final {
public:
    explicit ParcelPositionGuard(const android::Parcel& parcel)
        : parcel_(parcel), position_(parcel.dataPosition()) {}
    ~ParcelPositionGuard() { parcel_.setDataPosition(position_); }

private:
    const android::Parcel& parcel_;
    const std::size_t position_;
};

bool readAsciiString16(
        const android::Parcel& parcel,
        std::string* value,
        std::size_t* start,
        std::size_t* end) {
    *start = parcel.dataPosition();
    std::size_t length = 0;
    const char16_t* text = parcel.readString16Inplace(&length);
    *end = parcel.dataPosition();
    if (text == nullptr || length == 0 || length > 255) return false;
    value->clear();
    value->reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        if (text[index] > 0x7f) return false;
        value->push_back(static_cast<char>(text[index]));
    }
    return true;
}

bool readAndCheckInterfaceHeader(const android::Parcel& parcel) {
    std::int32_t ignored = 0;
    std::int32_t header = 0;
    if (parcel.readInt32(&ignored) != android::OK ||
        parcel.readInt32(&ignored) != android::OK ||
        parcel.readInt32(&header) != android::OK) {
        return false;
    }
    constexpr std::int32_t expectedHeader =
            ('S' << 24) | ('Y' << 16) | ('S' << 8) | 'T';
    if (header != expectedHeader) return false;

    std::size_t length = 0;
    const char16_t* descriptor = parcel.readString16Inplace(&length);
    constexpr std::size_t expectedLength =
            sizeof(kDeviceUserDescriptor) /
                    sizeof(kDeviceUserDescriptor[0]) -
            1;
    if (descriptor == nullptr || length != expectedLength) return false;
    for (std::size_t index = 0; index < expectedLength; ++index) {
        if (descriptor[index] != kDeviceUserDescriptor[index]) return false;
    }
    return true;
}

bool skipMetadata(const android::Parcel& parcel) {
    std::int32_t blobSize = -1;
    if (parcel.readInt32(&blobSize) != android::OK || blobSize < 0 ||
        blobSize > kMaxMetadataBlobSize) {
        return false;
    }
    if (blobSize == 0) return true;
    if (blobSize <= kMetadataAlignment) return false;
    android::Parcel::ReadableBlob blob;
    if (parcel.readBlob(static_cast<std::size_t>(blobSize), &blob) != android::OK) {
        return false;
    }
    blob.release();
    std::int32_t ignoredOffset = -1;
    return parcel.readInt32(&ignoredOffset) == android::OK &&
           ignoredOffset >= 0 && ignoredOffset < kMetadataAlignment;
}

bool skipSurface(const android::Parcel& parcel) {
    std::optional<android::String16> name;
    std::int32_t singleBuffered = 0;
    std::uint32_t producerMagic = 0;
    android::sp<android::IBinder> ignored;
    return parcel.readString16(&name) == android::OK &&
           parcel.readInt32(&singleBuffered) == android::OK &&
           parcel.readUint32(&producerMagic) == android::OK &&
           producerMagic == kUseBufferQueue &&
           parcel.readNullableStrongBinder(&ignored) == android::OK &&
           parcel.readNullableStrongBinder(&ignored) == android::OK;
}

struct CameraIdOffset {
    std::size_t start = 0;
    std::size_t end = 0;
};

bool observeCaptureRequest(
        const android::Parcel& parcel,
        const std::string& publicCameraId,
        CameraIdOffset* offset) {
    std::int32_t present = 0;
    std::int32_t settingsCount = 0;
    if (parcel.readInt32(&present) != android::OK ||
        present != kParcelablePresent ||
        parcel.readInt32(&settingsCount) != android::OK || settingsCount != 1) {
        return false;
    }
    std::string logicalCameraId;
    if (!readAsciiString16(
                parcel, &logicalCameraId, &offset->start, &offset->end) ||
        logicalCameraId != publicCameraId || !skipMetadata(parcel)) {
        return false;
    }

    std::int32_t ignored = 0;
    std::int32_t surfaceCount = 0;
    if (parcel.readInt32(&ignored) != android::OK ||
        parcel.readInt32(&surfaceCount) != android::OK || surfaceCount < 0 ||
        surfaceCount > kMaxSurfaceCount) {
        return false;
    }
    for (std::int32_t index = 0; index < surfaceCount; ++index) {
        std::optional<std::string> className;
        if (parcel.readUtf8FromUtf16(&className) != android::OK ||
            !className.has_value() || !skipSurface(parcel)) {
            return false;
        }
    }

    std::int32_t streamSurfaceCount = 0;
    if (parcel.readInt32(&streamSurfaceCount) != android::OK ||
        streamSurfaceCount < 0 || streamSurfaceCount > kMaxSurfaceCount) {
        return false;
    }
    for (std::int32_t index = 0; index < streamSurfaceCount; ++index) {
        if (parcel.readInt32(&ignored) != android::OK ||
            parcel.readInt32(&ignored) != android::OK) {
            return false;
        }
    }

    std::int32_t hasUserTag = 0;
    if (parcel.readInt32(&hasUserTag) != android::OK) return false;
    if (hasUserTag != 0) {
        std::optional<android::String16> ignoredTag;
        if (parcel.readString16(&ignoredTag) != android::OK ||
            !ignoredTag.has_value()) {
            return false;
        }
    }
    return true;
}

bool observeRequestOffsets(
        std::uint32_t code,
        const android::Parcel& parcel,
        const std::string& publicCameraId,
        std::vector<CameraIdOffset>* offsets) {
    ParcelPositionGuard positionGuard(parcel);
    parcel.setDataPosition(0);
    if (!readAndCheckInterfaceHeader(parcel)) {
        return false;
    }

    std::int32_t requestCount = 1;
    if (code == kSubmitRequestListCode) {
        if (parcel.readInt32(&requestCount) != android::OK || requestCount <= 0 ||
            requestCount > kMaxRequestCount) {
            return false;
        }
    } else if (code != kSubmitRequestCode) {
        return false;
    }
    offsets->clear();
    offsets->resize(static_cast<std::size_t>(requestCount));
    for (CameraIdOffset& offset : *offsets) {
        if (!observeCaptureRequest(parcel, publicCameraId, &offset)) return false;
    }
    bool ignoredStreaming = false;
    return parcel.readBool(&ignoredStreaming) == android::OK &&
           parcel.dataPosition() == parcel.dataSize();
}

bool rewriteRequestIds(
        std::uint32_t code,
        const android::Parcel& input,
        const std::string& publicCameraId,
        const std::string& deviceCameraId,
        android::Parcel* output,
        std::size_t* requestCount) {
    if (output == nullptr || publicCameraId.size() != deviceCameraId.size()) {
        return false;
    }
    std::vector<CameraIdOffset> offsets;
    if (!observeRequestOffsets(code, input, publicCameraId, &offsets)) return false;
    output->setDataPosition(0);
    if (output->setDataSize(0) != android::OK ||
        output->appendFrom(&input, 0, input.dataSize()) != android::OK) {
        return false;
    }
    for (const CameraIdOffset& offset : offsets) {
        output->setDataPosition(offset.start);
        if (output->writeString16(
                    android::String16(deviceCameraId.c_str())) != android::OK ||
            output->dataPosition() != offset.end) {
            return false;
        }
    }
    if (output->dataSize() != input.dataSize()) return false;
    output->setDataPosition(input.dataPosition());
    *requestCount = offsets.size();
    return true;
}

class RoutedCameraDeviceUser final : public android::BBinder {
public:
    RoutedCameraDeviceUser(
            android::sp<android::IBinder> target,
            std::string publicCameraId,
            std::string deviceCameraId)
        : target_(std::move(target)),
          publicCameraId_(std::move(publicCameraId)),
          deviceCameraId_(std::move(deviceCameraId)) {}

    const android::String16& getInterfaceDescriptor() const override {
        static const android::String16 descriptor(kDeviceUserDescriptor);
        return descriptor;
    }

protected:
    android::status_t onTransact(
            std::uint32_t code,
            const android::Parcel& data,
            android::Parcel* reply,
            std::uint32_t flags) override {
        if (code == kSubmitRequestCode || code == kSubmitRequestListCode) {
            android::Parcel rewritten;
            std::size_t requestCount = 0;
            if (rewriteRequestIds(
                        code,
                        data,
                        publicCameraId_,
                        deviceCameraId_,
                        &rewritten,
                        &requestCount)) {
                gRequestBatchesRewritten.fetch_add(1, std::memory_order_relaxed);
                gRequestsRewritten.fetch_add(
                        requestCount, std::memory_order_relaxed);
                return target_->transact(code, rewritten, reply, flags);
            }
            gRequestBatchesSkipped.fetch_add(1, std::memory_order_relaxed);
        }
        return target_->transact(code, data, reply, flags);
    }

private:
    const android::sp<android::IBinder> target_;
    const std::string publicCameraId_;
    const std::string deviceCameraId_;
};

}  // namespace

CameraDeviceUserReplyRouteStatus wrapAndroid14CameraDeviceUserReply(
        android::Parcel* reply,
        const std::string& publicCameraId,
        const std::string& deviceCameraId) noexcept {
    if (reply == nullptr || publicCameraId.empty() || deviceCameraId.empty() ||
        publicCameraId == deviceCameraId ||
        publicCameraId.size() != deviceCameraId.size()) {
        return CameraDeviceUserReplyRouteStatus::kNotEligible;
    }

    const std::size_t originalPosition = reply->dataPosition();
    reply->setDataPosition(0);
    android::binder::Status serviceStatus;
    if (serviceStatus.readFromParcel(*reply) != android::OK) {
        reply->setDataPosition(originalPosition);
        return CameraDeviceUserReplyRouteStatus::kMalformedReply;
    }
    if (!serviceStatus.isOk()) {
        reply->setDataPosition(originalPosition);
        return CameraDeviceUserReplyRouteStatus::kServiceError;
    }

    android::sp<android::IBinder> deviceBinder;
    if (reply->readStrongBinder(&deviceBinder) != android::OK ||
        deviceBinder == nullptr || reply->dataPosition() != reply->dataSize()) {
        reply->setDataPosition(originalPosition);
        return CameraDeviceUserReplyRouteStatus::kMalformedReply;
    }
    const android::sp<RoutedCameraDeviceUser> wrapper =
            android::sp<RoutedCameraDeviceUser>::make(
                    std::move(deviceBinder), publicCameraId, deviceCameraId);
    android::Parcel replacement;
    if (serviceStatus.writeToParcel(&replacement) != android::OK ||
        replacement.writeStrongBinder(wrapper) != android::OK) {
        reply->setDataPosition(originalPosition);
        return CameraDeviceUserReplyRouteStatus::kWriteFailed;
    }
    reply->freeData();
    if (reply->appendFrom(&replacement, 0, replacement.dataSize()) != android::OK) {
        return CameraDeviceUserReplyRouteStatus::kWriteFailed;
    }
    reply->setDataPosition(0);
    gDeviceUserWrappers.fetch_add(1, std::memory_order_relaxed);
    return CameraDeviceUserReplyRouteStatus::kWrapped;
}

const char* cameraDeviceUserReplyRouteStatusName(
        CameraDeviceUserReplyRouteStatus status) noexcept {
    switch (status) {
        case CameraDeviceUserReplyRouteStatus::kWrapped: return "wrapped";
        case CameraDeviceUserReplyRouteStatus::kNotEligible: return "not_eligible";
        case CameraDeviceUserReplyRouteStatus::kMalformedReply: return "malformed_reply";
        case CameraDeviceUserReplyRouteStatus::kServiceError: return "service_error";
        case CameraDeviceUserReplyRouteStatus::kWriteFailed: return "write_failed";
    }
    return "unknown";
}

std::uint64_t android14CameraDeviceUserWrappers() noexcept {
    return gDeviceUserWrappers.load(std::memory_order_relaxed);
}
std::uint64_t android14CameraRequestBatchesRewritten() noexcept {
    return gRequestBatchesRewritten.load(std::memory_order_relaxed);
}
std::uint64_t android14CameraRequestsRewritten() noexcept {
    return gRequestsRewritten.load(std::memory_order_relaxed);
}
std::uint64_t android14CameraRequestBatchesSkipped() noexcept {
    return gRequestBatchesSkipped.load(std::memory_order_relaxed);
}

}  // namespace vcam::runtime
