#include "vcam/Android14CameraListenerFilter.h"

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

constexpr char16_t kCameraServiceDescriptor[] =
        u"android.hardware.ICameraService";
constexpr char16_t kCameraServiceListenerDescriptor[] =
        u"android.hardware.ICameraServiceListener";
constexpr std::uint32_t kOnStatusChangedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 0;
constexpr std::uint32_t kOnPhysicalCameraStatusChangedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 1;
constexpr std::uint32_t kOnTorchStatusChangedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 2;
constexpr std::uint32_t kOnTorchStrengthLevelChangedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 3;
constexpr std::uint32_t kOnCameraAccessPrioritiesChangedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 4;
constexpr std::uint32_t kOnCameraOpenedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 5;
constexpr std::uint32_t kOnCameraClosedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 6;
constexpr std::int32_t kParcelablePresent = 1;
constexpr std::int32_t kMaxCameraStatusCount = 1024;
constexpr std::size_t kMaxCameraIdLength = 255;

std::atomic<std::uint64_t> gListenerWrappers {0};
std::atomic<std::uint64_t> gListenerCallbacksFiltered {0};
std::atomic<std::uint64_t> gStatusRecordsFiltered {0};

class ParcelPositionGuard final {
public:
    explicit ParcelPositionGuard(const android::Parcel& parcel)
        : parcel_(parcel), position_(parcel.dataPosition()) {}
    ~ParcelPositionGuard() { parcel_.setDataPosition(position_); }
    std::size_t position() const { return position_; }

private:
    const android::Parcel& parcel_;
    const std::size_t position_;
};

bool isInternalId(const android::String16& value) {
    static const android::String16 backId("1000");
    static const android::String16 frontId("1001");
    return value == backId || value == frontId;
}

bool readRequiredString16(
        const android::Parcel& parcel,
        android::String16* value) {
    std::optional<android::String16> decoded;
    if (parcel.readString16(&decoded) != android::OK ||
        !decoded.has_value() || decoded->size() > kMaxCameraIdLength) {
        return false;
    }
    *value = std::move(*decoded);
    return true;
}

bool callbackReferencesInternalCamera(
        std::uint32_t code,
        const android::Parcel& data) {
    ParcelPositionGuard positionGuard(data);
    data.setDataPosition(0);
    if (!data.enforceInterface(
                android::String16(kCameraServiceListenerDescriptor))) {
        return false;
    }

    std::int32_t ignored = 0;
    android::String16 cameraId;
    android::String16 secondaryString;
    bool secondaryIsPhysicalCameraId = false;
    bool decoded = false;
    switch (code) {
        case kOnStatusChangedCode:
        case kOnTorchStatusChangedCode:
            decoded = data.readInt32(&ignored) == android::OK &&
                    readRequiredString16(data, &cameraId);
            break;
        case kOnPhysicalCameraStatusChangedCode:
            decoded = data.readInt32(&ignored) == android::OK &&
                    readRequiredString16(data, &cameraId) &&
                    readRequiredString16(data, &secondaryString);
            secondaryIsPhysicalCameraId = decoded;
            break;
        case kOnTorchStrengthLevelChangedCode:
            decoded = readRequiredString16(data, &cameraId) &&
                    data.readInt32(&ignored) == android::OK;
            break;
        case kOnCameraOpenedCode:
            decoded = readRequiredString16(data, &cameraId) &&
                    readRequiredString16(data, &secondaryString);
            break;
        case kOnCameraClosedCode:
            decoded = readRequiredString16(data, &cameraId);
            break;
        case kOnCameraAccessPrioritiesChangedCode:
            return false;
        default:
            return false;
    }
    if (!decoded || data.dataPosition() != data.dataSize()) {
        return false;
    }
    return isInternalId(cameraId) ||
            (secondaryIsPhysicalCameraId && isInternalId(secondaryString));
}

class FilteringCameraServiceListener final : public android::BBinder {
public:
    explicit FilteringCameraServiceListener(android::sp<android::IBinder> target)
        : target_(std::move(target)) {}

