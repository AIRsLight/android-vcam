/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#define LOG_TAG "VirtualCameraHAL"

#include "vcam/VirtualCamera.h"

#include <errno.h>
#include <inttypes.h>
#include <jpeglib.h>
#include <poll.h>
#include <setjmp.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include <hardware/hardware.h>
#include <system/graphics.h>

#include "vcam/MetadataBuilder.h"
#include "vcam/Log.h"
#include "vcam/PatternGenerator.h"
#include "vcam/VendorTags.h"

namespace vcam {
namespace {

constexpr int kFenceTimeoutMs = 5000;
constexpr int32_t kJpegMaxSize = 16 * 1024 * 1024;
constexpr int64_t kFrameDurationNs = 33333333;
constexpr uint32_t kMaxStreamDimension = 16384;

struct JpegErrorManager {
    jpeg_error_mgr base;
    jmp_buf jump;
    unsigned char* output = nullptr;
};

void jpegErrorExit(j_common_ptr compressor) {
    auto* error = reinterpret_cast<JpegErrorManager*>(compressor->err);
    longjmp(error->jump, 1);
}

bool supportedSize(int format, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 ||
        width > kMaxStreamDimension || height > kMaxStreamDimension) {
        return false;
    }

    // CameraService validates requested sizes against the original physical
    // camera metadata exposed by the proxy. Do not repeat that validation with
    // a short virtual-only resolution list here: Camera1 and OEM clients often
    // select device-specific preview sizes such as 2080x960. YUV 4:2:0 output
    // still requires even dimensions, while JPEG may use odd dimensions.
    return format == HAL_PIXEL_FORMAT_BLOB ||
           ((width & 1U) == 0 && (height & 1U) == 0);
}

uint64_t bootTimeNs() {
    timespec ts{};
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

void sleepForNs(uint64_t nanoseconds) {
    timespec remaining{
            static_cast<time_t>(nanoseconds / 1000000000ULL),
            static_cast<long>(nanoseconds % 1000000000ULL),
    };
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

void logClientPackage(const camera_metadata_t* metadata, const char* source) {
    camera_metadata_ro_entry_t entry{};
    if (metadata == nullptr || find_camera_metadata_ro_entry(
            metadata, kOplusPackageNameTag, &entry) != 0) {
        const size_t count = metadata == nullptr ? 0 : get_camera_metadata_entry_count(metadata);
        ALOGI("Client package tag absent in %s (entries=%zu)", source, count);
        for (size_t i = 0; i < count; ++i) {
            camera_metadata_ro_entry_t candidate{};
            if (get_camera_metadata_ro_entry(metadata, i, &candidate) == 0 &&
                    candidate.tag >= VENDOR_SECTION_START) {
                ALOGI("Vendor metadata in %s: tag=0x%08x type=%u count=%zu",
                      source, candidate.tag, candidate.type, candidate.count);
            }
        }
        return;
    }

    char value[256]{};
    const size_t length = std::min(entry.count, sizeof(value) - 1);
    for (size_t i = 0; i < length; ++i) {
        const uint8_t byte = entry.data.u8[i];
        value[i] = byte >= 0x20 && byte <= 0x7e ? static_cast<char>(byte) : '.';
    }
    ALOGI("Client package from %s tag=0x%08x type=%u count=%zu value='%s'",
          source, entry.tag, entry.type, entry.count, value);
}

int waitForFence(int fd, int timeoutMs) {
    pollfd fence{fd, POLLIN, 0};
    int result;
    do {
        result = poll(&fence, 1, timeoutMs);
    } while (result < 0 && errno == EINTR);

    if (result == 0) return -ETIMEDOUT;
    if (result < 0) return -errno;
    return (fence.revents & (POLLIN | POLLERR | POLLHUP)) != 0 ? 0 : -EIO;
}

bool addStaticMetadata(MetadataBuilder* metadata, int cameraId) {
    const uint8_t aberrationModes[] = {ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF};
    const uint8_t aeAntibanding[] = {ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF,
                                     ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO};
    const uint8_t aeModes[] = {ANDROID_CONTROL_AE_MODE_ON};
    const int32_t fpsRanges[] = {15, 30, 30, 30};
    const int32_t compensationRange[] = {0, 0};
    const camera_metadata_rational_t compensationStep[] = {{0, 1}};
    const uint8_t afModes[] = {ANDROID_CONTROL_AF_MODE_OFF};
    const uint8_t effects[] = {ANDROID_CONTROL_EFFECT_MODE_OFF};
    const uint8_t sceneModes[] = {ANDROID_CONTROL_SCENE_MODE_DISABLED};
    const uint8_t stabilizationModes[] = {ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF};
    const uint8_t awbModes[] = {ANDROID_CONTROL_AWB_MODE_AUTO};
    const int32_t maxRegions[] = {0, 0, 0};
    const uint8_t falseValue[] = {0};
    const uint8_t controlModes[] = {ANDROID_CONTROL_MODE_OFF, ANDROID_CONTROL_MODE_AUTO};
    const uint8_t edgeModes[] = {ANDROID_EDGE_MODE_OFF};
    const uint8_t flashAvailable[] = {0};
    const uint8_t hotPixelModes[] = {ANDROID_HOT_PIXEL_MODE_OFF};
    const int32_t thumbnailSizes[] = {0, 0, 320, 240};
    const int32_t jpegMaxSize[] = {kJpegMaxSize};
    const float apertures[] = {2.0f};
    const float filterDensities[] = {0.0f};
    const float focalLengths[] = {4.0f};
    const uint8_t opticalStabilization[] = {ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF};
    const float minimumFocusDistance[] = {0.0f};
    const uint8_t focusCalibration[] = {ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED};
    const uint8_t lensFacing[] = {static_cast<uint8_t>(
            cameraId == 0 ? ANDROID_LENS_FACING_BACK : ANDROID_LENS_FACING_FRONT)};
    const uint8_t noiseModes[] = {ANDROID_NOISE_REDUCTION_MODE_OFF};
    const int32_t maxOutputStreams[] = {0, 3, 1};
    const uint8_t pipelineDepth[] = {4};
    const int32_t partialResultCount[] = {1};
    const uint8_t capabilities[] = {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE};

    const int32_t requestKeys[] = {
            ANDROID_CONTROL_AE_ANTIBANDING_MODE,
            ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
            ANDROID_CONTROL_AE_LOCK,
            ANDROID_CONTROL_AE_MODE,
            ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
            ANDROID_CONTROL_AF_MODE,
            ANDROID_CONTROL_AWB_LOCK,
            ANDROID_CONTROL_AWB_MODE,
            ANDROID_CONTROL_CAPTURE_INTENT,
            ANDROID_CONTROL_EFFECT_MODE,
            ANDROID_CONTROL_MODE,
            ANDROID_CONTROL_SCENE_MODE,
            ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
            ANDROID_EDGE_MODE,
            ANDROID_FLASH_MODE,
            ANDROID_JPEG_ORIENTATION,
            ANDROID_JPEG_QUALITY,
            ANDROID_JPEG_THUMBNAIL_QUALITY,
            ANDROID_JPEG_THUMBNAIL_SIZE,
            ANDROID_LENS_FOCUS_DISTANCE,
            ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
            ANDROID_NOISE_REDUCTION_MODE,
            ANDROID_SENSOR_FRAME_DURATION,
            static_cast<int32_t>(kOplusPackageNameTag),
    };
    const int32_t sessionKeys[] = {
            static_cast<int32_t>(kOplusPackageNameTag),
    };
    const int32_t resultKeys[] = {
            ANDROID_CONTROL_AE_MODE,
            ANDROID_CONTROL_AE_STATE,
            ANDROID_CONTROL_AF_MODE,
            ANDROID_CONTROL_AF_STATE,
            ANDROID_CONTROL_AWB_MODE,
            ANDROID_CONTROL_AWB_STATE,
            ANDROID_CONTROL_MODE,
            ANDROID_FLASH_MODE,
            ANDROID_FLASH_STATE,
            ANDROID_LENS_FOCUS_DISTANCE,
            ANDROID_LENS_STATE,
            ANDROID_REQUEST_PIPELINE_DEPTH,
            ANDROID_SENSOR_EXPOSURE_TIME,
            ANDROID_SENSOR_FRAME_DURATION,
            ANDROID_SENSOR_SENSITIVITY,
            ANDROID_SENSOR_TIMESTAMP,
    };

    const int32_t streamConfigurations[] = {
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 640, 480,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 720,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1920, 1080,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 2560, 1440,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 3840, 2160,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 4096, 3072,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 640, 480,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 720,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 1920, 1080,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 2560, 1440,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 3840, 2160,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 4096, 3072,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_BLOB, 640, 480,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_BLOB, 1280, 720,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_BLOB, 1920, 1080,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_BLOB, 2560, 1440,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_BLOB, 3840, 2160,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
            HAL_PIXEL_FORMAT_BLOB, 4096, 3072,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
    };
    const int64_t minDurations[] = {
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 640, 480, kFrameDurationNs,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 720, kFrameDurationNs,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1920, 1080, kFrameDurationNs,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 2560, 1440, kFrameDurationNs,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 3840, 2160, 66666666,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 4096, 3072, 111111111,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 640, 480, kFrameDurationNs,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 720, kFrameDurationNs,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 1920, 1080, kFrameDurationNs,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 2560, 1440, kFrameDurationNs,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 3840, 2160, 66666666,
            HAL_PIXEL_FORMAT_YCbCr_420_888, 4096, 3072, 111111111,
            HAL_PIXEL_FORMAT_BLOB, 640, 480, kFrameDurationNs,
            HAL_PIXEL_FORMAT_BLOB, 1280, 720, kFrameDurationNs,
            HAL_PIXEL_FORMAT_BLOB, 1920, 1080, kFrameDurationNs,
            HAL_PIXEL_FORMAT_BLOB, 2560, 1440, kFrameDurationNs,
            HAL_PIXEL_FORMAT_BLOB, 3840, 2160, 66666666,
            HAL_PIXEL_FORMAT_BLOB, 4096, 3072, 111111111,
    };
    const int64_t stallDurations[] = {
            HAL_PIXEL_FORMAT_BLOB, 640, 480, 100000000,
            HAL_PIXEL_FORMAT_BLOB, 1280, 720, 150000000,
            HAL_PIXEL_FORMAT_BLOB, 1920, 1080, 250000000,
            HAL_PIXEL_FORMAT_BLOB, 2560, 1440, 400000000,
            HAL_PIXEL_FORMAT_BLOB, 3840, 2160, 800000000,
            HAL_PIXEL_FORMAT_BLOB, 4096, 3072, 1200000000,
    };
    const float maxDigitalZoom[] = {1.0f};
    const uint8_t croppingType[] = {ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY};
    const int32_t activeArray[] = {0, 0, 4096, 3072};
    const int32_t sensitivityRange[] = {100, 1600};
    const int64_t exposureRange[] = {100000, 30000000};
    const int64_t maxFrameDuration[] = {66666666};
    const float physicalSize[] = {4.8f, 3.6f};
    const int32_t pixelArray[] = {4096, 3072};
    const int32_t orientation[] = {cameraId == 0 ? 90 : 270};
    const uint8_t timestampSource[] = {ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME};
    const uint8_t faceDetectModes[] = {ANDROID_STATISTICS_FACE_DETECT_MODE_OFF};
    const int32_t maxFaceCount[] = {0};
    const uint8_t hardwareLevel[] = {ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED};
    const int32_t maxLatency[] = {ANDROID_SYNC_MAX_LATENCY_PER_FRAME_CONTROL};

    const int32_t characteristicKeys[] = {
            ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
            ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
            ANDROID_CONTROL_AE_AVAILABLE_MODES,
            ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
            ANDROID_CONTROL_AE_COMPENSATION_RANGE,
            ANDROID_CONTROL_AE_COMPENSATION_STEP,
            ANDROID_CONTROL_AF_AVAILABLE_MODES,
            ANDROID_CONTROL_AVAILABLE_EFFECTS,
            ANDROID_CONTROL_AVAILABLE_MODES,
            ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
            ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
            ANDROID_CONTROL_AWB_AVAILABLE_MODES,
            ANDROID_CONTROL_MAX_REGIONS,
            ANDROID_CONTROL_AE_LOCK_AVAILABLE,
            ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
            ANDROID_EDGE_AVAILABLE_EDGE_MODES,
            ANDROID_FLASH_INFO_AVAILABLE,
            ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,
            ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
            ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
            ANDROID_JPEG_MAX_SIZE,
            ANDROID_LENS_FACING,
            ANDROID_LENS_INFO_AVAILABLE_APERTURES,
            ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
            ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
            ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
            ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
            ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
            ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
            ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
            ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
            ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
            ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
            ANDROID_REQUEST_AVAILABLE_SESSION_KEYS,
            ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
            ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
            ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
            ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
            ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
            ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
            ANDROID_SCALER_CROPPING_TYPE,
            ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
            ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
            ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
            ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
            ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
            ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
            ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
            ANDROID_SENSOR_ORIENTATION,
            ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
            ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
            ANDROID_SYNC_MAX_LATENCY,
    };

    return metadata->add(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES, aberrationModes) &&
           metadata->add(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, aeAntibanding) &&
           metadata->add(ANDROID_CONTROL_AE_AVAILABLE_MODES, aeModes) &&
           metadata->add(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, fpsRanges) &&
           metadata->add(ANDROID_CONTROL_AE_COMPENSATION_RANGE, compensationRange) &&
           metadata->add(ANDROID_CONTROL_AE_COMPENSATION_STEP, compensationStep) &&
           metadata->add(ANDROID_CONTROL_AF_AVAILABLE_MODES, afModes) &&
           metadata->add(ANDROID_CONTROL_AVAILABLE_EFFECTS, effects) &&
           metadata->add(ANDROID_CONTROL_AVAILABLE_SCENE_MODES, sceneModes) &&
           metadata->add(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, stabilizationModes) &&
           metadata->add(ANDROID_CONTROL_AWB_AVAILABLE_MODES, awbModes) &&
           metadata->add(ANDROID_CONTROL_MAX_REGIONS, maxRegions) &&
           metadata->add(ANDROID_CONTROL_AE_LOCK_AVAILABLE, falseValue) &&
           metadata->add(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, falseValue) &&
           metadata->add(ANDROID_CONTROL_AVAILABLE_MODES, controlModes) &&
           metadata->add(ANDROID_EDGE_AVAILABLE_EDGE_MODES, edgeModes) &&
           metadata->add(ANDROID_FLASH_INFO_AVAILABLE, flashAvailable) &&
           metadata->add(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, hotPixelModes) &&
           metadata->add(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, thumbnailSizes) &&
           metadata->add(ANDROID_JPEG_MAX_SIZE, jpegMaxSize) &&
           metadata->add(ANDROID_LENS_INFO_AVAILABLE_APERTURES, apertures) &&
           metadata->add(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES, filterDensities) &&
           metadata->add(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, focalLengths) &&
           metadata->add(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, opticalStabilization) &&
           metadata->add(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, minimumFocusDistance) &&
           metadata->add(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION, focusCalibration) &&
           metadata->add(ANDROID_LENS_FACING, lensFacing) &&
           metadata->add(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, noiseModes) &&
           metadata->add(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, maxOutputStreams) &&
           metadata->add(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, pipelineDepth) &&
           metadata->add(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, partialResultCount) &&
           metadata->add(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, capabilities) &&
           metadata->add(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, requestKeys) &&
           metadata->add(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, resultKeys) &&
           metadata->add(ANDROID_REQUEST_AVAILABLE_SESSION_KEYS, sessionKeys) &&
           metadata->add(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, characteristicKeys) &&
           metadata->add(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, streamConfigurations) &&
           metadata->add(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, minDurations) &&
           metadata->add(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stallDurations) &&
           metadata->add(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, maxDigitalZoom) &&
           metadata->add(ANDROID_SCALER_CROPPING_TYPE, croppingType) &&
           metadata->add(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, activeArray) &&
           metadata->add(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE, sensitivityRange) &&
           metadata->add(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, exposureRange) &&
           metadata->add(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, maxFrameDuration) &&
           metadata->add(ANDROID_SENSOR_INFO_PHYSICAL_SIZE, physicalSize) &&
           metadata->add(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixelArray) &&
           metadata->add(ANDROID_SENSOR_ORIENTATION, orientation) &&
           metadata->add(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, timestampSource) &&
           metadata->add(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, faceDetectModes) &&
           metadata->add(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, maxFaceCount) &&
           metadata->add(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, hardwareLevel) &&
           metadata->add(ANDROID_SYNC_MAX_LATENCY, maxLatency);
}

}  // namespace

camera3_device_ops_t VirtualCamera::operations_ = {
        .initialize = VirtualCamera::initialize,
        .configure_streams = VirtualCamera::configureStreams,
        .register_stream_buffers = nullptr,
        .construct_default_request_settings = VirtualCamera::constructDefaultRequestSettings,
        .process_capture_request = VirtualCamera::processCaptureRequest,
        .get_metadata_vendor_tag_ops = nullptr,
        .dump = VirtualCamera::dump,
        .flush = VirtualCamera::flush,
        .signal_stream_flush = nullptr,
        .is_reconfiguration_required = nullptr,
        .reserved = {nullptr},
};

VirtualCamera::VirtualCamera(int id) : id_(id) {
    device_.common.tag = HARDWARE_DEVICE_TAG;
    device_.common.version = CAMERA_DEVICE_API_VERSION_3_5;
    device_.common.close = closeDevice;
    device_.ops = &operations_;
    device_.priv = this;
    staticMetadata_ = buildStaticMetadata();
}

VirtualCamera::~VirtualCamera() {
    if (gralloc1_ != nullptr) {
        gralloc1_close(gralloc1_);
        gralloc1_ = nullptr;
    }
    for (camera_metadata_t* metadata : templates_) {
        if (metadata != nullptr) free_camera_metadata(metadata);
    }
    if (lastSettings_ != nullptr) free_camera_metadata(lastSettings_);
    if (staticMetadata_ != nullptr) free_camera_metadata(staticMetadata_);
}

VirtualCamera* VirtualCamera::self(const camera3_device_t* device) {
    return device == nullptr ? nullptr : static_cast<VirtualCamera*>(device->priv);
}

int VirtualCamera::open(const hw_module_t* module, hw_device_t** device) {
    if (module == nullptr || device == nullptr) return -EINVAL;
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_) return -EBUSY;
    if (staticMetadata_ == nullptr) return -ENODEV;
    device_.common.module = const_cast<hw_module_t*>(module);
    callbacks_ = nullptr;
    configured_ = false;
    layoutLogged_ = false;
    requestLogged_ = false;
    bufferErrorLogged_ = false;
    resultLogged_ = false;
    lastFrameTimestampNs_ = 0;
    open_ = true;
    *device = &device_.common;
    ALOGI("Opened virtual camera %d", id_);
    return 0;
}

int VirtualCamera::getInfo(camera_info* info) {
    if (info == nullptr || staticMetadata_ == nullptr) return -EINVAL;
    memset(info, 0, sizeof(*info));
    info->facing = id_ == 0 ? CAMERA_FACING_BACK : CAMERA_FACING_FRONT;
    info->orientation = id_ == 0 ? 90 : 270;
    info->device_version = CAMERA_DEVICE_API_VERSION_3_5;
    info->static_camera_characteristics = staticMetadata_;
    info->resource_cost = 50;
    return 0;
}

void VirtualCamera::setSourcePath(std::string path) {
    std::lock_guard<std::mutex> lock(mutex_);
    sourcePath_ = std::move(path);
    sourceFrame_ = RgbFrame{};
}

int VirtualCamera::closeDevice(hw_device_t* device) {
    if (device == nullptr) return -EINVAL;
    auto* cameraDevice = reinterpret_cast<camera3_device_t*>(device);
    VirtualCamera* camera = self(cameraDevice);
    if (camera == nullptr) return -EINVAL;
    std::lock_guard<std::mutex> lock(camera->mutex_);
    return camera->closeLocked();
}

int VirtualCamera::closeLocked() {
    if (!open_) return -EINVAL;
    streams_.clear();
    configured_ = false;
    layoutLogged_ = false;
    requestLogged_ = false;
    bufferErrorLogged_ = false;
    resultLogged_ = false;
    lastFrameTimestampNs_ = 0;
    callbacks_ = nullptr;
    if (gralloc1_ != nullptr) {
        gralloc1_close(gralloc1_);
        gralloc1_ = nullptr;
    }
    getNumFlexPlanes_ = nullptr;
    lock1_ = nullptr;
    lockFlex1_ = nullptr;
    unlock1_ = nullptr;
    if (lastSettings_ != nullptr) {
        free_camera_metadata(lastSettings_);
        lastSettings_ = nullptr;
    }
    open_ = false;
    ALOGI("Closed virtual camera %d", id_);
    return 0;
}

int VirtualCamera::initialize(const camera3_device_t* device,
                              const camera3_callback_ops_t* callbacks) {
    VirtualCamera* camera = self(device);
    if (camera == nullptr) return -EINVAL;
    std::lock_guard<std::mutex> lock(camera->mutex_);
    return camera->initializeLocked(callbacks);
}

int VirtualCamera::initializeLocked(const camera3_callback_ops_t* callbacks) {
    if (!open_ || callbacks == nullptr) return -EINVAL;
    const hw_module_t* module = nullptr;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) != 0 || module == nullptr) {
        ALOGE("Unable to load gralloc module");
        return -ENODEV;
    }
    gralloc_ = reinterpret_cast<const gralloc_module_t*>(module);
    if (gralloc_->lock_ycbcr == nullptr || gralloc_->lock == nullptr ||
        gralloc_->unlock == nullptr) {
        if (gralloc1_open(module, &gralloc1_) != 0 || gralloc1_ == nullptr ||
            gralloc1_->getFunction == nullptr) {
            ALOGE("Neither legacy gralloc nor Gralloc1 is available");
            return -ENODEV;
        }
        getNumFlexPlanes_ = reinterpret_cast<GRALLOC1_PFN_GET_NUM_FLEX_PLANES>(
                gralloc1_->getFunction(
                        gralloc1_, GRALLOC1_FUNCTION_GET_NUM_FLEX_PLANES));
        lock1_ = reinterpret_cast<GRALLOC1_PFN_LOCK>(gralloc1_->getFunction(
                gralloc1_, GRALLOC1_FUNCTION_LOCK));
        lockFlex1_ = reinterpret_cast<GRALLOC1_PFN_LOCK_FLEX>(
                gralloc1_->getFunction(gralloc1_, GRALLOC1_FUNCTION_LOCK_FLEX));
        unlock1_ = reinterpret_cast<GRALLOC1_PFN_UNLOCK>(gralloc1_->getFunction(
                gralloc1_, GRALLOC1_FUNCTION_UNLOCK));
        if (getNumFlexPlanes_ == nullptr || lock1_ == nullptr ||
            lockFlex1_ == nullptr || unlock1_ == nullptr) {
            ALOGE("Gralloc1 is missing required lock functions");
            gralloc1_close(gralloc1_);
            gralloc1_ = nullptr;
            return -ENODEV;
        }
        ALOGI("Using Gralloc1 buffer access");
    } else {
        ALOGI("Using legacy gralloc buffer access");
    }
    callbacks_ = callbacks;
    return 0;
}

