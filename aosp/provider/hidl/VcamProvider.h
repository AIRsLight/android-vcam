/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <android/hardware/camera/provider/2.4/ICameraProvider.h>
#include <CameraModule.h>

#include <mutex>
#include <string>
#include <utility>

#include <utils/SortedVector.h>

namespace android::hardware::camera::provider::V2_4::implementation {

class VcamProvider final : public ICameraProvider {
  public:
    VcamProvider();
    ~VcamProvider() override;

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
    static constexpr const char* kBackDeviceName = "device@3.4/vcam/1000";
    static constexpr const char* kFrontDeviceName = "device@3.4/vcam/1001";

    std::mutex mutex_;
    sp<ICameraProviderCallback> callback_;
    sp<common::V1_0::helper::CameraModule> module_;
    void* moduleHandle_ = nullptr;
    SortedVector<std::pair<std::string, std::string>> cameraDeviceNames_;
    bool ready_ = false;
    bool enabled_ = false;
};

}  // namespace android::hardware::camera::provider::V2_4::implementation