    const android::String16& getInterfaceDescriptor() const override {
        static const android::String16 descriptor(
                kCameraServiceListenerDescriptor);
        return descriptor;
    }

protected:
    android::status_t onTransact(
            std::uint32_t code,
            const android::Parcel& data,
            android::Parcel* reply,
            std::uint32_t flags) override {
        if (callbackReferencesInternalCamera(code, data)) {
            gListenerCallbacksFiltered.fetch_add(1, std::memory_order_relaxed);
            return android::OK;
        }
        return target_->transact(code, data, reply, flags);
    }

private:
    const android::sp<android::IBinder> target_;
};

struct CameraStatusRecord {
    android::String16 cameraId;
    std::int32_t status = 0;
    std::vector<android::String16> unavailablePhysicalIds;
    android::String16 clientPackage;
};

bool readCameraStatusRecord(
        const android::Parcel& parcel,
        CameraStatusRecord* record) {
    std::int32_t present = 0;
    std::optional<android::String16> cameraId;
    std::optional<android::String16> clientPackage;
    if (parcel.readInt32(&present) != android::OK ||
        present != kParcelablePresent ||
        parcel.readString16(&cameraId) != android::OK || !cameraId.has_value() ||
        cameraId->size() > kMaxCameraIdLength ||
        parcel.readInt32(&record->status) != android::OK ||
        parcel.readString16Vector(&record->unavailablePhysicalIds) != android::OK ||
        parcel.readString16(&clientPackage) != android::OK ||
        !clientPackage.has_value()) {
        return false;
    }
    record->cameraId = std::move(*cameraId);
    record->clientPackage = std::move(*clientPackage);
    return true;
}

bool writeCameraStatusRecord(
        const CameraStatusRecord& record,
        android::Parcel* parcel) {
    return parcel->writeInt32(kParcelablePresent) == android::OK &&
            parcel->writeString16(record.cameraId) == android::OK &&
            parcel->writeInt32(record.status) == android::OK &&
            parcel->writeString16Vector(record.unavailablePhysicalIds) ==
                    android::OK &&
            parcel->writeString16(record.clientPackage) == android::OK;
}

}  // namespace

CameraListenerRequestRouteStatus wrapAndroid14CameraListenerRequest(
        const android::Parcel& input,
        android::Parcel* output) noexcept {
    if (output == nullptr || output == &input) {
        return CameraListenerRequestRouteStatus::kMalformedRequest;
    }
    ParcelPositionGuard positionGuard(input);
    input.setDataPosition(0);
    if (!input.enforceInterface(android::String16(kCameraServiceDescriptor))) {
        return CameraListenerRequestRouteStatus::kMalformedRequest;
    }
    const std::size_t listenerStart = input.dataPosition();
    android::sp<android::IBinder> listener;
    if (input.readStrongBinder(&listener) != android::OK || listener == nullptr) {
        return CameraListenerRequestRouteStatus::kMalformedRequest;
    }
    const std::size_t listenerEnd = input.dataPosition();
    if (listenerEnd != input.dataSize()) {
        return CameraListenerRequestRouteStatus::kMalformedRequest;
    }

    output->setDataPosition(0);
    if (output->setDataSize(0) != android::OK ||
        output->appendFrom(&input, 0, listenerStart) != android::OK) {
        return CameraListenerRequestRouteStatus::kCopyFailed;
    }
    const android::sp<FilteringCameraServiceListener> wrapper =
            android::sp<FilteringCameraServiceListener>::make(
                    std::move(listener));
    if (output->writeStrongBinder(wrapper) != android::OK) {
        return CameraListenerRequestRouteStatus::kWriteFailed;
    }
    if (output->appendFrom(
                &input,
                listenerEnd,
                input.dataSize() - listenerEnd) != android::OK) {
        return CameraListenerRequestRouteStatus::kCopyFailed;
    }
    if (output->dataSize() != input.dataSize()) {
        return CameraListenerRequestRouteStatus::kWriteFailed;
    }
    output->setDataPosition(positionGuard.position());
    gListenerWrappers.fetch_add(1, std::memory_order_relaxed);
    return CameraListenerRequestRouteStatus::kWrapped;
}