int VirtualCamera::configureStreams(const camera3_device_t* device,
                                    camera3_stream_configuration_t* streams) {
    VirtualCamera* camera = self(device);
    if (camera == nullptr) return -EINVAL;
    std::lock_guard<std::mutex> lock(camera->mutex_);
    return camera->configureStreamsLocked(streams);
}

int VirtualCamera::configureStreamsLocked(camera3_stream_configuration_t* config) {
    if (!open_ || callbacks_ == nullptr || config == nullptr ||
        config->num_streams == 0 || config->streams == nullptr) {
        return -EINVAL;
    }
    if (config->operation_mode != CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE) {
        return -EINVAL;
    }
    logClientPackage(config->session_parameters, "session parameters");
    loadSourceConfigurationLocked();

    std::vector<camera3_stream_t*> accepted;
    accepted.reserve(config->num_streams);
    for (uint32_t i = 0; i < config->num_streams; ++i) {
        camera3_stream_t* stream = config->streams[i];
        if (stream == nullptr) {
            ALOGE("Rejected null stream camera=%d index=%u", id_, i);
            return -EINVAL;
        }
        if (stream->format != HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
            stream->format != HAL_PIXEL_FORMAT_YCbCr_420_888 &&
            stream->format != HAL_PIXEL_FORMAT_BLOB) {
            ALOGE("Rejected stream camera=%d index=%u unsupported format=%d",
                  id_, i, stream->format);
            return -EINVAL;
        }
        if (stream->stream_type != CAMERA3_STREAM_OUTPUT ||
            stream->rotation != CAMERA3_STREAM_ROTATION_0 ||
            !supportedSize(stream->format, stream->width, stream->height)) {
            ALOGE("Rejected stream camera=%d index=%u type=%d format=%d "
                  "size=%ux%u rotation=%d",
                  id_, i, stream->stream_type, stream->format,
                  stream->width, stream->height, stream->rotation);
            return -EINVAL;
        }

        ALOGI("Accepting stream camera=%d index=%u type=%d format=%d "
              "size=%ux%u rotation=%d usage=%" PRIu64,
              id_, i, stream->stream_type, stream->format,
              stream->width, stream->height, stream->rotation,
              static_cast<uint64_t>(stream->usage));

        // Camera3 passes consumer usage into configure_streams. Preserve it so
        // SurfaceTexture buffers retain HW_TEXTURE/display compatibility.
        stream->usage |= GRALLOC_USAGE_SW_WRITE_OFTEN |
                         GRALLOC_USAGE_HW_CAMERA_WRITE;
        stream->max_buffers = 2;
        stream->priv = this;
        accepted.push_back(stream);
    }

    streams_ = std::move(accepted);
    configured_ = true;
    layoutLogged_ = false;
    requestLogged_ = false;
    bufferErrorLogged_ = false;
    resultLogged_ = false;
    lastFrameTimestampNs_ = 0;
    if (lastSettings_ != nullptr) {
        free_camera_metadata(lastSettings_);
        lastSettings_ = nullptr;
    }
    ALOGI("Configured virtual camera %d with %u streams", id_, config->num_streams);
    return 0;
}

