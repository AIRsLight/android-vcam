#include "vcam/Android14ParcelObserver.h"

#include <binder/Binder.h>
#include <binder/Parcel.h>
#include <utils/String16.h>

#include <cassert>

namespace {

vcam::runtime::AbiRecipe recipe() {
    vcam::runtime::AbiRecipe value;
    value.transactions = {
        {"connect_api1", 3},
        {"connect_device", 4},
        {"add_listener", 5},
        {"concurrent_session_support", 7},
        {"get_camera_characteristics", 9},
        {"get_legacy_parameters", 12},
    };
    return value;
}

void writeToken(android::Parcel* parcel, const char16_t* descriptor) {
    assert(parcel->writeInterfaceToken(android::String16(descriptor)) == android::OK);
}

}  // namespace

int main() {
    const auto transactions = recipe();

    android::Parcel metadata;
    writeToken(&metadata, u"android.hardware.ICameraService");
    assert(metadata.writeString16(android::String16(u"0")) == android::OK);
    assert(metadata.writeInt32(34) == android::OK);
    assert(metadata.writeBool(false) == android::OK);
    const std::size_t metadataPosition = metadata.dataPosition();
    const auto metadataObservation =
            vcam::runtime::observeAndroid14CameraServiceParcel(transactions, 9, &metadata);
    assert(metadataObservation.status == vcam::runtime::ParcelObservationStatus::kObserved);
    assert(metadataObservation.cameraId == "0");
    assert(metadataObservation.packageName.empty());
    assert(metadata.dataPosition() == metadataPosition);

    android::Parcel camera1;
    writeToken(&camera1, u"android.hardware.ICameraService");
    const android::sp<android::BBinder> callback = android::sp<android::BBinder>::make();
    assert(camera1.writeStrongBinder(callback) == android::OK);
    assert(camera1.writeInt32(1) == android::OK);
    assert(camera1.writeString16(android::String16(u"com.example.camera1")) == android::OK);
    const std::size_t camera1Position = camera1.dataPosition();
    const auto camera1Observation =
            vcam::runtime::observeAndroid14CameraServiceParcel(transactions, 3, &camera1);
    assert(camera1Observation.status == vcam::runtime::ParcelObservationStatus::kObserved);
    assert(camera1Observation.cameraId == "1");
    assert(camera1Observation.packageName == "com.example.camera1");
    assert(camera1.dataPosition() == camera1Position);

    android::Parcel camera2;
    writeToken(&camera2, u"android.hardware.ICameraService");
    assert(camera2.writeStrongBinder(callback) == android::OK);
    assert(camera2.writeString16(android::String16(u"0")) == android::OK);
    assert(camera2.writeString16(android::String16(u"com.example.camera2")) == android::OK);
    const std::size_t camera2Position = camera2.dataPosition();
    const auto camera2Observation =
            vcam::runtime::observeAndroid14CameraServiceParcel(transactions, 4, &camera2);
    assert(camera2Observation.status == vcam::runtime::ParcelObservationStatus::kObserved);
    assert(camera2Observation.cameraId == "0");
    assert(camera2Observation.packageName == "com.example.camera2");
    assert(camera2.dataPosition() == camera2Position);

    android::Parcel wrongInterface;
    writeToken(&wrongInterface, u"android.hardware.NotCameraService");
    const std::size_t wrongPosition = wrongInterface.dataPosition();
    assert(vcam::runtime::observeAndroid14CameraServiceParcel(
            transactions, 9, &wrongInterface).status ==
           vcam::runtime::ParcelObservationStatus::kWrongInterface);
    assert(wrongInterface.dataPosition() == wrongPosition);

    android::Parcel malformedHeader;
    assert(malformedHeader.writeInt32(0) == android::OK);
    const std::size_t malformedHeaderPosition = malformedHeader.dataPosition();
    assert(vcam::runtime::observeAndroid14CameraServiceParcel(
            transactions, 9, &malformedHeader).status ==
           vcam::runtime::ParcelObservationStatus::kMalformedHeader);
    assert(malformedHeader.dataPosition() == malformedHeaderPosition);

    android::Parcel malformedPayload;
    writeToken(&malformedPayload, u"android.hardware.ICameraService");
    const std::size_t malformedPayloadPosition = malformedPayload.dataPosition();
    assert(vcam::runtime::observeAndroid14CameraServiceParcel(
            transactions, 9, &malformedPayload).status ==
           vcam::runtime::ParcelObservationStatus::kMalformedPayload);
    assert(malformedPayload.dataPosition() == malformedPayloadPosition);

    android::Parcel unsupported;
    writeToken(&unsupported, u"android.hardware.ICameraService");
    const std::size_t unsupportedPosition = unsupported.dataPosition();
    assert(vcam::runtime::observeAndroid14CameraServiceParcel(
            transactions, 7, &unsupported).status ==
           vcam::runtime::ParcelObservationStatus::kUnsupportedPayload);
    assert(unsupported.dataPosition() == unsupportedPosition);

    assert(vcam::runtime::observeAndroid14CameraServiceParcel(
            transactions, 999, nullptr).status ==
           vcam::runtime::ParcelObservationStatus::kNotRoutedTransaction);
    assert(vcam::runtime::observeAndroid14CameraServiceParcel(
            transactions, 9, nullptr).status ==
           vcam::runtime::ParcelObservationStatus::kNullParcel);
    return 0;
}
