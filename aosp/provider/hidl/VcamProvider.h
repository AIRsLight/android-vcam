/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <android/hardware/camera/provider/2.4/ICameraProvider.h>

#include <mutex>

namespace android::hardware::camera::provider::V2_4::implementation {

class VcamProvider final : public ICameraProvider {
  public:
    Return<common::V1_0::Status> setCallback(
            const sp<ICameraProviderCallback>& callback) override;
    Return<void> getVendorTags(getVendorTags_cb callback) override;
    Return<void> getCameraIdList(getCameraIdList_cb callback) override;
    Return<void> isSetTorchModeSupported(
            isSetTorchModeSupported_cb callback) override;
    Return<void> getCameraDeviceInterface_V1_x(
            const hidl_string& cameraDeviceName,
            getCameraDeviceInterface_V1_x_cb callback) override;
    Return<void> getCameraDeviceInterface_V3_x(
            const hidl_string& cameraDeviceName,
            getCameraDeviceInterface_V3_x_cb callback) override;

  private:
    std::mutex mutex_;
    sp<ICameraProviderCallback> callback_;
};

}  // namespace android::hardware::camera::provider::V2_4::implementation