void VirtualCamera::loadSourceConfigurationLocked() {
    sourceTransform_ = {};
    sourceFrameDurationNs_ = kFrameDurationNs;
    const size_t slash = sourcePath_.find_last_of('/');
    if (slash == std::string::npos) return;
    const std::string directory = sourcePath_.substr(0, slash);

    int fps = 30;
    int ignoredWidth = 0;
    int ignoredHeight = 0;
    if (FILE* source = fopen((directory + "/source.cfg").c_str(), "re")) {
        if (fscanf(source, "%d,%d,%d", &fps, &ignoredWidth, &ignoredHeight) == 3 &&
            fps >= 1 && fps <= 60) {
            sourceFrameDurationNs_ = 1000000000LL / fps;
        }
        fclose(source);
    }

    int rotation = 0;
    int scale = 1000;
    int centerX = 500;
    int centerY = 500;
    const std::string viewPath = directory + "/view-" + std::to_string(id_) + ".cfg";
    if (FILE* view = fopen(viewPath.c_str(), "re")) {
        const int parsed = fscanf(view, "%d,%d,%d,%d", &rotation, &scale,
                                  &centerX, &centerY);
        fclose(view);
        if (parsed != 4 || (rotation != 0 && rotation != 90 &&
                rotation != 180 && rotation != 270) || scale < 100 ||
                scale > 8000 || centerX < 0 || centerX > 1000 ||
                centerY < 0 || centerY > 1000) {
            rotation = 0; scale = 1000; centerX = 500; centerY = 500;
        }
    }
    const int sensorOrientation = id_ == 0 ? 90 : 270;
    int totalRotation = rotation - sensorOrientation;
    while (totalRotation <= -270) totalRotation += 360;
    while (totalRotation > 270) totalRotation -= 360;
    sourceTransform_.rotationDegrees = totalRotation;
    sourceTransform_.scale = scale / 1000.0f;
    sourceTransform_.centerX = centerX / 1000.0f;
    sourceTransform_.centerY = centerY / 1000.0f;
    ALOGI("Source view camera=%d rotation=%d scale=%.3f center=%.3f,%.3f fps=%lld",
          id_, rotation, sourceTransform_.scale, sourceTransform_.centerX,
          sourceTransform_.centerY,
          static_cast<long long>(1000000000LL / sourceFrameDurationNs_));
}

