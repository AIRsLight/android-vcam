/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#define LOG_TAG "VcamProviderHidlService"

#include "VcamProvider.h"

#include <android-base/logging.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>
#include <hidl/ServiceManagement.h>

#include <cstdlib>
#include <cstdio>

using android::OK;
using android::ProcessState;
using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::camera::provider::V2_4::implementation::VcamProvider;

int main() {
    const char* allowUndeclared = std::getenv("ANDROID_VCAM_HIDL_ALLOW_UNDECLARED");
    if (allowUndeclared != nullptr && allowUndeclared[0] == '1') {
        // Diagnostic escape hatch for user-build bring-up only. Production
        // deployments must declare the instance in the device VINTF manifest.
        android::hardware::details::setTrebleTestingOverride(true);
    }
    ProcessState::initWithDriver("/dev/vndbinder");
    configureRpcThreadpool(6, true);
    sp<VcamProvider> provider = new VcamProvider();
    const android::status_t registration = provider->registerAsService("vcam/0");
    if (registration != OK) {
        std::fprintf(stderr,
                     "Unable to register VCAM HIDL provider vcam/0: status=%d\n",
                     registration);
        LOG(ERROR) << "Unable to register VCAM provider instance vcam/0: "
                   << registration;
        return 1;
    }
    LOG(INFO) << "VCAM HIDL provider registered as vcam/0";
    joinRpcThreadpool();
    return 1;
}
