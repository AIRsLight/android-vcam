#include "vcam/Android14CameraListenerFilter.h"

#include <binder/Binder.h>
#include <binder/Parcel.h>
#include <binder/Status.h>
#include <utils/String16.h>
#include <utils/String8.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr char16_t kServiceDescriptor[] =
        u"android.hardware.ICameraService";
constexpr char16_t kListenerDescriptor[] =
        u"android.hardware.ICameraServiceListener";
constexpr std::uint32_t kOnStatusChangedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 0;
constexpr std::uint32_t kOnPhysicalCameraStatusChangedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 1;
constexpr std::uint32_t kOnCameraAccessPrioritiesChangedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 4;
constexpr std::uint32_t kOnCameraOpenedCode =
        android::IBinder::FIRST_CALL_TRANSACTION + 5;

class RecordingListener final : public android::BBinder {
public:
    const android::String16& getInterfaceDescriptor() const override {
        static const android::String16 descriptor(kListenerDescriptor);
        return descriptor;
    }

    std::uint64_t forwarded = 0;
    std::uint32_t lastCode = 0;
    std::uint64_t deathLinks = 0;
    std::uint64_t deathUnlinks = 0;

    android::status_t linkToDeath(
            const android::sp<android::IBinder::DeathRecipient>&,
            void* = nullptr,
            std::uint32_t = 0) override {
        ++deathLinks;
        return android::OK;
    }

    android::status_t unlinkToDeath(
            const android::wp<android::IBinder::DeathRecipient>&,
            void* = nullptr,
            std::uint32_t = 0,
            android::wp<android::IBinder::DeathRecipient>* = nullptr) override {
        ++deathUnlinks;
        return android::OK;
    }

protected:
    android::status_t onTransact(
            std::uint32_t code,
            const android::Parcel&,
            android::Parcel*,
            std::uint32_t) override {
        ++forwarded;
        lastCode = code;
        return android::OK;
    }
};

class TestDeathRecipient final : public android::IBinder::DeathRecipient {
public:
    void binderDied(const android::wp<android::IBinder>&) override {}
};

void writeStatusCallback(android::Parcel* parcel, const char* cameraId) {
    assert(parcel->writeInterfaceToken(
                   android::String16(kListenerDescriptor)) == android::OK);
    assert(parcel->writeInt32(1) == android::OK);
    assert(parcel->writeString16(android::String16(cameraId)) == android::OK);
}

void writePhysicalStatusCallback(
        android::Parcel* parcel,
        const char* cameraId,
        const char* physicalCameraId) {
    assert(parcel->writeInterfaceToken(
                   android::String16(kListenerDescriptor)) == android::OK);
    assert(parcel->writeInt32(1) == android::OK);
    assert(parcel->writeString16(android::String16(cameraId)) == android::OK);
    assert(parcel->writeString16(
                   android::String16(physicalCameraId)) == android::OK);
}

void writeCameraOpenedCallback(android::Parcel* parcel, const char* cameraId) {
    assert(parcel->writeInterfaceToken(
                   android::String16(kListenerDescriptor)) == android::OK);
    assert(parcel->writeString16(android::String16(cameraId)) == android::OK);
    assert(parcel->writeString16(
                   android::String16("com.example.client")) == android::OK);
}

void writeStatusRecord(
        android::Parcel* parcel,
        const char* cameraId,
        std::int32_t status,
        const std::vector<android::String16>& unavailable = {}) {
    assert(parcel->writeInt32(1) == android::OK);
    assert(parcel->writeString16(android::String16(cameraId)) == android::OK);
    assert(parcel->writeInt32(status) == android::OK);
    assert(parcel->writeString16Vector(unavailable) == android::OK);
    assert(parcel->writeString16(
                   android::String16("com.example.owner")) == android::OK);
}