const camera_metadata_t* VirtualCamera::constructDefaultRequestSettings(
        const camera3_device_t* device, int type) {
    VirtualCamera* camera = self(device);
    if (camera == nullptr) return nullptr;
    std::lock_guard<std::mutex> lock(camera->mutex_);
    return camera->defaultRequestLocked(type);
}

const camera_metadata_t* VirtualCamera::defaultRequestLocked(int type) {
    if (!open_ || type < CAMERA3_TEMPLATE_PREVIEW || type >= CAMERA3_TEMPLATE_COUNT) {
        return nullptr;
    }
    if (type == CAMERA3_TEMPLATE_MANUAL) {
        return nullptr;
    }
    if (templates_[type] == nullptr) {
        templates_[type] = buildDefaultRequest(type);
    }
    return templates_[type];
}

int VirtualCamera::processCaptureRequest(const camera3_device_t* device,
                                         camera3_capture_request_t* request) {
    VirtualCamera* camera = self(device);
    if (camera == nullptr) return -EINVAL;
    std::lock_guard<std::mutex> lock(camera->mutex_);
    return camera->processRequestLocked(request);
}

int VirtualCamera::processRequestLocked(camera3_capture_request_t* request) {
    if (!open_ || !configured_ || callbacks_ == nullptr || request == nullptr ||
        request->num_output_buffers == 0 || request->output_buffers == nullptr ||
        request->input_buffer != nullptr) {
        ALOGE("Rejected capture request camera=%d open=%d configured=%d "
              "callbacks=%d request=%d outputs=%u outputBuffers=%d input=%d",
              id_, open_, configured_, callbacks_ != nullptr, request != nullptr,
              request == nullptr ? 0 : request->num_output_buffers,
              request != nullptr && request->output_buffers != nullptr,
              request != nullptr && request->input_buffer != nullptr);
        return -EINVAL;
    }
    if (!requestLogged_) {
        ALOGI("First capture request camera=%d frame=%u outputs=%u settings=%d",
              id_, request->frame_number, request->num_output_buffers,
              request->settings != nullptr);
        logClientPackage(request->settings, "first request");
        requestLogged_ = true;
    }
    if (request->settings != nullptr) {
        camera_metadata_t* copy = clone_camera_metadata(request->settings);
        if (copy == nullptr) return -ENOMEM;
        if (lastSettings_ != nullptr) free_camera_metadata(lastSettings_);
        lastSettings_ = copy;
    } else if (lastSettings_ == nullptr) {
        return -EINVAL;
    }

    uint64_t timestamp = bootTimeNs();
    if (lastFrameTimestampNs_ != 0) {
        const uint64_t target = lastFrameTimestampNs_ + sourceFrameDurationNs_;
        if (timestamp < target) {
            sleepForNs(target - timestamp);
            timestamp = bootTimeNs();
        }
    }
    lastFrameTimestampNs_ = timestamp;
    notifyShutter(request->frame_number, timestamp);

    std::vector<camera3_stream_buffer_t> outputs(request->num_output_buffers);
    for (uint32_t i = 0; i < request->num_output_buffers; ++i) {
        if (!fillBuffer(request->output_buffers[i], &outputs[i], request->frame_number)) {
            outputs[i].status = CAMERA3_BUFFER_STATUS_ERROR;
            notifyBufferError(request->frame_number, outputs[i].stream);
        }
    }

    camera_metadata_t* resultMetadata = buildResultMetadata(timestamp);
    if (resultMetadata == nullptr) {
        ALOGE("Unable to build result metadata camera=%d frame=%u",
              id_, request->frame_number);
        return -ENOMEM;
    }

    camera3_capture_result_t result{};
    result.frame_number = request->frame_number;
    result.result = resultMetadata;
    result.num_output_buffers = static_cast<uint32_t>(outputs.size());
    result.output_buffers = outputs.data();
    // The stock camera metadata on the target ROM advertises two partial
    // results. This is the final (and only) result emitted by the virtual
    // pipeline, so mark it with the advertised final partial-result index.
    // Otherwise Camera3Device keeps every request in-flight indefinitely and
    // consumers render a black surface.
    result.partial_result = 2;
    if (!resultLogged_) {
        ALOGI("Sending first capture result camera=%d frame=%u outputs=%u",
              id_, result.frame_number, result.num_output_buffers);
    }
    callbacks_->process_capture_result(callbacks_, &result);
    if (!resultLogged_) {
        ALOGI("First capture result callback returned camera=%d", id_);
        resultLogged_ = true;
    }
    free_camera_metadata(resultMetadata);
    return 0;
}

