/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#define LOG_TAG "VirtualCameraProxy"

#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <hardware/camera3.h>
#include <hardware/camera_common.h>
#include <hardware/hardware.h>
#include <stdint.h>
#include <string.h>
#include <system/camera_metadata.h>

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <string>

#include "vcam/VirtualCamera.h"
#include "vcam/RouteResolver.h"
#include "vcam/VendorTags.h"

namespace {

constexpr size_t kMaxCameras = 16;

enum class Backend { kUnconfigured, kPhysical, kVirtual };

struct ProxyDevice {
    int cameraId = -1;
    camera3_device_t facade{};
    hw_device_t* physicalRaw = nullptr;
    camera3_device_t* physical = nullptr;
    int physicalSourceId = -1;
    const camera3_callback_ops_t* callbacks = nullptr;
    std::unique_ptr<vcam::VirtualCamera> virtualCamera;
    camera3_device_t* virtualDevice = nullptr;
    Backend backend = Backend::kUnconfigured;
    std::string packageName;
    std::mutex mutex;
};

camera_module_t* gModule = nullptr;
hw_module_methods_t gProxyModuleMethods{};
hw_module_methods_t gOriginalModuleMethods{};
void (*gOriginalGetVendorTagOps)(vendor_tag_ops_t*) = nullptr;
vendor_tag_ops_t gOriginalVendorOps{};
bool gOriginalVendorOpsReady = false;
std::array<std::unique_ptr<ProxyDevice>, kMaxCameras> gDevices;
std::array<camera_metadata_t*, kMaxCameras> gAugmentedMetadata{};
std::mutex gModuleMutex;

void ensureOriginalVendorOps() {
    std::lock_guard<std::mutex> lock(gModuleMutex);
    if (gOriginalVendorOpsReady) return;
    memset(&gOriginalVendorOps, 0, sizeof(gOriginalVendorOps));
    if (gOriginalGetVendorTagOps != nullptr) {
        gOriginalGetVendorTagOps(&gOriginalVendorOps);
    }
    gOriginalVendorOpsReady = true;
}

ProxyDevice* findDevice(const camera3_device_t* device) {
    std::lock_guard<std::mutex> lock(gModuleMutex);
    for (const auto& candidate : gDevices) {
        if (candidate != nullptr && &candidate->facade == device) {
            return candidate.get();
        }
    }
    return nullptr;
}

std::string packageFrom(const camera_metadata_t* metadata) {
    if (metadata == nullptr) return {};
    camera_metadata_ro_entry_t entry{};
    if (find_camera_metadata_ro_entry(
                metadata, vcam::kOplusPackageNameTag, &entry) != 0 ||
        entry.count == 0 || entry.data.u8 == nullptr) {
        return {};
    }
    size_t length = 0;
    while (length < entry.count && entry.data.u8[length] != 0) ++length;
    return std::string(reinterpret_cast<const char*>(entry.data.u8), length);
}

bool appendKey(camera_metadata_t* metadata, uint32_t keyTag) {
    camera_metadata_entry_t entry{};
    if (find_camera_metadata_entry(metadata, keyTag, &entry) != 0) {
        const int32_t tag = static_cast<int32_t>(vcam::kOplusPackageNameTag);
        return add_camera_metadata_entry(metadata, keyTag, &tag, 1) == 0;
    }
    for (size_t i = 0; i < entry.count; ++i) {
        if (static_cast<uint32_t>(entry.data.i32[i]) ==
            vcam::kOplusPackageNameTag) {
            return true;
        }
    }
    std::unique_ptr<int32_t[]> values(new int32_t[entry.count + 1]);
    memcpy(values.get(), entry.data.i32, entry.count * sizeof(int32_t));
    values[entry.count] = static_cast<int32_t>(vcam::kOplusPackageNameTag);
    return update_camera_metadata_entry(metadata, entry.index, values.get(),
                                        entry.count + 1, nullptr) == 0;
}

camera_metadata_t* augmentMetadata(const camera_metadata_t* source) {
    if (source == nullptr) return nullptr;
    const size_t entries = get_camera_metadata_entry_count(source);
    const size_t data = get_camera_metadata_data_count(source);
    camera_metadata_t* copy = allocate_camera_metadata(entries + 4, data + 64);
    if (copy == nullptr || append_camera_metadata(copy, source) != 0 ||
        !appendKey(copy, ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS) ||
        !appendKey(copy, ANDROID_REQUEST_AVAILABLE_SESSION_KEYS) ||
        sort_camera_metadata(copy) != 0) {
        if (copy != nullptr) free_camera_metadata(copy);
        return nullptr;
    }
    return copy;
}

int proxyVendorTagCount(const vendor_tag_ops_t*) {
    ensureOriginalVendorOps();
    const int original = gOriginalVendorOps.get_tag_count == nullptr
            ? 0 : gOriginalVendorOps.get_tag_count(&gOriginalVendorOps);
    return original + 1;
}

void proxyAllVendorTags(const vendor_tag_ops_t*, uint32_t* tags) {
    if (tags == nullptr) return;
    ensureOriginalVendorOps();
    int count = 0;
    if (gOriginalVendorOps.get_tag_count != nullptr) {
        count = gOriginalVendorOps.get_tag_count(&gOriginalVendorOps);
    }
    if (count > 0 && gOriginalVendorOps.get_all_tags != nullptr) {
        gOriginalVendorOps.get_all_tags(&gOriginalVendorOps, tags);
    }
    tags[count] = vcam::kOplusPackageNameTag;
}

const char* proxyVendorSection(const vendor_tag_ops_t*, uint32_t tag) {
    if (tag == vcam::kOplusPackageNameTag) return vcam::kOplusPackageNameSection;
    ensureOriginalVendorOps();
    return gOriginalVendorOps.get_section_name == nullptr ? nullptr
            : gOriginalVendorOps.get_section_name(&gOriginalVendorOps, tag);
}

const char* proxyVendorName(const vendor_tag_ops_t*, uint32_t tag) {
    if (tag == vcam::kOplusPackageNameTag) return vcam::kOplusPackageNameName;
    ensureOriginalVendorOps();
    return gOriginalVendorOps.get_tag_name == nullptr ? nullptr
            : gOriginalVendorOps.get_tag_name(&gOriginalVendorOps, tag);
}

int proxyVendorType(const vendor_tag_ops_t*, uint32_t tag) {
    if (tag == vcam::kOplusPackageNameTag) return TYPE_BYTE;
    ensureOriginalVendorOps();
    return gOriginalVendorOps.get_tag_type == nullptr ? -1
            : gOriginalVendorOps.get_tag_type(&gOriginalVendorOps, tag);
}

void proxyGetVendorTagOps(vendor_tag_ops_t* ops) {
    if (ops == nullptr) return;
    memset(ops, 0, sizeof(*ops));
    ops->get_tag_count = proxyVendorTagCount;
    ops->get_all_tags = proxyAllVendorTags;
    ops->get_section_name = proxyVendorSection;
    ops->get_tag_name = proxyVendorName;
    ops->get_tag_type = proxyVendorType;
}

int (*gOriginalGetCameraInfo)(int, camera_info*) = nullptr;

int augmentedGetCameraInfo(int id, camera_info* info) {
    if (gOriginalGetCameraInfo == nullptr || info == nullptr) return -EINVAL;
    const int result = gOriginalGetCameraInfo(id, info);
    if (result != 0 || id < 0 || id >= static_cast<int>(kMaxCameras) ||
        info->static_camera_characteristics == nullptr) {
        return result;
    }
    std::lock_guard<std::mutex> lock(gModuleMutex);
    if (gAugmentedMetadata[id] == nullptr) {
        gAugmentedMetadata[id] = augmentMetadata(info->static_camera_characteristics);
        if (gAugmentedMetadata[id] == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                                "failed to augment static metadata for camera %d", id);
            return -ENODEV;
        }
    }
    info->static_camera_characteristics = gAugmentedMetadata[id];
    if (info->device_version < CAMERA_DEVICE_API_VERSION_3_5) {
        info->device_version = CAMERA_DEVICE_API_VERSION_3_5;
    }
    return 0;
}

