/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#define LOG_TAG "VcamProviderHidl"

#include "VcamProvider.h"

#include <android-base/logging.h>

namespace android::hardware::camera::provider::V2_4::implementation {

using common::V1_0::Status;

Return<Status> VcamProvider::setCallback(
        const sp<ICameraProviderCallback>& callback) {
    if (callback == nullptr) return Status::ILLEGAL_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = callback;
    LOG(INFO) << "VCAM HIDL provider callback registered";
    return Status::OK;
}

Return<void> VcamProvider::getVendorTags(getVendorTags_cb callback) {
    hidl_vec<common::V1_0::VendorTagSection> tags;
    callback(Status::OK, tags);
    return Void();
}

Return<void> VcamProvider::getCameraIdList(getCameraIdList_cb callback) {
    // The external VCAM device will be announced through cameraDeviceStatusChange
    // once its HIDL Device/Session implementation is ready.
    hidl_vec<hidl_string> cameraIds;
    callback(Status::OK, cameraIds);
    return Void();
}

Return<void> VcamProvider::isSetTorchModeSupported(
        isSetTorchModeSupported_cb callback) {
    callback(Status::OK, false);
    return Void();
}

Return<void> VcamProvider::getCameraDeviceInterface_V1_x(
        const hidl_string&, getCameraDeviceInterface_V1_x_cb callback) {
    callback(Status::OPERATION_NOT_SUPPORTED, nullptr);
    return Void();
}

Return<void> VcamProvider::getCameraDeviceInterface_V3_x(
        const hidl_string&, getCameraDeviceInterface_V3_x_cb callback) {
    callback(Status::ILLEGAL_ARGUMENT, nullptr);
    return Void();
}

}  // namespace android::hardware::camera::provider::V2_4::implementation