bool VirtualCamera::fillBuffer(const camera3_stream_buffer_t& input,
                               camera3_stream_buffer_t* output,
                               uint64_t frameNumber) {
    if (output == nullptr) {
        return false;
    }
    *output = input;
    if (input.stream == nullptr || input.buffer == nullptr || *input.buffer == nullptr) {
        output->status = CAMERA3_BUFFER_STATUS_ERROR;
        output->release_fence = input.acquire_fence;
        output->acquire_fence = -1;
        if (!bufferErrorLogged_) {
            ALOGE("Invalid output buffer camera=%d frame=%" PRIu64
                  " stream=%d bufferPointer=%d handle=%d fence=%d",
                  id_, frameNumber, input.stream != nullptr,
                  input.buffer != nullptr,
                  input.buffer != nullptr && *input.buffer != nullptr,
                  input.acquire_fence);
            bufferErrorLogged_ = true;
        }
        return false;
    }
    output->acquire_fence = -1;
    output->release_fence = -1;
    output->status = CAMERA3_BUFFER_STATUS_OK;

    sourceFrame_.reloadIfChanged(sourcePath_.c_str());

    if (input.acquire_fence >= 0) {
        const int waitResult = waitForFence(input.acquire_fence, kFenceTimeoutMs);
        close(input.acquire_fence);
        if (waitResult != 0) {
            ALOGE("Fence wait failed for camera %d frame %" PRIu64, id_, frameNumber);
            return false;
        }
    }

    if (input.stream->format == HAL_PIXEL_FORMAT_BLOB) {
        return fillJpeg(output, frameNumber);
    }
    return fillYuv(output, frameNumber);
}

