#include "vcam/Android14ParcelObserver.h"

#include <binder/IBinder.h>
#include <binder/IPCThreadState.h>
#include <binder/Parcel.h>

#include <limits>

namespace vcam::runtime {
namespace {

constexpr char16_t kCameraServiceDescriptor[] = u"android.hardware.ICameraService";
constexpr std::size_t kMaxObservedStringLength = 255;

class ParcelPositionGuard final {
public:
    explicit ParcelPositionGuard(const android::Parcel& parcel)
        : parcel_(parcel), position_(parcel.dataPosition()) {}

    ~ParcelPositionGuard() { parcel_.setDataPosition(position_); }

    std::size_t position() const { return position_; }

private:
    const android::Parcel& parcel_;
    std::size_t position_;
};

bool readAsciiString16(const android::Parcel& parcel, std::string* output) {
    std::size_t length = 0;
    const char16_t* value = parcel.readString16Inplace(&length);
    if (value == nullptr || length > kMaxObservedStringLength) {
        return false;
    }
    output->clear();
    output->reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        if (value[index] > 0x7f) {
            return false;
        }
        output->push_back(static_cast<char>(value[index]));
    }
    return true;
}

enum class HeaderStatus { kOk, kMalformed, kWrongInterface };

HeaderStatus readAndCheckHeader(const android::Parcel& parcel) {
    std::int32_t ignored = 0;
    std::int32_t header = 0;
    if (parcel.readInt32(&ignored) != android::OK ||
        parcel.readInt32(&ignored) != android::OK ||
        parcel.readInt32(&header) != android::OK) {
        return HeaderStatus::kMalformed;
    }
    constexpr std::int32_t expectedHeader =
            ('S' << 24) | ('Y' << 16) | ('S' << 8) | 'T';
    if (header != expectedHeader) {
        return HeaderStatus::kWrongInterface;
    }
    std::size_t length = 0;
    const char16_t* descriptor = parcel.readString16Inplace(&length);
    constexpr std::size_t expectedLength =
            sizeof(kCameraServiceDescriptor) / sizeof(kCameraServiceDescriptor[0]) - 1;
    if (descriptor == nullptr || length != expectedLength) {
        return HeaderStatus::kWrongInterface;
    }
    for (std::size_t index = 0; index < expectedLength; ++index) {
        if (descriptor[index] != kCameraServiceDescriptor[index]) {
            return HeaderStatus::kWrongInterface;
        }
    }
    return HeaderStatus::kOk;
}

bool skipStrongBinder(const android::Parcel& parcel) {
    android::sp<android::IBinder> ignored;
    return parcel.readStrongBinder(&ignored) == android::OK;
}

bool readIntegerCameraId(const android::Parcel& parcel, std::string* output) {
    std::int32_t cameraId = -1;
    if (parcel.readInt32(&cameraId) != android::OK || cameraId < 0) {
        return false;
    }
    *output = std::to_string(cameraId);
    return true;
}

}  // namespace

ParcelObservation observeAndroid14CameraServiceParcel(
        const AbiRecipe& recipe,
        std::uint32_t code,
        const void* dataParcel) {
    ParcelObservation result;
    result.transaction = classifyBinderTransaction(recipe, code);
    if (result.transaction.payloadShape == BinderPayloadShape::kPassThrough) {
        result.status = ParcelObservationStatus::kNotRoutedTransaction;
        return result;
    }
    if (dataParcel == nullptr) {
        result.status = ParcelObservationStatus::kNullParcel;
        return result;
    }

    const auto& parcel = *static_cast<const android::Parcel*>(dataParcel);
    ParcelPositionGuard positionGuard(parcel);
    result.initialDataPosition = positionGuard.position();
    parcel.setDataPosition(0);
    const HeaderStatus headerStatus = readAndCheckHeader(parcel);
    if (headerStatus != HeaderStatus::kOk) {
        result.status = headerStatus == HeaderStatus::kMalformed
                ? ParcelObservationStatus::kMalformedHeader
                : ParcelObservationStatus::kWrongInterface;
        result.finalDataPosition = positionGuard.position();
        return result;
    }

    const android::IPCThreadState* threadState = android::IPCThreadState::self();
    result.callingUid = threadState->getCallingUid();
    result.callingPid = threadState->getCallingPid();

    bool decoded = false;
    switch (result.transaction.payloadShape) {
        case BinderPayloadShape::kConnectApi1:
            decoded = skipStrongBinder(parcel);
            result.cameraIdStart = parcel.dataPosition();
            decoded = decoded && readIntegerCameraId(parcel, &result.cameraId);
            result.cameraIdEnd = parcel.dataPosition();
            decoded = decoded && readAsciiString16(parcel, &result.packageName);
            break;
        case BinderPayloadShape::kConnectDevice:
            decoded = skipStrongBinder(parcel);
            result.cameraIdStart = parcel.dataPosition();
            decoded = decoded && readAsciiString16(parcel, &result.cameraId);
            result.cameraIdEnd = parcel.dataPosition();
            decoded = decoded && readAsciiString16(parcel, &result.packageName);
            break;
        case BinderPayloadShape::kStringCameraId:
            result.cameraIdStart = parcel.dataPosition();
            decoded = readAsciiString16(parcel, &result.cameraId);
            result.cameraIdEnd = parcel.dataPosition();
            break;
        case BinderPayloadShape::kIntegerCameraId:
            result.cameraIdStart = parcel.dataPosition();
            decoded = readIntegerCameraId(parcel, &result.cameraId);
            result.cameraIdEnd = parcel.dataPosition();
            break;
        case BinderPayloadShape::kListener:
        case BinderPayloadShape::kListenerRemoval:
        case BinderPayloadShape::kConcurrentIds:
            decoded = true;
            break;
        case BinderPayloadShape::kConcurrentSessionConfiguration:
            result.status = ParcelObservationStatus::kUnsupportedPayload;
            result.finalDataPosition = positionGuard.position();
            return result;
        case BinderPayloadShape::kPassThrough:
            break;
    }
    result.status = decoded ? ParcelObservationStatus::kObserved
                            : ParcelObservationStatus::kMalformedPayload;
    result.finalDataPosition = positionGuard.position();
    return result;
}

const char* parcelObservationStatusName(ParcelObservationStatus status) {
    switch (status) {
        case ParcelObservationStatus::kObserved: return "observed";
        case ParcelObservationStatus::kNotRoutedTransaction: return "not_routed_transaction";
        case ParcelObservationStatus::kNullParcel: return "null_parcel";
        case ParcelObservationStatus::kMalformedHeader: return "malformed_header";
        case ParcelObservationStatus::kWrongInterface: return "wrong_interface";
        case ParcelObservationStatus::kMalformedPayload: return "malformed_payload";
        case ParcelObservationStatus::kUnsupportedPayload: return "unsupported_payload";
    }
    return "unknown";
}

}  // namespace vcam::runtime
