#include "vcam/Android14ParcelObserver.h"
#include "vcam/Android14BinderShadowObserver.h"
#include "vcam/Android14CameraIdRewriter.h"
#include "vcam/Android14CameraServiceProfile.h"
#include "vcam/BinderPassThroughBridge.h"
#include "vcam/CameraCallerIdentityClassifier.h"

#include <binder/Binder.h>
#include <binder/Parcel.h>
#include <utils/String16.h>

#include <cassert>
#include <cstring>
#include <vector>

namespace {

std::size_t expectedOriginalPosition = 0;
bool originalObservedRestoredPosition = false;

std::int32_t originalOnTransact(
        void*, std::uint32_t code, const void* dataParcel, void*, std::uint32_t flags) {
    const auto* parcel = static_cast<const android::Parcel*>(dataParcel);
    originalObservedRestoredPosition =
            parcel != nullptr && parcel->dataPosition() == expectedOriginalPosition;
    return static_cast<std::int32_t>(code + flags + 100);
}

vcam::runtime::AbiRecipe recipe() {
    vcam::runtime::AbiRecipe value;
    value.transactions = {
        {"connect_api1", 3},
        {"connect_device", 4},
        {"add_listener", 5},
        {"concurrent_session_support", 7},
        {"remove_listener", 8},
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
    const auto claimedIdentity =
            vcam::runtime::classifyCameraCallerIdentity(
                    true, true, 10123, 456, "com.example.camera");
    assert(claimedIdentity.kind ==
           vcam::runtime::CameraCallerIdentityKind::kClaimedPackage);
    assert(claimedIdentity.requiresPackageVerification);
    const auto uidOnlyIdentity =
            vcam::runtime::classifyCameraCallerIdentity(
                    true, false, 10123, 456, "");
    assert(uidOnlyIdentity.kind ==
           vcam::runtime::CameraCallerIdentityKind::kUidOnly);
    assert(!uidOnlyIdentity.requiresPackageVerification);
    assert(vcam::runtime::classifyCameraCallerIdentity(
            true, true, -1, 456, "com.example.camera").kind ==
           vcam::runtime::CameraCallerIdentityKind::kUnavailable);
    assert(vcam::runtime::classifyCameraCallerIdentity(
            false, true, 10123, 456, "com.example.camera").kind ==
           vcam::runtime::CameraCallerIdentityKind::kNotApplicable);

    assert(vcam::runtime::matchesNx769jAndroid14CameraServiceProfile(
            vcam::runtime::kNx769jAndroid14Fingerprint));
    assert(!vcam::runtime::matchesNx769jAndroid14CameraServiceProfile(
            "unknown/device:14/test"));
    const auto qualifiedProtocol =
            vcam::runtime::selectAndroid14CameraServiceProtocol(
                    34, vcam::runtime::kNx769jAndroid14Fingerprint);
    assert(qualifiedProtocol.observationAllowed);
    assert(qualifiedProtocol.routingAllowed);
    assert(qualifiedProtocol.confidence ==
           vcam::runtime::Android14CameraServiceProtocolConfidence::kQualified);
    assert(std::strcmp(qualifiedProtocol.profileName,
                       vcam::runtime::kNx769jAndroid14ProfileName) == 0);
    const auto candidateProtocol =
            vcam::runtime::selectAndroid14CameraServiceProtocol(
                    34, "vendor/product/device:14/UP1A/build:user/release-keys");
    assert(candidateProtocol.observationAllowed);
    assert(!candidateProtocol.routingAllowed);
    assert(candidateProtocol.confidence ==
           vcam::runtime::Android14CameraServiceProtocolConfidence::kProbeCandidate);
    assert(std::strcmp(candidateProtocol.profileName,
                       vcam::runtime::kAndroid14InitialCandidateProfileName) == 0);
    const auto unsupportedProtocol =
            vcam::runtime::selectAndroid14CameraServiceProtocol(
                    33, vcam::runtime::kNx769jAndroid14Fingerprint);
    assert(!unsupportedProtocol.observationAllowed);
    assert(!unsupportedProtocol.routingAllowed);
    assert(unsupportedProtocol.recipe.transactions.empty());
    assert(std::strcmp(vcam::runtime::android14CameraServiceProtocolConfidenceName(
                               candidateProtocol.confidence),
                       "probe_candidate") == 0);
    const auto qualifiedRecipe =
            vcam::runtime::makeNx769jAndroid14CameraServiceRecipe();
    assert(qualifiedRecipe.schema == 2);
    assert(qualifiedRecipe.transactions.size() == 12);

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
    const android::sp<android::BBinder> trailingBinder =
            android::sp<android::BBinder>::make();
    assert(camera2.writeStrongBinder(trailingBinder) == android::OK);
    const std::size_t camera2Position = camera2.dataPosition();
    const auto camera2Observation =
            vcam::runtime::observeAndroid14CameraServiceParcel(transactions, 4, &camera2);
    assert(camera2Observation.status == vcam::runtime::ParcelObservationStatus::kObserved);
    assert(camera2Observation.cameraId == "0");
    assert(camera2Observation.packageName == "com.example.camera2");
    assert(camera2.dataPosition() == camera2Position);
    const std::vector<std::uint8_t> originalCamera2Bytes(
            camera2.data(), camera2.data() + camera2.dataSize());
    android::Parcel rewrittenCamera2;
    assert(vcam::runtime::rewriteAndroid14CameraId(
            camera2Observation, "1", camera2, &rewrittenCamera2) ==
           vcam::runtime::CameraIdRewriteStatus::kRewritten);
    assert(camera2.dataPosition() == camera2Position);
    assert(camera2.dataSize() == rewrittenCamera2.dataSize());
    assert(std::memcmp(camera2.data(), originalCamera2Bytes.data(),
                       originalCamera2Bytes.size()) == 0);
    assert(camera2.debugReadAllStrongBinders().size() == 2);
    assert(rewrittenCamera2.debugReadAllStrongBinders().size() == 2);
    const auto rewrittenCamera2Observation =
            vcam::runtime::observeAndroid14CameraServiceParcel(
                    transactions, 4, &rewrittenCamera2);
    assert(rewrittenCamera2Observation.status ==
           vcam::runtime::ParcelObservationStatus::kObserved);
    assert(rewrittenCamera2Observation.cameraId == "1");
    assert(rewrittenCamera2Observation.packageName == "com.example.camera2");
    android::Parcel expandedCamera2;
    assert(vcam::runtime::rewriteAndroid14CameraId(
            camera2Observation, "1000", camera2, &expandedCamera2) ==
           vcam::runtime::CameraIdRewriteStatus::kRewritten);
    assert(expandedCamera2.dataSize() > camera2.dataSize());
    assert(expandedCamera2.dataPosition() == expandedCamera2.dataSize());
    assert(expandedCamera2.debugReadAllStrongBinders().size() == 2);
    const auto expandedCamera2Observation =
            vcam::runtime::observeAndroid14CameraServiceParcel(
                    transactions, 4, &expandedCamera2);
    assert(expandedCamera2Observation.status ==
           vcam::runtime::ParcelObservationStatus::kObserved);
    assert(expandedCamera2Observation.cameraId == "1000");
    assert(expandedCamera2Observation.packageName == "com.example.camera2");

    android::Parcel contractedCamera2;
    assert(vcam::runtime::rewriteAndroid14CameraId(
            expandedCamera2Observation, "7", expandedCamera2,
            &contractedCamera2) ==
           vcam::runtime::CameraIdRewriteStatus::kRewritten);
    assert(contractedCamera2.dataSize() == camera2.dataSize());
    assert(contractedCamera2.debugReadAllStrongBinders().size() == 2);
    const auto contractedCamera2Observation =
            vcam::runtime::observeAndroid14CameraServiceParcel(
                    transactions, 4, &contractedCamera2);
    assert(contractedCamera2Observation.cameraId == "7");
    assert(contractedCamera2Observation.packageName == "com.example.camera2");

    android::Parcel rewrittenCamera1;
    assert(vcam::runtime::rewriteAndroid14CameraId(
            camera1Observation, "0", camera1, &rewrittenCamera1) ==
           vcam::runtime::CameraIdRewriteStatus::kRewritten);
    const auto rewrittenCamera1Observation =
            vcam::runtime::observeAndroid14CameraServiceParcel(
                    transactions, 3, &rewrittenCamera1);
    assert(rewrittenCamera1Observation.cameraId == "0");
    assert(rewrittenCamera1Observation.packageName == "com.example.camera1");

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

    vcam::runtime::Android14BinderShadowObserver shadow(transactions);
    vcam::runtime::BinderPassThroughBridge bridge;
    assert(bridge.bindObserverOnce(
            &vcam::runtime::Android14BinderShadowObserver::bridgeCallback, &shadow));
    assert(bridge.bindOnce(&originalOnTransact));
    expectedOriginalPosition = metadata.dataPosition();
    assert(bridge.invoke(nullptr, 9, &metadata, nullptr, 2) == 111);
    assert(originalObservedRestoredPosition);
    assert(metadata.dataPosition() == expectedOriginalPosition);

    assert(bridge.invoke(nullptr, 999, nullptr, nullptr, 0) == 1099);
    assert(bridge.invoke(nullptr, 7, &unsupported, nullptr, 0) == 107);
    assert(bridge.invoke(nullptr, 9, nullptr, nullptr, 0) == 109);
    const auto stats = shadow.stats();
    assert(stats.total == 4);
    assert(stats.observed == 1);
    assert(stats.ignored == 1);
    assert(stats.rejected == 1);
    assert(stats.unsupported == 1);
    assert(stats.claimedPackage == 0);
    assert(stats.uidOnly == 1);
    assert(stats.identityUnavailable == 0);
    const std::uint64_t characteristicsBit =
            vcam::runtime::android14ProtocolRoleBit(
                    "get_camera_characteristics");
    const std::uint64_t concurrentSessionBit =
            vcam::runtime::android14ProtocolRoleBit(
                    "concurrent_session_support");
    assert((stats.protocolEvidence.seenMask & characteristicsBit) != 0);
    assert((stats.protocolEvidence.validMask & characteristicsBit) != 0);
    assert((stats.protocolEvidence.invalidMask & characteristicsBit) != 0);
    assert((stats.protocolEvidence.unsupportedMask & concurrentSessionBit) != 0);
    assert(stats.protocolEvidence.verdict ==
           vcam::runtime::Android14ProtocolEvidenceVerdict::kRejected);
    return 0;
}
