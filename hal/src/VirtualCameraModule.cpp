/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#define LOG_TAG "VirtualCameraModule"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <memory>

#include <hardware/camera_common.h>
#include <hardware/hardware.h>

#include "vcam/VirtualCamera.h"
#include "vcam/Log.h"
#include "vcam/OriginalHal.h"
#include "vcam/VendorTags.h"

namespace vcam {
namespace {

class VirtualCameraModule final {
  public:
    VirtualCameraModule() {
        cameras_[0] = std::make_unique<VirtualCamera>(0);
        cameras_[1] = std::make_unique<VirtualCamera>(1);
    }

    int getCameraInfo(int id, camera_info* info) {
        if (id < 0 || id >= static_cast<int>(cameras_.size())) return -EINVAL;
        return cameras_[id]->getInfo(info);
    }

    int open(const hw_module_t* module, const char* name, hw_device_t** device) {
        if (name == nullptr || *name == '\0') return -EINVAL;
        char* end = nullptr;
        const long parsed = strtol(name, &end, 10);
        if (end == nullptr || *end != '\0' || parsed < 0 ||
            parsed >= static_cast<long>(cameras_.size())) {
            return -EINVAL;
        }
        return cameras_[parsed]->open(module, device);
    }

    int setCallbacks(const camera_module_callbacks_t* callbacks) {
        if (callbacks == nullptr) return -EINVAL;
        return 0;
    }

  private:
    std::array<std::unique_ptr<VirtualCamera>, 2> cameras_;
};

VirtualCameraModule gModule;

int getNumberOfCameras() {
    return 2;
}

int getCameraInfo(int id, camera_info* info) {
    return gModule.getCameraInfo(id, info);
}

int setCallbacks(const camera_module_callbacks_t* callbacks) {
    return gModule.setCallbacks(callbacks);
}

int getVendorTagCount(const vendor_tag_ops_t*) {
    return 1;
}

void getAllVendorTags(const vendor_tag_ops_t*, uint32_t* tags) {
    if (tags != nullptr) tags[0] = kOplusPackageNameTag;
}

const char* getVendorSectionName(const vendor_tag_ops_t*, uint32_t tag) {
    return tag == kOplusPackageNameTag ? kOplusPackageNameSection : nullptr;
}

const char* getVendorTagName(const vendor_tag_ops_t*, uint32_t tag) {
    return tag == kOplusPackageNameTag ? kOplusPackageNameName : nullptr;
}

int getVendorTagType(const vendor_tag_ops_t*, uint32_t tag) {
    return tag == kOplusPackageNameTag ? TYPE_BYTE : -1;
}

void getVendorTagOps(vendor_tag_ops_t* ops) {
    if (ops == nullptr) return;
    memset(ops, 0, sizeof(*ops));
    ops->get_tag_count = getVendorTagCount;
    ops->get_all_tags = getAllVendorTags;
    ops->get_section_name = getVendorSectionName;
    ops->get_tag_name = getVendorTagName;
    ops->get_tag_type = getVendorTagType;
}

int openDevice(const hw_module_t* module, const char* name, hw_device_t** device) {
    return gModule.open(module, name, device);
}

int openLegacy(const hw_module_t*, const char*, uint32_t, hw_device_t**) {
    return -ENOSYS;
}

int setTorchMode(const char*, bool) {
    return -ENOSYS;
}

int initializeModule() {
    if (!OriginalHal::instance().load()) {
        ALOGE("android-vcam initialized without physical fallback: %s",
              OriginalHal::instance().error());
        return 0;
    }
    ALOGI("android-vcam camera module initialized with physical fallback");
    return 0;
}

hw_module_methods_t kModuleMethods = {
        .open = openDevice,
};

}  // namespace
}  // namespace vcam

extern "C" {
camera_module_t HAL_MODULE_INFO_SYM __attribute__((visibility("default"))) = {
                .common = {
                        .tag = HARDWARE_MODULE_TAG,
                        .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
                        .hal_api_version = HARDWARE_HAL_API_VERSION,
                        .id = CAMERA_HARDWARE_MODULE_ID,
                        .name = "android-vcam virtual Camera HAL",
                        .author = "android-vcam contributors",
                        .methods = &vcam::kModuleMethods,
                        .dso = nullptr,
                        .reserved = {0},
                },
                .get_number_of_cameras = vcam::getNumberOfCameras,
                .get_camera_info = vcam::getCameraInfo,
                .set_callbacks = vcam::setCallbacks,
                .get_vendor_tag_ops = vcam::getVendorTagOps,
                .open_legacy = vcam::openLegacy,
                .set_torch_mode = vcam::setTorchMode,
                .init = vcam::initializeModule,
                .get_physical_camera_info = nullptr,
                .is_stream_combination_supported = nullptr,
                .notify_device_state_change = nullptr,
                .reserved = {nullptr},
};
}  // extern "C"
