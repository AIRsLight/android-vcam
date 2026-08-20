/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */

#include <aidl/android/hardware/camera/device/BnCameraDeviceCallback.h>
#include <aidl/android/hardware/camera/device/BufferRequestStatus.h>
#include <aidl/android/hardware/camera/device/BufferStatus.h>
#include <aidl/android/hardware/camera/device/CaptureRequest.h>
#include <aidl/android/hardware/camera/device/ICameraDevice.h>
#include <aidl/android/hardware/camera/device/ICameraDeviceSession.h>
#include <aidl/android/hardware/camera/device/RequestTemplate.h>
#include <aidl/android/hardware/camera/device/Stream.h>
#include <aidl/android/hardware/camera/device/StreamConfiguration.h>
#include <aidl/android/hardware/camera/device/StreamConfigurationMode.h>
#include <aidl/android/hardware/camera/device/StreamRotation.h>
#include <aidl/android/hardware/camera/device/StreamType.h>
#include <aidl/android/hardware/camera/metadata/RequestAvailableColorSpaceProfilesMap.h>
#include <aidl/android/hardware/camera/metadata/RequestAvailableDynamicRangeProfilesMap.h>
#include <aidl/android/hardware/camera/metadata/ScalerAvailableStreamUseCases.h>
#include <aidl/android/hardware/camera/metadata/SensorPixelMode.h>
#include <aidl/android/hardware/camera/provider/ICameraProvider.h>
#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/Dataspace.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <grallocusage/GrallocUsageConversion.h>
#include <hardware/gralloc.h>
#include <sync/sync.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

namespace camera_device = aidl::android::hardware::camera::device;

constexpr const char* kDefaultInstance =
    "android.hardware.camera.provider.ICameraProvider/vcam/0";

bool PrintStatus(const char* operation, const ndk::ScopedAStatus& status) {
  if (status.isOk()) return true;
  std::fprintf(stderr, "%s failed: %s\n", operation,
               status.getDescription().c_str());
  return false;
}