int proxyClose(hw_device_t* rawDevice);
int proxyInitialize(const camera3_device_t*, const camera3_callback_ops_t*);
int proxyConfigure(const camera3_device_t*, camera3_stream_configuration_t*);
const camera_metadata_t* proxyDefaultRequest(const camera3_device_t*, int);
int proxyProcessRequest(const camera3_device_t*, camera3_capture_request_t*);
void proxyDump(const camera3_device_t*, int);
int proxyFlush(const camera3_device_t*);
void proxySignalFlush(const camera3_device_t*, uint32_t,
                      const camera3_stream_t* const*);
int proxyReconfigurationRequired(const camera3_device_t*,
                                 const camera_metadata_t*,
                                 const camera_metadata_t*);

camera3_device_ops_t gProxyDeviceOps = {
        .initialize = proxyInitialize,
        .configure_streams = proxyConfigure,
        .register_stream_buffers = nullptr,
        .construct_default_request_settings = proxyDefaultRequest,
        .process_capture_request = proxyProcessRequest,
        .get_metadata_vendor_tag_ops = nullptr,
        .dump = proxyDump,
        .flush = proxyFlush,
        .signal_stream_flush = proxySignalFlush,
        .is_reconfiguration_required = proxyReconfigurationRequired,
        .reserved = {nullptr},
};