std::vector<std::string> readStatusIds(android::Parcel* parcel) {
    parcel->setDataPosition(0);
    android::binder::Status status;
    assert(status.readFromParcel(*parcel) == android::OK && status.isOk());
    std::int32_t count = -1;
    assert(parcel->readInt32(&count) == android::OK && count >= 0);
    std::vector<std::string> ids;
    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t present = 0;
        std::int32_t cameraStatus = 0;
        std::optional<android::String16> cameraId;
        std::vector<android::String16> unavailable;
        std::optional<android::String16> clientPackage;
        assert(parcel->readInt32(&present) == android::OK && present == 1);
        assert(parcel->readString16(&cameraId) == android::OK &&
               cameraId.has_value());
        assert(parcel->readInt32(&cameraStatus) == android::OK);
        assert(parcel->readString16Vector(&unavailable) == android::OK);
        assert(parcel->readString16(&clientPackage) == android::OK &&
               clientPackage.has_value());
        ids.emplace_back(android::String8(*cameraId).c_str());
    }
    assert(parcel->dataPosition() == parcel->dataSize());
    return ids;
}

}  // namespace

int main() {
    const android::sp<RecordingListener> target =
            android::sp<RecordingListener>::make();
    android::Parcel request;
    assert(request.writeInterfaceToken(android::String16(kServiceDescriptor)) ==
           android::OK);
    assert(request.writeStrongBinder(target) == android::OK);
    const std::size_t requestPosition = request.dataPosition();
    const std::vector<std::uint8_t> requestBytes(
            request.data(), request.data() + request.dataSize());

    android::Parcel wrappedRequest;
    assert(vcam::runtime::wrapAndroid14CameraListenerRequest(
                   request, &wrappedRequest) ==
           vcam::runtime::CameraListenerRequestRouteStatus::kWrapped);
    assert(request.dataPosition() == requestPosition);
    assert(request.dataSize() == requestBytes.size());
    assert(std::memcmp(request.data(), requestBytes.data(), requestBytes.size()) == 0);
    assert(request.debugReadAllStrongBinders().size() == 1);
    assert(wrappedRequest.debugReadAllStrongBinders().size() == 1);

    wrappedRequest.setDataPosition(0);
    assert(wrappedRequest.enforceInterface(android::String16(kServiceDescriptor)));
    android::sp<android::IBinder> routedListener;
    assert(wrappedRequest.readStrongBinder(&routedListener) == android::OK);
    assert(routedListener != nullptr && routedListener.get() != target.get());

    const android::sp<TestDeathRecipient> deathRecipient =
            android::sp<TestDeathRecipient>::make();
    assert(routedListener->linkToDeath(deathRecipient) == android::OK);
    assert(target->deathLinks == 1);
    assert(routedListener->unlinkToDeath(deathRecipient) == android::OK);
    assert(target->deathUnlinks == 1);

    android::Parcel removalRequest;
    assert(removalRequest.writeInterfaceToken(
                   android::String16(kServiceDescriptor)) == android::OK);
    assert(removalRequest.writeStrongBinder(target) == android::OK);
    android::Parcel wrappedRemovalRequest;
    assert(vcam::runtime::wrapAndroid14CameraListenerRemovalRequest(
                   removalRequest, &wrappedRemovalRequest) ==
           vcam::runtime::CameraListenerRequestRouteStatus::kWrapped);
    wrappedRemovalRequest.setDataPosition(0);
    assert(wrappedRemovalRequest.enforceInterface(
            android::String16(kServiceDescriptor)));
    android::sp<android::IBinder> removalListener;
    assert(wrappedRemovalRequest.readStrongBinder(&removalListener) == android::OK);
    assert(removalListener.get() == routedListener.get());

    const android::sp<RecordingListener> unknownTarget =
            android::sp<RecordingListener>::make();
    android::Parcel unknownRemovalRequest;
    assert(unknownRemovalRequest.writeInterfaceToken(
                   android::String16(kServiceDescriptor)) == android::OK);
    assert(unknownRemovalRequest.writeStrongBinder(unknownTarget) == android::OK);
    assert(vcam::runtime::wrapAndroid14CameraListenerRemovalRequest(
                   unknownRemovalRequest, &wrappedRemovalRequest) ==
           vcam::runtime::CameraListenerRequestRouteStatus::kNoRegisteredWrapper);

    android::Parcel publicStatus;
    writeStatusCallback(&publicStatus, "0");
    assert(routedListener->transact(
                   kOnStatusChangedCode, publicStatus, nullptr,
                   android::IBinder::FLAG_ONEWAY) == android::OK);
    assert(target->forwarded == 1 && target->lastCode == kOnStatusChangedCode);

    android::Parcel internalStatus;
    writeStatusCallback(&internalStatus, "1000");
    assert(routedListener->transact(
                   kOnStatusChangedCode, internalStatus, nullptr,
                   android::IBinder::FLAG_ONEWAY) == android::OK);
    assert(target->forwarded == 1);

    android::Parcel internalPhysical;
    writePhysicalStatusCallback(&internalPhysical, "0", "1001");
    assert(routedListener->transact(
                   kOnPhysicalCameraStatusChangedCode, internalPhysical, nullptr,
                   android::IBinder::FLAG_ONEWAY) == android::OK);
    assert(target->forwarded == 1);

    android::Parcel internalOpened;
    writeCameraOpenedCallback(&internalOpened, "1000");
    assert(routedListener->transact(
                   kOnCameraOpenedCode, internalOpened, nullptr,
                   android::IBinder::FLAG_ONEWAY) == android::OK);
    assert(target->forwarded == 1);

    android::Parcel priorities;
    assert(priorities.writeInterfaceToken(
                   android::String16(kListenerDescriptor)) == android::OK);
    assert(routedListener->transact(
                   kOnCameraAccessPrioritiesChangedCode, priorities, nullptr,
                   android::IBinder::FLAG_ONEWAY) == android::OK);
    assert(target->forwarded == 2 &&
           target->lastCode == kOnCameraAccessPrioritiesChangedCode);

    android::Parcel reply;
    assert(android::binder::Status::ok().writeToParcel(&reply) == android::OK);
    assert(reply.writeInt32(4) == android::OK);
    writeStatusRecord(&reply, "0", 1);
    writeStatusRecord(&reply, "1000", 1);
    writeStatusRecord(
            &reply, "1", -2, {android::String16("logical-subcamera")});
    writeStatusRecord(&reply, "1001", 1);
    assert(vcam::runtime::filterAndroid14CameraStatusReply(&reply) ==
           vcam::runtime::CameraStatusReplyFilterStatus::kFiltered);
    assert(readStatusIds(&reply) == std::vector<std::string>({"0", "1"}));

    android::Parcel unchangedReply;
    assert(android::binder::Status::ok().writeToParcel(&unchangedReply) ==
           android::OK);
    assert(unchangedReply.writeInt32(1) == android::OK);
    writeStatusRecord(&unchangedReply, "0", 1);
    const std::size_t unchangedPosition = unchangedReply.dataPosition();
    const std::vector<std::uint8_t> unchangedBytes(
            unchangedReply.data(),
            unchangedReply.data() + unchangedReply.dataSize());
    assert(vcam::runtime::filterAndroid14CameraStatusReply(&unchangedReply) ==
           vcam::runtime::CameraStatusReplyFilterStatus::kUnchanged);
    assert(unchangedReply.dataPosition() == unchangedPosition);
    assert(std::memcmp(
                   unchangedReply.data(), unchangedBytes.data(),
                   unchangedBytes.size()) == 0);

    android::Parcel malformedReply;
    assert(vcam::runtime::filterAndroid14CameraStatusReply(&malformedReply) ==
           vcam::runtime::CameraStatusReplyFilterStatus::kMalformedReply);
    assert(vcam::runtime::wrapAndroid14CameraListenerRequest(
                   request, nullptr) ==
           vcam::runtime::CameraListenerRequestRouteStatus::kMalformedRequest);

    assert(vcam::runtime::android14CameraListenerWrappers() == 1);
    assert(vcam::runtime::android14CameraListenerCallbacksFiltered() == 3);
    assert(vcam::runtime::android14CameraStatusRecordsFiltered() == 2);
    return 0;
}
