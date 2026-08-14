/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#define LOG_TAG "VcamProviderHidlService"

#include "VcamProvider.h"

#include <android-base/logging.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>

using android::OK;
using android::ProcessState;
using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::camera::provider::V2_4::implementation::VcamProvider;

int main() {
    ProcessState::initWithDriver("/dev/vndbinder");
    configureRpcThreadpool(6, true);
    sp<VcamProvider> provider = new VcamProvider();
    if (provider->registerAsService("vcam/0") != OK) {
        LOG(ERROR) << "Unable to register VCAM provider instance vcam/0";
        return 1;
    }
    LOG(INFO) << "VCAM HIDL provider registered as vcam/0";
    joinRpcThreadpool();
    return 1;
}
