/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#define LOG_TAG "VcamProviderHidl"

#include "VcamProvider.h"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <CameraDevice_3_4.h>
#include <hardware/camera_common.h>
#include <hardware/hardware.h>

#include "vcam/VendorTags.h"

namespace android::hardware::camera::provider::V2_4::implementation {

using common::V1_0::Status;

VcamProvider::VcamProvider() {
    const hw_module_t* rawModule = nullptr;
    const int loadResult = hw_get_module_by_class(
            CAMERA_HARDWARE_MODULE_ID, "vcam", &rawModule);
    if (loadResult != 0 || rawModule == nullptr) {
        LOG(ERROR) << "Unable to load camera.vcam module: " << loadResult;
        return;
    }

    module_ = new common::V1_0::helper::CameraModule(
            const_cast<camera_module_t*>(
                    reinterpret_cast<const camera_module_t*>(rawModule)));
    const int initResult = module_->init();
    if (initResult != 0 || module_->getNumberOfCameras() != 2) {
        LOG(ERROR) << "Unable to initialize camera.vcam module: " << initResult;
        module_.clear();
        return;
    }

    camera_info info{};
    for (int id = 0; id < 2; ++id) {
        if (module_->getCameraInfo(id, &info) != 0 ||
                info.device_version != CAMERA_DEVICE_API_VERSION_3_5) {
            LOG(ERROR) << "Invalid embedded Camera3 device " << id;
            module_.clear();
            cameraDeviceNames_.clear();
            return;
        }
    }
    cameraDeviceNames_.add(std::make_pair(std::string("0"),
                                         std::string(kBackDeviceName)));
    cameraDeviceNames_.add(std::make_pair(std::string("1"),
                                         std::string(kFrontDeviceName)));
    ready_ = true;
    enabled_ = android::base::GetBoolProperty(
            "ro.vendor.vcam.provider.enabled", false);
    LOG(INFO) << "VCAM Camera3 module ready; enumeration enabled=" << enabled_;
}

Return<Status> VcamProvider::setCallback(
        const sp<ICameraProviderCallback>& callback) {
    if (callback == nullptr) return Status::ILLEGAL_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = callback;
    LOG(INFO) << "VCAM HIDL provider callback registered";
    return Status::OK;
}

Return<void> VcamProvider::getVendorTags(getVendorTags_cb callback) {
    hidl_vec<common::V1_0::VendorTagSection> sections(1);
    sections[0].sectionName = vcam::kVcamClientPackageSection;
    sections[0].tags.resize(1);
    sections[0].tags[0].tagId = vcam::kVcamClientPackageTag;
    sections[0].tags[0].tagName = vcam::kVcamClientPackageName;
    sections[0].tags[0].tagType = common::V1_0::CameraMetadataType::BYTE;
    callback(Status::OK, sections);
    return Void();
}

Return<void> VcamProvider::getCameraIdList(getCameraIdList_cb callback) {
    hidl_vec<hidl_string> cameraIds;
    if (ready_ && enabled_) {
        cameraIds.resize(2);
        cameraIds[0] = kBackDeviceName;
        cameraIds[1] = kFrontDeviceName;
    }
    callback(Status::OK, cameraIds);
    return Void();
}

Return<void> VcamProvider::isSetTorchModeSupported(
        isSetTorchModeSupported_cb callback) {
    callback(Status::OK, true);
    return Void();
}

Return<void> VcamProvider::getCameraDeviceInterface_V1_x(
        const hidl_string&, getCameraDeviceInterface_V1_x_cb callback) {
    callback(Status::OPERATION_NOT_SUPPORTED, nullptr);
    return Void();
}

Return<void> VcamProvider::getCameraDeviceInterface_V3_x(
        const hidl_string& cameraDeviceName,
        getCameraDeviceInterface_V3_x_cb callback) {
    if (!ready_ || module_ == nullptr) {
        callback(Status::INTERNAL_ERROR, nullptr);
        return Void();
    }
    if (!enabled_) {
        callback(Status::ILLEGAL_ARGUMENT, nullptr);
        return Void();
    }

    std::string internalId;
    for (const auto& entry : cameraDeviceNames_) {
        if (entry.second == cameraDeviceName.c_str()) {
            internalId = entry.first;
            break;
        }
    }
    if (internalId.empty()) {
        callback(Status::ILLEGAL_ARGUMENT, nullptr);
        return Void();
    }

    sp<device::V3_4::implementation::CameraDevice> device =
            new device::V3_4::implementation::CameraDevice(
                    module_, internalId, cameraDeviceNames_);
    if (device == nullptr || device->isInitFailed()) {
        callback(Status::INTERNAL_ERROR, nullptr);
        return Void();
    }
    callback(Status::OK, device->getInterface());
    return Void();
}

}  // namespace android::hardware::camera::provider::V2_4::implementation