int proxyOpen(const hw_module_t* module, const char* id, hw_device_t** out) {
    if (id == nullptr || out == nullptr || gOriginalModuleMethods.open == nullptr) {
        return -EINVAL;
    }
    hw_device_t* physicalRaw = nullptr;
    const int result = gOriginalModuleMethods.open(module, id, &physicalRaw);
    if (result != 0) return result;
    if (physicalRaw == nullptr || physicalRaw->version < CAMERA_DEVICE_API_VERSION_3_2) {
        if (physicalRaw != nullptr && physicalRaw->close != nullptr) physicalRaw->close(physicalRaw);
        return -ENODEV;
    }

    char* end = nullptr;
    const long parsed = strtol(id, &end, 10);
    if (end == nullptr || *end != '\0' || parsed < 0 ||
        parsed >= static_cast<long>(kMaxCameras)) {
        physicalRaw->close(physicalRaw);
        return -EINVAL;
    }

    auto state = std::make_unique<ProxyDevice>();
    state->cameraId = static_cast<int>(parsed);
    state->physicalRaw = physicalRaw;
    state->physical = reinterpret_cast<camera3_device_t*>(physicalRaw);
    state->physicalSourceId = state->cameraId;
    // The OEM static characteristics advertise two partial results. Keep the
    // proxy result index aligned with those physical-camera characteristics;
    // the standalone AOSP module uses VirtualCamera's default of one.
    state->virtualCamera = std::make_unique<vcam::VirtualCamera>(
            state->cameraId, 2);
    hw_device_t* virtualRaw = nullptr;
    const int virtualResult = state->virtualCamera->open(module, &virtualRaw);
    if (virtualResult != 0 || virtualRaw == nullptr) {
        physicalRaw->close(physicalRaw);
        return virtualResult == 0 ? -ENODEV : virtualResult;
    }
    state->virtualDevice = reinterpret_cast<camera3_device_t*>(virtualRaw);
    state->facade.common.tag = HARDWARE_DEVICE_TAG;
    state->facade.common.version = std::max<uint32_t>(
            physicalRaw->version, CAMERA_DEVICE_API_VERSION_3_5);
    state->facade.common.module = const_cast<hw_module_t*>(module);
    state->facade.common.close = proxyClose;
    state->facade.ops = &gProxyDeviceOps;
    state->facade.priv = state.get();
    hw_device_t* facadeRaw = &state->facade.common;
    {
        std::lock_guard<std::mutex> lock(gModuleMutex);
        if (gDevices[state->cameraId] != nullptr) {
            state->virtualDevice->common.close(&state->virtualDevice->common);
            physicalRaw->close(physicalRaw);
            return -EBUSY;
        }
        gDevices[state->cameraId] = std::move(state);
    }
    *out = facadeRaw;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "opened hybrid camera %ld", parsed);
    return 0;
}