class ProbeDeviceCallback final : public camera_device::BnCameraDeviceCallback {
 public:
  ndk::ScopedAStatus notify(
      const std::vector<camera_device::NotifyMsg>&) override {
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus processCaptureResult(
      const std::vector<camera_device::CaptureResult>& results) override {
    for (const auto& result : results) {
      if (result.outputBuffers.empty()) continue;
      const auto& output = result.outputBuffers.front();
      int fence_result = 0;
      if (!output.releaseFence.fds.empty()) {
        fence_result = sync_wait(output.releaseFence.fds.front().get(), 5000);
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_number_ = result.frameNumber;
        buffer_status_ = output.status;
        fence_result_ = fence_result;
        frame_received_ = true;
      }
      condition_.notify_all();
    }
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus requestStreamBuffers(
      const std::vector<camera_device::BufferRequest>&,
      std::vector<camera_device::StreamBufferRet>* out_buffers,
      camera_device::BufferRequestStatus* status) override {
    if (out_buffers != nullptr) out_buffers->clear();
    if (status != nullptr) {
      *status = camera_device::BufferRequestStatus::FAILED_CONFIGURING;
    }
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus returnStreamBuffers(
      const std::vector<camera_device::StreamBuffer>&) override {
    return ndk::ScopedAStatus::ok();
  }

  bool WaitForFrame(int32_t expected_frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!condition_.wait_for(lock, std::chrono::seconds(8),
                             [this] { return frame_received_; })) {
      std::fprintf(stderr, "Timed out waiting for capture result\n");
      return false;
    }
    if (frame_number_ != expected_frame ||
        buffer_status_ != camera_device::BufferStatus::OK ||
        fence_result_ != 0) {
      std::fprintf(stderr,
                   "Invalid capture result frame=%d status=%d fence=%d\n",
                   frame_number_, static_cast<int32_t>(buffer_status_),
                   fence_result_);
      return false;
    }
    return true;
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool frame_received_ = false;
  int32_t frame_number_ = -1;
  camera_device::BufferStatus buffer_status_ =
      camera_device::BufferStatus::ERROR;
  int fence_result_ = -1;
};

struct RegistrationWatch {
  std::mutex mutex;
  std::condition_variable condition;
  bool registered = false;
  std::string instance;
};

void OnServiceRegistration(const char* instance, AIBinder*, void* cookie) {
  auto* watch = static_cast<RegistrationWatch*>(cookie);
  if (watch == nullptr) return;
  {
    std::lock_guard<std::mutex> lock(watch->mutex);
    watch->registered = true;
    watch->instance = instance == nullptr ? "" : instance;
  }
  watch->condition.notify_all();
}

int WatchRegistration(const char* instance) {
  RegistrationWatch watch;
  std::printf("watching instance=%s declared=%s\n", instance,
              AServiceManager_isDeclared(instance) ? "true" : "false");
  std::fflush(stdout);
  AServiceManager_NotificationRegistration* registration =
      AServiceManager_registerForServiceNotifications(
          instance, OnServiceRegistration, &watch);
  if (registration == nullptr) {
    std::fprintf(stderr, "Unable to register service notification\n");
    return 15;
  }
  bool registered = false;
  std::string registered_instance;
  {
    std::unique_lock<std::mutex> lock(watch.mutex);
    registered = watch.condition.wait_for(
        lock, std::chrono::seconds(15),
        [&watch] { return watch.registered; });
    registered_instance = watch.instance;
  }
  AServiceManager_NotificationRegistration_delete(registration);
  if (!registered) {
    std::fprintf(stderr, "Timed out waiting for service registration: %s\n",
                 instance);
    return 16;
  }
  std::printf("registration received instance=%s\n",
              registered_instance.c_str());
  return registered_instance == instance ? 0 : 17;
}

}  // namespace

int main(int argc, char** argv) {
  ABinderProcess_setThreadPoolMaxThreadCount(2);
  ABinderProcess_startThreadPool();

  if (argc > 1 && std::string(argv[1]) == "--watch-registration") {
    const char* watched_instance = argc > 2 ? argv[2] : kDefaultInstance;
    return WatchRegistration(watched_instance);
  }

  const char* instance = argc > 1 ? argv[1] : kDefaultInstance;
  ndk::SpAIBinder binder(AServiceManager_checkService(instance));
  if (binder.get() == nullptr) {
    std::fprintf(stderr, "Provider service is unavailable: %s\n", instance);
    return 2;
  }

  auto provider =
      aidl::android::hardware::camera::provider::ICameraProvider::fromBinder(
          binder);
  if (provider == nullptr) {
    std::fprintf(stderr, "Unable to create ICameraProvider proxy\n");
    return 3;
  }

  int32_t version = 0;
  std::string hash;
  if (!PrintStatus("getInterfaceVersion",
                   provider->getInterfaceVersion(&version)) ||
      !PrintStatus("getInterfaceHash", provider->getInterfaceHash(&hash))) {
    return 4;
  }
  std::printf("provider version=%d hash=%s\n", version, hash.c_str());

  std::vector<std::string> camera_ids;
  if (!PrintStatus("getCameraIdList",
                   provider->getCameraIdList(&camera_ids))) {
    return 5;
  }
  std::printf("camera_count=%zu\n", camera_ids.size());

  for (const std::string& camera_id : camera_ids) {
    std::shared_ptr<aidl::android::hardware::camera::device::ICameraDevice>
        device;
    if (!PrintStatus("getCameraDeviceInterface",
                     provider->getCameraDeviceInterface(camera_id, &device)) ||
        device == nullptr) {
      return 6;
    }

    aidl::android::hardware::camera::device::CameraMetadata characteristics;
    if (!PrintStatus("getCameraCharacteristics",
                     device->getCameraCharacteristics(&characteristics))) {
      return 7;
    }
    std::printf("camera id=%s characteristics_bytes=%zu\n", camera_id.c_str(),
                characteristics.metadata.size());

    auto callback = ndk::SharedRefBase::make<ProbeDeviceCallback>();
    std::shared_ptr<camera_device::ICameraDeviceSession> session;
    if (!PrintStatus("open", device->open(callback, &session)) ||
        session == nullptr) {
      return 8;
    }

    camera_device::CameraMetadata preview_settings;
    if (!PrintStatus(
            "constructDefaultRequestSettings",
            session->constructDefaultRequestSettings(
                camera_device::RequestTemplate::PREVIEW,
                &preview_settings))) {
      session->close();
      return 9;
    }
    std::printf("camera id=%s preview_settings_bytes=%zu session=open\n",
                camera_id.c_str(), preview_settings.metadata.size());

    camera_device::Stream stream;
    stream.id = 0;
    stream.streamType = camera_device::StreamType::OUTPUT;
    stream.width = 640;
    stream.height = 480;
    stream.format =
        aidl::android::hardware::graphics::common::PixelFormat::YCBCR_420_888;
    stream.usage = aidl::android::hardware::graphics::common::BufferUsage::
        CPU_READ_OFTEN;
    stream.dataSpace =
        aidl::android::hardware::graphics::common::Dataspace::UNKNOWN;
    stream.rotation = camera_device::StreamRotation::ROTATION_0;
    stream.groupId = -1;
    stream.sensorPixelModesUsed = {
        aidl::android::hardware::camera::metadata::SensorPixelMode::
            ANDROID_SENSOR_PIXEL_MODE_DEFAULT};
    stream.dynamicRangeProfile =
        aidl::android::hardware::camera::metadata::
            RequestAvailableDynamicRangeProfilesMap::
                ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_STANDARD;
    stream.useCase =
        aidl::android::hardware::camera::metadata::
            ScalerAvailableStreamUseCases::
                ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT;
    stream.colorSpace = static_cast<int32_t>(
        aidl::android::hardware::camera::metadata::
            RequestAvailableColorSpaceProfilesMap::
                ANDROID_REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP_UNSPECIFIED);

    camera_device::StreamConfiguration config;
    config.streams = {stream};
    config.operationMode = camera_device::StreamConfigurationMode::NORMAL_MODE;
    config.sessionParams = preview_settings;
    config.streamConfigCounter = 0;
    config.multiResolutionInputImage = false;

    bool supported = false;
    if (!PrintStatus("isStreamCombinationSupported",
                     device->isStreamCombinationSupported(config,
                                                          &supported)) ||
        !supported) {
      std::fprintf(stderr, "640x480 YUV stream is unsupported: %s\n",
                   camera_id.c_str());
      session->close();
      return 10;
    }
    std::vector<camera_device::HalStream> hal_streams;
    if (!PrintStatus("configureStreams",
                     session->configureStreams(config, &hal_streams)) ||
        hal_streams.size() != 1) {
      session->close();
      return 11;
    }
    std::printf(
        "camera id=%s stream=640x480/YUV_420_888 configured max_buffers=%d\n",
        camera_id.c_str(), hal_streams[0].maxBuffers);

    const uint64_t buffer_usage = android_convertGralloc1To0Usage(
        static_cast<uint64_t>(hal_streams[0].producerUsage),
        static_cast<uint64_t>(stream.usage));
    buffer_handle_t buffer = nullptr;
    uint32_t stride = 0;
    const android::status_t allocation =
        android::GraphicBufferAllocator::get().allocate(
            stream.width, stream.height,
            static_cast<int32_t>(hal_streams[0].overrideFormat), 1,
            buffer_usage, &buffer, &stride, "vcam_provider_probe_client");
    if (allocation != android::OK || buffer == nullptr) {
      std::fprintf(stderr, "Buffer allocation failed: %d\n", allocation);
      session->close();
      return 12;
    }

    camera_device::CaptureRequest request;
    request.frameNumber = 1;
    request.settings = preview_settings;
    request.fmqSettingsSize = 0;
    camera_device::StreamBuffer output_buffer;
    output_buffer.streamId = hal_streams[0].id;
    output_buffer.bufferId = 1;
    output_buffer.buffer = android::dupToAidl(buffer);
    output_buffer.status = camera_device::BufferStatus::OK;
    request.outputBuffers.push_back(std::move(output_buffer));
    request.inputBuffer = {
        .streamId = -1,
        .bufferId = 0,
        .status = camera_device::BufferStatus::ERROR,
    };
    int32_t processed = 0;
    std::vector<camera_device::BufferCache> caches_to_remove;
    std::vector<camera_device::CaptureRequest> requests;
    requests.push_back(std::move(request));
    const bool request_ok = PrintStatus(
        "processCaptureRequest",
        session->processCaptureRequest(requests, caches_to_remove, &processed));
    const bool frame_ok =
        request_ok && processed == 1 && callback->WaitForFrame(1);

    bool pixels_ok = false;
    uint8_t min_y = 255;
    uint8_t max_y = 0;
    if (frame_ok) {
      android_ycbcr ycbcr{};
      const android::status_t lock_result =
          android::GraphicBufferMapper::get().lockYCbCr(
              buffer, GRALLOC_USAGE_SW_READ_OFTEN,
              android::Rect(stream.width, stream.height), &ycbcr);
      if (lock_result == android::OK && ycbcr.y != nullptr) {
        const auto* y = static_cast<const uint8_t*>(ycbcr.y);
        for (int32_t row = 0; row < stream.height; row += 32) {
          for (int32_t column = 0; column < stream.width; column += 32) {
            const uint8_t value =
                y[static_cast<size_t>(row) * ycbcr.ystride + column];
            min_y = std::min(min_y, value);
            max_y = std::max(max_y, value);
          }
        }
        pixels_ok = static_cast<int>(max_y) - static_cast<int>(min_y) >= 32;
        android::GraphicBufferMapper::get().unlock(buffer);
      } else {
        std::fprintf(stderr, "Buffer lock failed: %d\n", lock_result);
      }
    }
    android::GraphicBufferAllocator::get().free(buffer);
    if (!frame_ok || !pixels_ok) {
      std::fprintf(stderr,
                   "Captured frame validation failed processed=%d y=[%u,%u]\n",
                   processed, min_y, max_y);
      session->close();
      return 13;
    }
    std::printf("camera id=%s frame=1 received y_range=[%u,%u]\n",
                camera_id.c_str(), min_y, max_y);
    if (!PrintStatus("close", session->close())) return 14;
    std::printf("camera id=%s session=closed\n", camera_id.c_str());
  }

  return 0;
}
