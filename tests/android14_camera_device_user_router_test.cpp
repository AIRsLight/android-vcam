#include "vcam/Android14CameraDeviceUserRouter.h"

#include <binder/Binder.h>
#include <binder/Parcel.h>
#include <binder/Status.h>
#include <utils/String16.h>
#include <utils/String8.h>

#include <cassert>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr char16_t kDescriptor[] =
        u"android.hardware.camera2.ICameraDeviceUser";
constexpr std::uint32_t kSubmitRequestCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 1;
constexpr std::uint32_t kSubmitRequestListCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 2;
constexpr std::uint32_t kUseBufferQueue = 0x62717565;  // 'bque'

class RecordingBinder final : public android::BBinder {
public:
    const android::String16& getInterfaceDescriptor() const override {
        static const android::String16 descriptor(kDescriptor);
        return descriptor;
    }

    std::string observedId;
    std::size_t observedBinderObjects = 0;

protected:
    android::status_t onTransact(
            std::uint32_t code,
            const android::Parcel& data,
            android::Parcel*,
            std::uint32_t) override {
        data.setDataPosition(0);
        assert(data.enforceInterface(android::String16(kDescriptor)));
        if (code == kSubmitRequestListCode) {
            std::int32_t count = 0;
            assert(data.readInt32(&count) == android::OK && count == 1);
        }
        std::int32_t present = 0;
        std::int32_t settingsCount = 0;
        assert(data.readInt32(&present) == android::OK && present == 1);
        assert(data.readInt32(&settingsCount) == android::OK && settingsCount == 1);
        observedId = android::String8(data.readString16()).c_str();
        observedBinderObjects = data.debugReadAllStrongBinders().size();
        return android::OK;
    }
};

void writeRequest(
        android::Parcel* parcel,
        const char* cameraId,
        const android::sp<android::IBinder>& surfaceBinder) {
    assert(parcel->writeInt32(1) == android::OK);  // Parcelable present.
    assert(parcel->writeInt32(1) == android::OK);  // Settings count.
    assert(parcel->writeString16(android::String16(cameraId)) == android::OK);
    assert(parcel->writeInt32(0) == android::OK);  // Empty metadata.
    assert(parcel->writeInt32(0) == android::OK);  // Not reprocessing.
    assert(parcel->writeInt32(surfaceBinder == nullptr ? 0 : 1) == android::OK);
    if (surfaceBinder != nullptr) {
        assert(parcel->writeString16(
                       android::String16(u"android.view.Surface")) == android::OK);
        assert(parcel->writeString16(android::String16(u"test")) == android::OK);
        assert(parcel->writeInt32(0) == android::OK);
        assert(parcel->writeUint32(kUseBufferQueue) == android::OK);
        assert(parcel->writeStrongBinder(surfaceBinder) == android::OK);
        assert(parcel->writeStrongBinder(nullptr) == android::OK);
    }
    assert(parcel->writeInt32(0) == android::OK);  // Stream/surface indices.
    assert(parcel->writeInt32(0) == android::OK);  // No user tag.
}

void writeRequestParcel(
        android::Parcel* parcel,
        std::uint32_t code,
        const char* cameraId,
        const android::sp<android::IBinder>& surfaceBinder = nullptr) {
    assert(parcel->writeInterfaceToken(android::String16(kDescriptor)) == android::OK);
    if (code == kSubmitRequestListCode) {
        assert(parcel->writeInt32(1) == android::OK);
    }
    writeRequest(parcel, cameraId, surfaceBinder);
    assert(parcel->writeBool(true) == android::OK);
}

}  // namespace

int main() {
    const android::sp<RecordingBinder> target =
            android::sp<RecordingBinder>::make();
    android::Parcel connectReply;
    assert(android::binder::Status::ok().writeToParcel(&connectReply) == android::OK);
    assert(connectReply.writeStrongBinder(target) == android::OK);
    assert(vcam::runtime::wrapAndroid14CameraDeviceUserReply(
                   &connectReply, "0", "1") ==
           vcam::runtime::CameraDeviceUserReplyRouteStatus::kWrapped);

    android::binder::Status serviceStatus;
    assert(serviceStatus.readFromParcel(connectReply) == android::OK);
    assert(serviceStatus.isOk());
    android::sp<android::IBinder> routed;
    assert(connectReply.readStrongBinder(&routed) == android::OK);
    assert(routed != nullptr && routed.get() != target.get());

    const android::sp<android::BBinder> surface = android::sp<android::BBinder>::make();
    android::Parcel single;
    writeRequestParcel(&single, kSubmitRequestCode, "0", surface);
    const std::vector<std::uint8_t> singleBytes(
            single.data(), single.data() + single.dataSize());
    const std::size_t singleBinderObjects =
            single.debugReadAllStrongBinders().size();
    assert(singleBinderObjects > 0);
    assert(routed->transact(kSubmitRequestCode, single, nullptr, 0) == android::OK);
    assert(target->observedId == "1");
    assert(target->observedBinderObjects == singleBinderObjects);
    assert(single.dataSize() == singleBytes.size());
    assert(std::memcmp(single.data(), singleBytes.data(), singleBytes.size()) == 0);

    android::Parcel batch;
    writeRequestParcel(&batch, kSubmitRequestListCode, "0");
    assert(routed->transact(kSubmitRequestListCode, batch, nullptr, 0) == android::OK);
    assert(target->observedId == "1");

    android::Parcel wrongId;
    writeRequestParcel(&wrongId, kSubmitRequestCode, "2");
    assert(routed->transact(kSubmitRequestCode, wrongId, nullptr, 0) == android::OK);
    assert(target->observedId == "2");

    assert(vcam::runtime::android14CameraDeviceUserWrappers() == 1);
    assert(vcam::runtime::android14CameraRequestBatchesRewritten() == 2);
    assert(vcam::runtime::android14CameraRequestsRewritten() == 2);
    assert(vcam::runtime::android14CameraRequestBatchesSkipped() == 1);

    android::Parcel malformed;
    assert(vcam::runtime::wrapAndroid14CameraDeviceUserReply(
                   &malformed, "0", "1") ==
           vcam::runtime::CameraDeviceUserReplyRouteStatus::kMalformedReply);
    assert(vcam::runtime::wrapAndroid14CameraDeviceUserReply(
                   &connectReply, "0", "0") ==
           vcam::runtime::CameraDeviceUserReplyRouteStatus::kNotEligible);
    return 0;
}