int proxyClose(hw_device_t* rawDevice) {
    auto* device = reinterpret_cast<camera3_device_t*>(rawDevice);
    ProxyDevice* state = findDevice(device);
    if (state == nullptr) return -EINVAL;
    const int id = state->cameraId;
    int virtualResult = 0;
    if (state->virtualDevice != nullptr && state->virtualDevice->common.close != nullptr) {
        virtualResult = state->virtualDevice->common.close(&state->virtualDevice->common);
    }
    int physicalResult = 0;
    if (state->physicalRaw != nullptr && state->physicalRaw->close != nullptr) {
        physicalResult = state->physicalRaw->close(state->physicalRaw);
        state->physicalRaw = nullptr;
        state->physical = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(gModuleMutex);
        gDevices[id].release();
    }
    {
        std::lock_guard<std::mutex> lock(gModuleMutex);
        delete state;
    }
    return physicalResult != 0 ? physicalResult : virtualResult;
}

int proxyInitialize(const camera3_device_t* device,
                    const camera3_callback_ops_t* callbacks) {
    ProxyDevice* state = findDevice(device);
    if (state == nullptr || callbacks == nullptr || state->physical == nullptr ||
        state->physical->ops == nullptr || state->physical->ops->initialize == nullptr) {
        return -EINVAL;
    }
    state->callbacks = callbacks;
    const int physical = state->physical->ops->initialize(state->physical, callbacks);
    if (physical != 0) return physical;
    return state->virtualDevice->ops->initialize(state->virtualDevice, callbacks);
}

int selectPhysicalSource(ProxyDevice* state, int sourceId) {
    if (state == nullptr || sourceId < 0 ||
        sourceId >= static_cast<int>(kMaxCameras)) return -EINVAL;
    if (state->physical != nullptr && state->physicalSourceId == sourceId) return 0;

    if (state->physicalRaw != nullptr && state->physicalRaw->close != nullptr) {
        state->physicalRaw->close(state->physicalRaw);
    }
    state->physicalRaw = nullptr;
    state->physical = nullptr;
    state->physicalSourceId = -1;

    const std::string id = std::to_string(sourceId);
    hw_device_t* replacement = nullptr;
    const int openResult = gOriginalModuleMethods.open(
            &gModule->common, id.c_str(), &replacement);
    if (openResult != 0 || replacement == nullptr) return openResult;
    auto* camera = reinterpret_cast<camera3_device_t*>(replacement);
    if (camera->ops == nullptr || camera->ops->initialize == nullptr) {
        replacement->close(replacement);
        return -ENODEV;
    }
    const int initializeResult = camera->ops->initialize(camera, state->callbacks);
    if (initializeResult != 0) {
        replacement->close(replacement);
        return initializeResult;
    }
    state->physicalRaw = replacement;
    state->physical = camera;
    state->physicalSourceId = sourceId;
    return 0;
}

int proxyConfigure(const camera3_device_t* device,
                   camera3_stream_configuration_t* config) {
    ProxyDevice* state = findDevice(device);
    if (state == nullptr || config == nullptr) return -EINVAL;
    const std::string packageName = packageFrom(config->session_parameters);
    const std::string provider = vcam::RouteResolver::providerForPackage(
            packageName, state->cameraId);
    const int physicalSource =
            vcam::RouteResolver::physicalIdFromProvider(provider);
    const bool useVirtual = physicalSource < 0;
    if (useVirtual) {
        state->virtualCamera->setSourcePath(
                vcam::RouteResolver::framePath(provider));
    }
    int result = 0;
    if (useVirtual) {
        result = state->virtualDevice->ops->configure_streams(
                state->virtualDevice, config);
    } else {
        result = selectPhysicalSource(state, physicalSource);
        if (result == 0) {
            result = state->physical->ops->configure_streams(state->physical, config);
        }
    }
    if (result == 0) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->backend = useVirtual ? Backend::kVirtual : Backend::kPhysical;
        state->packageName = packageName;
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                            "camera %d package='%s' backend=%s provider='%s'",
                            state->cameraId, packageName.c_str(),
                            useVirtual ? "virtual" : "physical",
                            provider.c_str());
    }
    return result;
}

const camera_metadata_t* proxyDefaultRequest(const camera3_device_t* device, int type) {
    ProxyDevice* state = findDevice(device);
    if (state == nullptr) return nullptr;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->backend == Backend::kVirtual) {
        return state->virtualDevice->ops->construct_default_request_settings(
                state->virtualDevice, type);
    }
    return state->physical == nullptr ? nullptr
            : state->physical->ops->construct_default_request_settings(
                    state->physical, type);
}