CameraStatusReplyFilterStatus filterAndroid14CameraStatusReply(
        android::Parcel* reply) noexcept {
    if (reply == nullptr) {
        return CameraStatusReplyFilterStatus::kMalformedReply;
    }
    const std::size_t originalPosition = reply->dataPosition();
    reply->setDataPosition(0);
    android::binder::Status serviceStatus;
    if (serviceStatus.readFromParcel(*reply) != android::OK) {
        reply->setDataPosition(originalPosition);
        return CameraStatusReplyFilterStatus::kMalformedReply;
    }
    if (!serviceStatus.isOk()) {
        reply->setDataPosition(originalPosition);
        return CameraStatusReplyFilterStatus::kServiceError;
    }

    std::int32_t count = -1;
    if (reply->readInt32(&count) != android::OK || count < 0 ||
        count > kMaxCameraStatusCount) {
        reply->setDataPosition(originalPosition);
        return CameraStatusReplyFilterStatus::kMalformedReply;
    }
    std::vector<CameraStatusRecord> visible;
    visible.reserve(static_cast<std::size_t>(count));
    std::uint64_t filtered = 0;
    for (std::int32_t index = 0; index < count; ++index) {
        CameraStatusRecord record;
        if (!readCameraStatusRecord(*reply, &record)) {
            reply->setDataPosition(originalPosition);
            return CameraStatusReplyFilterStatus::kMalformedReply;
        }
        if (isInternalId(record.cameraId)) {
            ++filtered;
        } else {
            visible.emplace_back(std::move(record));
        }
    }
    if (reply->dataPosition() != reply->dataSize()) {
        reply->setDataPosition(originalPosition);
        return CameraStatusReplyFilterStatus::kMalformedReply;
    }
    if (filtered == 0) {
        reply->setDataPosition(originalPosition);
        return CameraStatusReplyFilterStatus::kUnchanged;
    }

    android::Parcel replacement;
    if (serviceStatus.writeToParcel(&replacement) != android::OK ||
        replacement.writeInt32(static_cast<std::int32_t>(visible.size())) !=
                android::OK) {
        reply->setDataPosition(originalPosition);
        return CameraStatusReplyFilterStatus::kWriteFailed;
    }
    for (const CameraStatusRecord& record : visible) {
        if (!writeCameraStatusRecord(record, &replacement)) {
            reply->setDataPosition(originalPosition);
            return CameraStatusReplyFilterStatus::kWriteFailed;
        }
    }
    reply->freeData();
    if (reply->appendFrom(&replacement, 0, replacement.dataSize()) != android::OK) {
        return CameraStatusReplyFilterStatus::kWriteFailed;
    }
    reply->setDataPosition(0);
    gStatusRecordsFiltered.fetch_add(filtered, std::memory_order_relaxed);
    return CameraStatusReplyFilterStatus::kFiltered;
}

const char* cameraListenerRequestRouteStatusName(
        CameraListenerRequestRouteStatus status) noexcept {
    switch (status) {
        case CameraListenerRequestRouteStatus::kWrapped: return "wrapped";
        case CameraListenerRequestRouteStatus::kMalformedRequest:
            return "malformed_request";
        case CameraListenerRequestRouteStatus::kCopyFailed: return "copy_failed";
        case CameraListenerRequestRouteStatus::kWriteFailed: return "write_failed";
    }
    return "unknown";
}

const char* cameraStatusReplyFilterStatusName(
        CameraStatusReplyFilterStatus status) noexcept {
    switch (status) {
        case CameraStatusReplyFilterStatus::kFiltered: return "filtered";
        case CameraStatusReplyFilterStatus::kUnchanged: return "unchanged";
        case CameraStatusReplyFilterStatus::kMalformedReply:
            return "malformed_reply";
        case CameraStatusReplyFilterStatus::kServiceError: return "service_error";
        case CameraStatusReplyFilterStatus::kWriteFailed: return "write_failed";
    }
    return "unknown";
}

std::uint64_t android14CameraListenerWrappers() noexcept {
    return gListenerWrappers.load(std::memory_order_relaxed);
}

std::uint64_t android14CameraListenerCallbacksFiltered() noexcept {
    return gListenerCallbacksFiltered.load(std::memory_order_relaxed);
}

std::uint64_t android14CameraStatusRecordsFiltered() noexcept {
    return gStatusRecordsFiltered.load(std::memory_order_relaxed);
}

}  // namespace vcam::runtime