bool VirtualCamera::fillYuv(camera3_stream_buffer_t* buffer,
                            uint64_t frameNumber) {
    if (buffer == nullptr || buffer->stream == nullptr || buffer->buffer == nullptr ||
        *buffer->buffer == nullptr) {
        return false;
    }
    const camera3_stream_t* stream = buffer->stream;
    Yuv420Layout target{};
    android_ycbcr legacyLayout{};
    std::vector<android_flex_plane_t> flexPlanes;
    android_flex_layout_t flexLayout{};

    if (gralloc1_ != nullptr) {
        uint32_t planeCount = 0;
        int lockResult = getNumFlexPlanes_(
                gralloc1_, *buffer->buffer, &planeCount);
        if (lockResult != GRALLOC1_ERROR_NONE || planeCount < 3 || planeCount > 8) {
            ALOGE("getNumFlexPlanes failed: camera=%d planes=%u error=%d",
                  id_, planeCount, lockResult);
            return false;
        }
        flexPlanes.resize(planeCount);
        flexLayout.num_planes = planeCount;
        flexLayout.planes = flexPlanes.data();
        const gralloc1_rect_t region{
                0, 0, static_cast<int32_t>(stream->width),
                static_cast<int32_t>(stream->height)};
        lockResult = lockFlex1_(
                gralloc1_, *buffer->buffer,
                GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN,
                GRALLOC1_CONSUMER_USAGE_NONE, &region, &flexLayout, -1);
        if (lockResult != GRALLOC1_ERROR_NONE ||
            flexLayout.format != FLEX_FORMAT_YCbCr) {
            ALOGE("Gralloc1 lockFlex failed: camera=%d format=%d flex=%d error=%d",
                  id_, stream->format, flexLayout.format, lockResult);
            return false;
        }
        const android_flex_plane_t* yPlane = nullptr;
        const android_flex_plane_t* cbPlane = nullptr;
        const android_flex_plane_t* crPlane = nullptr;
        for (uint32_t i = 0; i < flexLayout.num_planes; ++i) {
            const android_flex_plane_t& plane = flexLayout.planes[i];
            if (plane.component == FLEX_COMPONENT_Y) yPlane = &plane;
            if (plane.component == FLEX_COMPONENT_Cb) cbPlane = &plane;
            if (plane.component == FLEX_COMPONENT_Cr) crPlane = &plane;
        }
        if (yPlane == nullptr || cbPlane == nullptr || crPlane == nullptr ||
            yPlane->bits_used != 8 || cbPlane->bits_used != 8 ||
            crPlane->bits_used != 8 || yPlane->h_increment <= 0 ||
            yPlane->v_increment <= 0 || cbPlane->h_increment <= 0 ||
            cbPlane->v_increment <= 0 || crPlane->h_increment != cbPlane->h_increment ||
            crPlane->v_increment != cbPlane->v_increment) {
            ALOGE("Unsupported Gralloc1 flex layout camera=%d", id_);
            int releaseFence = -1;
            unlock1_(gralloc1_, *buffer->buffer, &releaseFence);
            if (releaseFence >= 0) close(releaseFence);
            return false;
        }
        target = {
                .y = yPlane->top_left,
                .cb = cbPlane->top_left,
                .cr = crPlane->top_left,
                .yStride = static_cast<size_t>(yPlane->v_increment),
                .cStride = static_cast<size_t>(cbPlane->v_increment),
                .chromaStep = static_cast<size_t>(cbPlane->h_increment),
                .yStep = static_cast<size_t>(yPlane->h_increment),
        };
    } else if (gralloc_ != nullptr && gralloc_->lock_ycbcr != nullptr &&
               gralloc_->unlock != nullptr) {
        const int usage = GRALLOC_USAGE_SW_WRITE_OFTEN |
                          GRALLOC_USAGE_HW_CAMERA_WRITE;
        const int lockResult = gralloc_->lock_ycbcr(
                gralloc_, *buffer->buffer, usage, 0, 0,
                stream->width, stream->height, &legacyLayout);
        if (lockResult != 0) {
            ALOGE("lock_ycbcr failed: camera=%d format=%d size=%ux%u error=%d",
                  id_, stream->format, stream->width, stream->height, lockResult);
            return false;
        }
        target = {
                .y = static_cast<uint8_t*>(legacyLayout.y),
                .cb = static_cast<uint8_t*>(legacyLayout.cb),
                .cr = static_cast<uint8_t*>(legacyLayout.cr),
                .yStride = legacyLayout.ystride,
                .cStride = legacyLayout.cstride,
                .chromaStep = legacyLayout.chroma_step,
                .yStep = 1,
        };
    } else {
        return false;
    }
    if (!layoutLogged_) {
        ALOGI("YUV layout camera=%d format=%d size=%ux%u yStride=%zu "
              "cStride=%zu chromaStep=%zu usage=%" PRIu64,
              id_, stream->format, stream->width, stream->height,
              target.yStride, target.cStride, target.chromaStep,
              static_cast<uint64_t>(stream->usage));
        layoutLogged_ = true;
    }
    const bool generated = sourceFrame_.valid()
            ? sourceFrame_.fillYuv420(stream->width, stream->height, target,
                                     sourceTransform_)
            : PatternGenerator::fillYuv420(
                    stream->width, stream->height, frameNumber, id_, target);
    int unlockResult = 0;
    if (gralloc1_ != nullptr) {
        int releaseFence = -1;
        unlockResult = unlock1_(gralloc1_, *buffer->buffer, &releaseFence);
        buffer->release_fence = releaseFence;
    } else {
        unlockResult = gralloc_->unlock(gralloc_, *buffer->buffer);
    }
    return generated && unlockResult == 0;
}

