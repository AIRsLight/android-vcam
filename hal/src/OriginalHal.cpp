/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#define LOG_TAG "VirtualCameraOriginalHal"

#include "vcam/OriginalHal.h"

#include <android/dlext.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include "vcam/Log.h"

namespace vcam {
namespace {

constexpr const char* kOriginalHalPath =
        "/vendor/lib64/hw/local_time.default.so";
constexpr const char* kOriginalHalLoadName =
        "/vendor/lib64/hw/camera.qcom.so";

}  // namespace

OriginalHal& OriginalHal::instance() {
    static OriginalHal original;
    return original;
}

OriginalHal::~OriginalHal() {
    if (handle_ != nullptr) dlclose(handle_);
}

bool OriginalHal::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (module_ != nullptr) return true;

    const int fd = open(kOriginalHalPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        error_ = "unable to open original HAL snapshot";
        ALOGE("%s: %s", error_, kOriginalHalPath);
        return false;
    }

    android_dlextinfo extInfo{};
    extInfo.flags = ANDROID_DLEXT_USE_LIBRARY_FD | ANDROID_DLEXT_FORCE_LOAD;
    extInfo.library_fd = fd;
    dlerror();
    // The FD points at a vendor-visible bind mount, while the logical loader
    // name remains the real camera HAL path. Some Qualcomm camera components
    // derive their initialization context from that name.
    handle_ = android_dlopen_ext(kOriginalHalLoadName,
                                RTLD_NOW | RTLD_LOCAL,
                                &extInfo);
    const char* loaderError = dlerror();
    close(fd);
    if (handle_ == nullptr) {
        error_ = loaderError != nullptr ? loaderError : "android_dlopen_ext failed";
        ALOGE("Unable to load original HAL: %s", error_);
        return false;
    }

    module_ = reinterpret_cast<camera_module_t*>(dlsym(handle_, HAL_MODULE_INFO_SYM_AS_STR));
    const char* symbolError = dlerror();
    if (module_ == nullptr) {
        error_ = symbolError != nullptr ? symbolError : "HMI symbol missing";
        ALOGE("Unable to resolve original HAL module: %s", error_);
        dlclose(handle_);
        handle_ = nullptr;
        return false;
    }

    if (module_->common.tag != HARDWARE_MODULE_TAG ||
            module_->get_number_of_cameras == nullptr ||
            module_->get_camera_info == nullptr || module_->common.methods == nullptr ||
            module_->common.methods->open == nullptr) {
        error_ = "original HAL module has an invalid ABI";
        ALOGE("%s", error_);
        module_ = nullptr;
        dlclose(handle_);
        handle_ = nullptr;
        return false;
    }

    if (module_->init != nullptr) {
        const int initResult = module_->init();
        if (initResult != 0) {
            error_ = "original HAL initialization failed";
            ALOGE("%s: %d", error_, initResult);
            module_ = nullptr;
            dlclose(handle_);
            handle_ = nullptr;
            return false;
        }
    }

    error_ = nullptr;
    ALOGI("Loaded original Camera HAL snapshot with %d cameras",
          module_->get_number_of_cameras());
    return true;
}

camera_module_t* OriginalHal::module() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return module_;
}

const char* OriginalHal::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

}  // namespace vcam