int proxyProcessRequest(const camera3_device_t* device,
                        camera3_capture_request_t* request) {
    ProxyDevice* state = findDevice(device);
    if (state == nullptr) return -EINVAL;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->backend == Backend::kVirtual
            ? state->virtualDevice->ops->process_capture_request(
                    state->virtualDevice, request)
            : state->physical->ops->process_capture_request(
                    state->physical, request);
}

void proxyDump(const camera3_device_t* device, int fd) {
    ProxyDevice* state = findDevice(device);
    if (state == nullptr) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->backend == Backend::kVirtual) {
        state->virtualDevice->ops->dump(state->virtualDevice, fd);
    } else if (state->physical != nullptr && state->physical->ops->dump != nullptr) {
        state->physical->ops->dump(state->physical, fd);
    }
}

int proxyFlush(const camera3_device_t* device) {
    ProxyDevice* state = findDevice(device);
    if (state == nullptr) return -EINVAL;
    std::lock_guard<std::mutex> lock(state->mutex);
    const camera3_device_t* selected = state->backend == Backend::kVirtual
            ? state->virtualDevice : state->physical;
    const camera3_device_ops_t* ops = state->backend == Backend::kVirtual
            ? state->virtualDevice->ops
            : (state->physical == nullptr ? nullptr : state->physical->ops);
    return ops == nullptr || ops->flush == nullptr ? 0 : ops->flush(selected);
}

void proxySignalFlush(const camera3_device_t* device, uint32_t count,
                      const camera3_stream_t* const* streams) {
    ProxyDevice* state = findDevice(device);
    if (state == nullptr) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    const camera3_device_t* selected = state->backend == Backend::kVirtual
            ? state->virtualDevice : state->physical;
    const camera3_device_ops_t* ops = state->backend == Backend::kVirtual
            ? state->virtualDevice->ops
            : (state->physical == nullptr ? nullptr : state->physical->ops);
    if (ops != nullptr && ops->signal_stream_flush != nullptr) {
        ops->signal_stream_flush(selected, count, streams);
    }
}

int proxyReconfigurationRequired(const camera3_device_t* device,
                                 const camera_metadata_t* oldParams,
                                 const camera_metadata_t* newParams) {
    ProxyDevice* state = findDevice(device);
    if (state == nullptr) return -EINVAL;
    std::lock_guard<std::mutex> lock(state->mutex);
    const camera3_device_t* selected = state->backend == Backend::kVirtual
            ? state->virtualDevice : state->physical;
    const camera3_device_ops_t* ops = state->backend == Backend::kVirtual
            ? state->virtualDevice->ops
            : (state->physical == nullptr ? nullptr : state->physical->ops);
    return ops == nullptr || ops->is_reconfiguration_required == nullptr ? 0
            : ops->is_reconfiguration_required(selected, oldParams, newParams);
}

__attribute__((constructor)) void bootstrapProxy() {
    dlerror();
    auto* module = reinterpret_cast<camera_module_t*>(
            dlsym(RTLD_DEFAULT, HAL_MODULE_INFO_SYM_AS_STR));
    const char* error = dlerror();
    if (module == nullptr || module->common.tag != HARDWARE_MODULE_TAG) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "unable to resolve OEM HMI: %s",
                            error == nullptr ? "invalid module" : error);
        return;
    }

    gModule = module;
    gOriginalGetCameraInfo = module->get_camera_info;
    gOriginalGetVendorTagOps = module->get_vendor_tag_ops;
    if (module->common.methods == nullptr || module->common.methods->open == nullptr ||
        gOriginalGetCameraInfo == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "OEM HMI is incomplete");
        return;
    }
    gOriginalModuleMethods = *module->common.methods;
    gProxyModuleMethods = gOriginalModuleMethods;
    gProxyModuleMethods.open = proxyOpen;
    module->common.methods = &gProxyModuleMethods;
    module->get_camera_info = augmentedGetCameraInfo;
    module->get_vendor_tag_ops = proxyGetVendorTagOps;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "installed OEM Camera HAL proxy tag=0x%08x",
                        vcam::kOplusPackageNameTag);
}

}  // namespace