bool VirtualCamera::fillJpeg(camera3_stream_buffer_t* buffer,
                             uint64_t frameNumber) {
    if (buffer == nullptr || buffer->stream == nullptr || buffer->buffer == nullptr ||
        *buffer->buffer == nullptr) {
        return false;
    }
    const camera3_stream_t* stream = buffer->stream;
    jpeg_compress_struct compressor{};
    JpegErrorManager errorManager{};
    compressor.err = jpeg_std_error(&errorManager.base);
    errorManager.base.error_exit = jpegErrorExit;

    if (setjmp(errorManager.jump) != 0) {
        jpeg_destroy_compress(&compressor);
        std::free(errorManager.output);
        ALOGE("JPEG compression failed for camera %d", id_);
        return false;
    }
    jpeg_create_compress(&compressor);

    unsigned long encodedSize = 0;
    jpeg_mem_dest(&compressor, &errorManager.output, &encodedSize);
    compressor.image_width = stream->width;
    compressor.image_height = stream->height;
    compressor.input_components = 3;
    compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, 88, TRUE);
    jpeg_start_compress(&compressor, TRUE);

    std::vector<uint8_t> row(stream->width * 3);
    while (compressor.next_scanline < compressor.image_height) {
        if (!sourceFrame_.fillRgbRow(stream->width, stream->height,
                                     compressor.next_scanline, row.data(),
                                     sourceTransform_)) {
            PatternGenerator::fillRgbRow(stream->width, stream->height,
                                         compressor.next_scanline, frameNumber,
                                         id_, row.data());
        }
        JSAMPROW rowPointer = row.data();
        jpeg_write_scanlines(&compressor, &rowPointer, 1);
    }
    jpeg_finish_compress(&compressor);
    jpeg_destroy_compress(&compressor);

    if (errorManager.output == nullptr ||
        encodedSize + sizeof(camera3_jpeg_blob_t) > kJpegMaxSize) {
        std::free(errorManager.output);
        return false;
    }

    void* destination = nullptr;
    int lockResult = -ENODEV;
    if (gralloc1_ != nullptr) {
        const gralloc1_rect_t region{0, 0, kJpegMaxSize, 1};
        lockResult = lock1_(
                gralloc1_, *buffer->buffer,
                GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN,
                GRALLOC1_CONSUMER_USAGE_NONE, &region, &destination, -1);
    } else if (gralloc_ != nullptr && gralloc_->lock != nullptr) {
        const int usage = GRALLOC_USAGE_SW_WRITE_OFTEN |
                          GRALLOC_USAGE_HW_CAMERA_WRITE;
        lockResult = gralloc_->lock(gralloc_, *buffer->buffer, usage,
                                    0, 0, kJpegMaxSize, 1, &destination);
    }
    if (lockResult != 0 || destination == nullptr) {
        std::free(errorManager.output);
        return false;
    }

    memcpy(destination, errorManager.output, encodedSize);
    auto* footer = reinterpret_cast<camera3_jpeg_blob_t*>(
            static_cast<uint8_t*>(destination) + kJpegMaxSize -
            sizeof(camera3_jpeg_blob_t));
    footer->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
    footer->jpeg_size = static_cast<uint32_t>(encodedSize);
    int unlockResult = 0;
    if (gralloc1_ != nullptr) {
        int releaseFence = -1;
        unlockResult = unlock1_(gralloc1_, *buffer->buffer, &releaseFence);
        buffer->release_fence = releaseFence;
    } else {
        unlockResult = gralloc_->unlock(gralloc_, *buffer->buffer);
    }
    std::free(errorManager.output);
    return unlockResult == 0;
}

void VirtualCamera::notifyShutter(uint32_t frameNumber, uint64_t timestamp) const {
    camera3_notify_msg_t message{};
    message.type = CAMERA3_MSG_SHUTTER;
    message.message.shutter.frame_number = frameNumber;
    message.message.shutter.timestamp = timestamp;
    callbacks_->notify(callbacks_, &message);
}

void VirtualCamera::notifyBufferError(uint32_t frameNumber,
                                      camera3_stream_t* stream) const {
    camera3_notify_msg_t message{};
    message.type = CAMERA3_MSG_ERROR;
    message.message.error.frame_number = frameNumber;
    message.message.error.error_stream = stream;
    message.message.error.error_code = CAMERA3_MSG_ERROR_BUFFER;
    callbacks_->notify(callbacks_, &message);
}

camera_metadata_t* VirtualCamera::buildStaticMetadata() const {
    MetadataBuilder metadata(192, 32768);
    if (!addStaticMetadata(&metadata, id_)) {
        ALOGE("Unable to construct static metadata for camera %d", id_);
        return nullptr;
    }
    return metadata.release();
}

camera_metadata_t* VirtualCamera::buildDefaultRequest(int type) const {
    MetadataBuilder metadata(48, 4096);
    const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    const uint8_t aeAntibanding = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    const int32_t aeCompensation = 0;
    const uint8_t aeLock = 0;
    const int32_t fpsRange[] = {30, 30};
    const uint8_t afMode = ANDROID_CONTROL_AF_MODE_OFF;
    const uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    const uint8_t awbLock = 0;
    const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    const uint8_t stabilization = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    const uint8_t edgeMode = ANDROID_EDGE_MODE_OFF;
    const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    const float focusDistance = 0.0f;
    const uint8_t opticalStabilization = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    const uint8_t noiseReduction = ANDROID_NOISE_REDUCTION_MODE_OFF;
    const int64_t frameDuration = kFrameDurationNs;
    const uint8_t jpegQuality = 88;
    const uint8_t thumbnailQuality = 85;
    const int32_t thumbnailSize[] = {0, 0};
    const int32_t jpegOrientation = 0;
    uint8_t captureIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
    switch (type) {
        case CAMERA3_TEMPLATE_STILL_CAPTURE:
            captureIntent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
            break;
        case CAMERA3_TEMPLATE_VIDEO_RECORD:
            captureIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
            break;
        case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
            captureIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
            break;
        case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
            captureIntent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
            break;
        case CAMERA3_TEMPLATE_MANUAL:
            captureIntent = ANDROID_CONTROL_CAPTURE_INTENT_MANUAL;
            break;
        default:
            break;
    }

    if (!metadata.addOne(ANDROID_CONTROL_MODE, controlMode) ||
        !metadata.addOne(ANDROID_CONTROL_CAPTURE_INTENT, captureIntent) ||
        !metadata.addOne(ANDROID_CONTROL_AE_MODE, aeMode) ||
        !metadata.addOne(ANDROID_CONTROL_AE_ANTIBANDING_MODE, aeAntibanding) ||
        !metadata.addOne(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, aeCompensation) ||
        !metadata.addOne(ANDROID_CONTROL_AE_LOCK, aeLock) ||
        !metadata.add(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fpsRange) ||
        !metadata.addOne(ANDROID_CONTROL_AF_MODE, afMode) ||
        !metadata.addOne(ANDROID_CONTROL_AWB_MODE, awbMode) ||
        !metadata.addOne(ANDROID_CONTROL_AWB_LOCK, awbLock) ||
        !metadata.addOne(ANDROID_CONTROL_EFFECT_MODE, effectMode) ||
        !metadata.addOne(ANDROID_CONTROL_SCENE_MODE, sceneMode) ||
        !metadata.addOne(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, stabilization) ||
        !metadata.addOne(ANDROID_EDGE_MODE, edgeMode) ||
        !metadata.addOne(ANDROID_FLASH_MODE, flashMode) ||
        !metadata.addOne(ANDROID_LENS_FOCUS_DISTANCE, focusDistance) ||
        !metadata.addOne(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, opticalStabilization) ||
        !metadata.addOne(ANDROID_NOISE_REDUCTION_MODE, noiseReduction) ||
        !metadata.addOne(ANDROID_SENSOR_FRAME_DURATION, frameDuration) ||
        !metadata.addOne(ANDROID_JPEG_QUALITY, jpegQuality) ||
        !metadata.addOne(ANDROID_JPEG_THUMBNAIL_QUALITY, thumbnailQuality) ||
        !metadata.add(ANDROID_JPEG_THUMBNAIL_SIZE, thumbnailSize) ||
        !metadata.addOne(ANDROID_JPEG_ORIENTATION, jpegOrientation)) {
        return nullptr;
    }
    return metadata.release();
}

camera_metadata_t* VirtualCamera::buildResultMetadata(uint64_t timestamp) const {
    MetadataBuilder metadata(32, 2048);
    const int64_t sensorTimestamp = timestamp;
    const int64_t frameDuration = sourceFrameDurationNs_;
    const int64_t exposureTime = 10000000;
    const int32_t sensitivity = 100;
    const uint8_t pipelineDepth = 1;
    const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    const uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
    const uint8_t afMode = ANDROID_CONTROL_AF_MODE_OFF;
    const uint8_t afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
    const uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    const uint8_t awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    const float focusDistance = 0.0f;
    const uint8_t lensState = ANDROID_LENS_STATE_STATIONARY;
    const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    const uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;

    if (!metadata.addOne(ANDROID_SENSOR_TIMESTAMP, sensorTimestamp) ||
        !metadata.addOne(ANDROID_SENSOR_FRAME_DURATION, frameDuration) ||
        !metadata.addOne(ANDROID_SENSOR_EXPOSURE_TIME, exposureTime) ||
        !metadata.addOne(ANDROID_SENSOR_SENSITIVITY, sensitivity) ||
        !metadata.addOne(ANDROID_REQUEST_PIPELINE_DEPTH, pipelineDepth) ||
        !metadata.addOne(ANDROID_CONTROL_MODE, controlMode) ||
        !metadata.addOne(ANDROID_CONTROL_AE_MODE, aeMode) ||
        !metadata.addOne(ANDROID_CONTROL_AE_STATE, aeState) ||
        !metadata.addOne(ANDROID_CONTROL_AF_MODE, afMode) ||
        !metadata.addOne(ANDROID_CONTROL_AF_STATE, afState) ||
        !metadata.addOne(ANDROID_CONTROL_AWB_MODE, awbMode) ||
        !metadata.addOne(ANDROID_CONTROL_AWB_STATE, awbState) ||
        !metadata.addOne(ANDROID_LENS_FOCUS_DISTANCE, focusDistance) ||
        !metadata.addOne(ANDROID_LENS_STATE, lensState) ||
        !metadata.addOne(ANDROID_FLASH_MODE, flashMode) ||
        !metadata.addOne(ANDROID_FLASH_STATE, flashState)) {
        return nullptr;
    }
    return metadata.release();
}

void VirtualCamera::dump(const camera3_device_t* device, int fd) {
    VirtualCamera* camera = self(device);
    if (camera == nullptr) return;
    std::lock_guard<std::mutex> lock(camera->mutex_);
    camera->dumpLocked(fd);
}

void VirtualCamera::dumpLocked(int fd) const {
    dprintf(fd, "android-vcam camera=%d open=%d configured=%d streams=%zu\n",
            id_, open_, configured_, streams_.size());
}

int VirtualCamera::flush(const camera3_device_t* device) {
    VirtualCamera* camera = self(device);
    if (camera == nullptr) return -EINVAL;
    return 0;
}

}  // namespace vcam
