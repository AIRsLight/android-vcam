/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#define LOG_TAG "VcamAidlHwl"

#include <camera_device_hwl.h>
#include <camera_device_session_hwl.h>
#include <camera_provider_hwl.h>
#include <log/log.h>
#include <system/camera_metadata.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "EmulatedCameraProviderHWLImpl.h"
#include "vcam/FrameRenderer.h"
#include "vcam/RouteResolver.h"
#include "vcam/VendorTags.h"

namespace android {
namespace {

namespace gch = google_camera_hal;

constexpr uint32_t kBackCameraId = 1000;
constexpr uint32_t kFrontCameraId = 1001;
constexpr uint32_t kDelegateBackCameraId = 0;
constexpr uint32_t kDelegateFrontCameraId = 1;
constexpr uint64_t kMaxOutputPixelRate = 1920ULL * 1080ULL * 60ULL;
constexpr int64_t kDefaultFrameDurationNs = 33333333;
constexpr int32_t kJpegMaxSize = 16 * 1024 * 1024;

struct RoutedFrameState {
  std::mutex mutex;
  vcam::FrameRenderer renderer;
  vcam::RgbTransform transform;
  std::string package_name;
  std::string provider_id;
  int64_t frame_duration_ns = kDefaultFrameDurationNs;
};

struct ActiveFrame {
  uint32_t camera_id = kDelegateBackCameraId;
  uint32_t frame_number = 0;
  bool valid = false;
};

std::array<RoutedFrameState, 2> g_routed_frames;
thread_local ActiveFrame g_active_frame;

std::string ClientPackageFrom(const gch::HalCameraMetadata* metadata) {
  if (metadata == nullptr) return {};
  camera_metadata_ro_entry_t entry{};
  if (metadata->Get(vcam::kVcamClientPackageTag, &entry) != OK ||
      entry.type != TYPE_BYTE || entry.count == 0 || entry.data.u8 == nullptr) {
    return {};
  }
  const char* value = reinterpret_cast<const char*>(entry.data.u8);
  const size_t length = strnlen(value, entry.count);
  return std::string(value, length);
}

vcam::RgbTransform LoadSourceTransform(const std::string& frame_path,
                                       uint32_t camera_id) {
  vcam::RgbTransform transform;
  const size_t slash = frame_path.find_last_of('/');
  if (slash == std::string::npos) return transform;

  int rotation = 0;
  int scale = 1000;
  int center_x = 500;
  int center_y = 500;
  const std::string path = frame_path.substr(0, slash) + "/view-" +
                           std::to_string(camera_id) + ".cfg";
  FILE* view = fopen(path.c_str(), "re");
  if (view != nullptr) {
    const int parsed =
        fscanf(view, "%d,%d,%d,%d", &rotation, &scale, &center_x, &center_y);
    fclose(view);
    if (parsed != 4 ||
        (rotation != 0 && rotation != 90 && rotation != 180 &&
         rotation != 270) ||
        scale < 100 || scale > 8000 || center_x < 0 || center_x > 1000 ||
        center_y < 0 || center_y > 1000) {
      rotation = 0;
      scale = 1000;
      center_x = 500;
      center_y = 500;
    }
  }

  const int sensor_orientation = camera_id == kDelegateBackCameraId ? 90 : 270;
  int total_rotation = rotation - sensor_orientation;
  while (total_rotation <= -270) total_rotation += 360;
  while (total_rotation > 270) total_rotation -= 360;
  transform.rotationDegrees = total_rotation;
  transform.scale = scale / 1000.0f;
  transform.centerX = center_x / 1000.0f;
  transform.centerY = center_y / 1000.0f;
  return transform;
}

int64_t LoadSourceFrameDuration(const std::string& frame_path) {
  const size_t slash = frame_path.find_last_of('/');
  if (slash == std::string::npos) return kDefaultFrameDurationNs;

  int fps = 30;
  int ignored_width = 0;
  int ignored_height = 0;
  FILE* source = fopen((frame_path.substr(0, slash) + "/source.cfg").c_str(),
                       "re");
  if (source == nullptr) return kDefaultFrameDurationNs;
  const int parsed =
      fscanf(source, "%d,%d,%d", &fps, &ignored_width, &ignored_height);
  fclose(source);
  if (parsed != 3 || fps < 1 || fps > 60) return kDefaultFrameDurationNs;
  return 1000000000LL / fps;
}

status_t ConfigureRoutedFrame(uint32_t camera_id,
                              const gch::HalCameraMetadata* session_params,
                              int64_t* frame_duration_ns) {
  if (camera_id >= g_routed_frames.size()) return BAD_VALUE;
  const std::string package_name = ClientPackageFrom(session_params);
  if (package_name.empty()) {
    const char* probe_pattern = getenv("ANDROID_VCAM_PROBE_TEST_PATTERN");
    if (probe_pattern != nullptr && strcmp(probe_pattern, "1") == 0) {
      RoutedFrameState& state = g_routed_frames[camera_id];
      std::lock_guard<std::mutex> lock(state.mutex);
      state.package_name = "<direct-probe>";
      state.provider_id = "test-pattern";
      state.renderer.setSourcePath({});
      state.transform = {};
      state.frame_duration_ns = kDefaultFrameDurationNs;
      if (frame_duration_ns != nullptr) {
        *frame_duration_ns = state.frame_duration_ns;
      }
      ALOGI("Configured diagnostic test pattern camera=%u", camera_id);
      return OK;
    }
    ALOGE("Client package is absent from VCAM session parameters");
    return BAD_VALUE;
  }
  const vcam::ProviderSelection selection =
      vcam::RouteResolver::resolveProviderForPackage(package_name, camera_id);
  if (!selection.configured || !selection.available ||
      vcam::RouteResolver::physicalIdFromProvider(selection.providerId) >= 0) {
    ALOGE("Invalid virtual route camera=%u package='%s' provider='%s'",
          camera_id, package_name.c_str(), selection.providerId.c_str());
    return BAD_VALUE;
  }

  const std::string frame_path =
      vcam::RouteResolver::framePath(selection.providerId);
  RoutedFrameState& state = g_routed_frames[camera_id];
  std::lock_guard<std::mutex> lock(state.mutex);
  state.package_name = package_name;
  state.provider_id = selection.providerId;
  state.renderer.setSourcePath(frame_path);
  state.transform = LoadSourceTransform(frame_path, camera_id);
  state.frame_duration_ns = LoadSourceFrameDuration(frame_path);
  state.renderer.reload();
  if (frame_duration_ns != nullptr) {
    *frame_duration_ns = state.frame_duration_ns;
  }
  ALOGI("Configured frame route camera=%u package='%s' provider='%s'",
        camera_id, package_name.c_str(), selection.providerId.c_str());
  return OK;
}

bool ToDelegateId(uint32_t public_id, uint32_t* delegate_id) {
  if (delegate_id == nullptr) return false;
  switch (public_id) {
    case kBackCameraId:
      *delegate_id = kDelegateBackCameraId;
      return true;
    case kFrontCameraId:
      *delegate_id = kDelegateFrontCameraId;
      return true;
    default:
      return false;
  }
}

bool ToPublicId(uint32_t delegate_id, uint32_t* public_id) {
  if (public_id == nullptr) return false;
  switch (delegate_id) {
    case kDelegateBackCameraId:
      *public_id = kBackCameraId;
      return true;
    case kDelegateFrontCameraId:
      *public_id = kFrontCameraId;
      return true;
    default:
      return false;
  }
}

status_t AppendMetadataKey(gch::HalCameraMetadata* metadata,
                           uint32_t list_tag, uint32_t key) {
  if (metadata == nullptr) return BAD_VALUE;
  camera_metadata_ro_entry_t entry{};
  std::vector<int32_t> keys;
  if (metadata->Get(list_tag, &entry) == OK) {
    keys.assign(entry.data.i32, entry.data.i32 + entry.count);
  }
  const int32_t signed_key = static_cast<int32_t>(key);
  if (std::find(keys.begin(), keys.end(), signed_key) == keys.end()) {
    keys.push_back(signed_key);
  }
  return metadata->Set(list_tag, keys.data(), keys.size());
}

status_t AddRoutingMetadata(gch::HalCameraMetadata* metadata) {
  status_t result = AppendMetadataKey(
      metadata, ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
      vcam::kVcamClientPackageTag);
  if (result != OK) return result;
  return AppendMetadataKey(metadata, ANDROID_REQUEST_AVAILABLE_SESSION_KEYS,
                           vcam::kVcamClientPackageTag);
}

template <typename T>
status_t AppendMetadataTuples(gch::HalCameraMetadata* metadata, uint32_t tag,
                              const T* additions, size_t addition_count) {
  if (metadata == nullptr || additions == nullptr || addition_count % 4 != 0) {
    return BAD_VALUE;
  }
  camera_metadata_ro_entry_t entry{};
  std::vector<T> values;
  if (metadata->Get(tag, &entry) == OK && entry.count > 0) {
    constexpr uint8_t expected_type =
        std::is_same_v<T, int32_t> ? TYPE_INT32 : TYPE_INT64;
    if (entry.type != expected_type || entry.count % 4 != 0) {
      ALOGE("Unexpected metadata tuple layout tag=%u type=%d count=%zu", tag,
            static_cast<int>(entry.type), entry.count);
      return BAD_VALUE;
    }
    const T* existing = nullptr;
    if constexpr (std::is_same_v<T, int32_t>) {
      existing = entry.data.i32;
    } else {
      existing = entry.data.i64;
    }
    values.assign(existing, existing + entry.count);
  }
  for (size_t offset = 0; offset < addition_count; offset += 4) {
    bool found = false;
    for (size_t current = 0; current + 3 < values.size(); current += 4) {
      if (values[current] == additions[offset] &&
          values[current + 1] == additions[offset + 1] &&
          values[current + 2] == additions[offset + 2]) {
        found = true;
        break;
      }
    }
    if (!found) {
      values.insert(values.end(), additions + offset, additions + offset + 4);
    }
  }
  return metadata->Set(tag, values.data(), values.size());
}

status_t AddHighResolutionMetadata(gch::HalCameraMetadata* metadata) {
  constexpr int32_t output =
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
  constexpr int32_t configs[] = {
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 2560, 1440, output,
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 3840, 2160, output,
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 4096, 3072, output,
      HAL_PIXEL_FORMAT_YCBCR_420_888, 2560, 1440, output,
      HAL_PIXEL_FORMAT_YCBCR_420_888, 3840, 2160, output,
      HAL_PIXEL_FORMAT_YCBCR_420_888, 4096, 3072, output,
      HAL_PIXEL_FORMAT_BLOB, 2560, 1440, output,
      HAL_PIXEL_FORMAT_BLOB, 3840, 2160, output,
      HAL_PIXEL_FORMAT_BLOB, 4096, 3072, output,
  };
  constexpr int64_t min_durations[] = {
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 2560, 1440, 33333333,
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 3840, 2160, 66666666,
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 4096, 3072, 111111111,
      HAL_PIXEL_FORMAT_YCBCR_420_888, 2560, 1440, 33333333,
      HAL_PIXEL_FORMAT_YCBCR_420_888, 3840, 2160, 66666666,
      HAL_PIXEL_FORMAT_YCBCR_420_888, 4096, 3072, 111111111,
      HAL_PIXEL_FORMAT_BLOB, 2560, 1440, 33333333,
      HAL_PIXEL_FORMAT_BLOB, 3840, 2160, 66666666,
      HAL_PIXEL_FORMAT_BLOB, 4096, 3072, 111111111,
  };
  constexpr int64_t stall_durations[] = {
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 2560, 1440, 0,
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 3840, 2160, 0,
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 4096, 3072, 0,
      HAL_PIXEL_FORMAT_YCBCR_420_888, 2560, 1440, 0,
      HAL_PIXEL_FORMAT_YCBCR_420_888, 3840, 2160, 0,
      HAL_PIXEL_FORMAT_YCBCR_420_888, 4096, 3072, 0,
      HAL_PIXEL_FORMAT_BLOB, 2560, 1440, 500000000,
      HAL_PIXEL_FORMAT_BLOB, 3840, 2160, 800000000,
      HAL_PIXEL_FORMAT_BLOB, 4096, 3072, 1200000000,
  };
  status_t result = AppendMetadataTuples(
      metadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, configs,
      std::size(configs));
  if (result != OK) return result;
  result = AppendMetadataTuples(
      metadata, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, min_durations,
      std::size(min_durations));
  if (result != OK) return result;
  result = AppendMetadataTuples(
      metadata, ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stall_durations,
      std::size(stall_durations));
  if (result != OK) return result;
  result = metadata->Set(ANDROID_JPEG_MAX_SIZE, &kJpegMaxSize, 1);
  if (result != OK) return result;
  constexpr int64_t max_frame_duration = 120000000;
  return metadata->Set(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
                       &max_frame_duration, 1);
}

class VcamCameraDeviceSessionHwl final : public gch::CameraDeviceSessionHwl {
 public:
  VcamCameraDeviceSessionHwl(
      uint32_t public_id, uint32_t delegate_id,
      std::unique_ptr<gch::CameraDeviceSessionHwl> delegate)
      : public_id_(public_id),
        delegate_id_(delegate_id),
        delegate_(std::move(delegate)) {}

  status_t ConstructDefaultRequestSettings(
      gch::RequestTemplate type,
      std::unique_ptr<gch::HalCameraMetadata>* settings) override {
    return delegate_->ConstructDefaultRequestSettings(type, settings);
  }

  status_t PrepareConfigureStreams(
      const gch::StreamConfiguration& config) override {
    int64_t source_frame_duration = kDefaultFrameDurationNs;
    status_t route_result = ConfigureRoutedFrame(
        delegate_id_, config.session_params.get(), &source_frame_duration);
    if (route_result != OK) return route_result;
    uint64_t max_output_pixels = 1;
    for (const auto& stream : config.streams) {
      if (stream.stream_type == gch::StreamType::kOutput) {
        max_output_pixels = std::max(
            max_output_pixels,
            static_cast<uint64_t>(stream.width) * stream.height);
      }
    }
    const int64_t output_fps = static_cast<int64_t>(std::max<uint64_t>(
        1, std::min<uint64_t>(60, kMaxOutputPixelRate / max_output_pixels)));
    frame_duration_ns_ =
        std::max<int64_t>(source_frame_duration, 1000000000LL / output_fps);
    next_frame_time_ = {};
    ALOGI("Configured AIDL route camera=%u outputFps=%lld",
          public_id_, static_cast<long long>(1000000000LL / frame_duration_ns_));
    return delegate_->PrepareConfigureStreams(config);
  }

  status_t ConfigurePipeline(
      uint32_t camera_id, gch::HwlPipelineCallback callback,
      const gch::StreamConfiguration& request_config,
      const gch::StreamConfiguration& overall_config,
      uint32_t* pipeline_id) override {
    if (camera_id != public_id_) return BAD_VALUE;
    if (callback.process_pipeline_result) {
      auto process_result = std::move(callback.process_pipeline_result);
      callback.process_pipeline_result =
          [process_result = std::move(process_result), public_id = public_id_](
              std::unique_ptr<gch::HwlPipelineResult> result) mutable {
            if (result != nullptr) result->camera_id = public_id;
            process_result(std::move(result));
          };
    }
    return delegate_->ConfigurePipeline(delegate_id_, std::move(callback),
                                        request_config, overall_config,
                                        pipeline_id);
  }

  status_t BuildPipelines() override { return delegate_->BuildPipelines(); }

  status_t PreparePipeline(uint32_t pipeline_id,
                           uint32_t frame_number) override {
    return delegate_->PreparePipeline(pipeline_id, frame_number);
  }

  status_t GetRequiredIntputStreams(
      const gch::StreamConfiguration& config,
      gch::HwlOfflinePipelineRole role,
      std::vector<gch::Stream>* streams) override {
    return delegate_->GetRequiredIntputStreams(config, role, streams);
  }

  status_t GetConfiguredHalStream(
      uint32_t pipeline_id,
      std::vector<gch::HalStream>* streams) const override {
    return delegate_->GetConfiguredHalStream(pipeline_id, streams);
  }

  void DestroyPipelines() override { delegate_->DestroyPipelines(); }

  status_t SubmitRequests(
      uint32_t frame_number,
      std::vector<gch::HwlPipelineRequest>& requests) override {
    {
      std::unique_lock<std::mutex> lock(pacing_mutex_);
      auto now = std::chrono::steady_clock::now();
      if (next_frame_time_ > now) {
        std::this_thread::sleep_until(next_frame_time_);
        now = std::chrono::steady_clock::now();
      }
      next_frame_time_ =
          now + std::chrono::nanoseconds(frame_duration_ns_);
    }
    for (auto& request : requests) {
      if (request.settings != nullptr) {
        request.settings->Set(ANDROID_SENSOR_FRAME_DURATION,
                              &frame_duration_ns_, 1);
      }
    }
    return delegate_->SubmitRequests(frame_number, requests);
  }

  status_t Flush() override { return delegate_->Flush(); }

  uint32_t GetCameraId() const override { return public_id_; }

  std::vector<uint32_t> GetPhysicalCameraIds() const override { return {}; }

  status_t GetCameraCharacteristics(
      std::unique_ptr<gch::HalCameraMetadata>* characteristics) const override {
    status_t result = delegate_->GetCameraCharacteristics(characteristics);
    if (result != OK || characteristics == nullptr ||
        *characteristics == nullptr) {
      return result;
    }
    return AddRoutingMetadata(characteristics->get());
  }

  status_t GetPhysicalCameraCharacteristics(
      uint32_t, std::unique_ptr<gch::HalCameraMetadata>*) const override {
    return NAME_NOT_FOUND;
  }

  status_t SetSessionData(gch::SessionDataKey key, void* value) override {
    return delegate_->SetSessionData(key, value);
  }

  status_t GetSessionData(gch::SessionDataKey key,
                          void** value) const override {
    return delegate_->GetSessionData(key, value);
  }

  void SetSessionCallback(
      const gch::HwlSessionCallback& callback) override {
    delegate_->SetSessionCallback(callback);
  }

  status_t FilterResultMetadata(
      gch::HalCameraMetadata* metadata) const override {
    return delegate_->FilterResultMetadata(metadata);
  }

  std::unique_ptr<gch::IMulticamCoordinatorHwl>
  CreateMulticamCoordinatorHwl() override {
    return delegate_->CreateMulticamCoordinatorHwl();
  }

  status_t IsReconfigurationRequired(
      const gch::HalCameraMetadata* old_session,
      const gch::HalCameraMetadata* new_session,
      bool* required) const override {
    return delegate_->IsReconfigurationRequired(old_session, new_session,
                                                 required);
  }

  std::unique_ptr<gch::ZoomRatioMapperHwl> GetZoomRatioMapperHwl() override {
    return delegate_->GetZoomRatioMapperHwl();
  }

  int GetMaxSupportedConcurrentCameras() const override {
    return delegate_->GetMaxSupportedConcurrentCameras();
  }

  std::unique_ptr<google::camera_common::Profiler> GetProfiler(
      uint32_t camera_id, int option) override {
    if (camera_id != public_id_) return nullptr;
    return delegate_->GetProfiler(delegate_id_, option);
  }

  void RemoveCachedBuffers(const native_handle_t* handle) override {
    delegate_->RemoveCachedBuffers(handle);
  }

 private:
  const uint32_t public_id_;
  const uint32_t delegate_id_;
  std::unique_ptr<gch::CameraDeviceSessionHwl> delegate_;
  int64_t frame_duration_ns_ = kDefaultFrameDurationNs;
  std::mutex pacing_mutex_;
  std::chrono::steady_clock::time_point next_frame_time_{};
};

class VcamCameraDeviceHwl final : public gch::CameraDeviceHwl {
 public:
  VcamCameraDeviceHwl(uint32_t public_id, uint32_t delegate_id,
                      std::unique_ptr<gch::CameraDeviceHwl> delegate)
      : public_id_(public_id),
        delegate_id_(delegate_id),
        delegate_(std::move(delegate)) {}

  uint32_t GetCameraId() const override { return public_id_; }

  status_t GetResourceCost(gch::CameraResourceCost* cost) const override {
    status_t result = delegate_->GetResourceCost(cost);
    if (result != OK || cost == nullptr) return result;
    for (uint32_t& conflict : cost->conflicting_devices) {
      uint32_t public_id = 0;
      if (ToPublicId(conflict, &public_id)) conflict = public_id;
    }
    return OK;
  }

  status_t GetCameraCharacteristics(
      std::unique_ptr<gch::HalCameraMetadata>* characteristics) const override {
    status_t result = delegate_->GetCameraCharacteristics(characteristics);
    if (result != OK || characteristics == nullptr ||
        *characteristics == nullptr) {
      return result;
    }
    return AddRoutingMetadata(characteristics->get());
  }

  status_t GetPhysicalCameraCharacteristics(
      uint32_t, std::unique_ptr<gch::HalCameraMetadata>*) const override {
    return NAME_NOT_FOUND;
  }

  status_t SetTorchMode(gch::TorchMode mode) override {
    return delegate_->SetTorchMode(mode);
  }

  status_t TurnOnTorchWithStrengthLevel(int32_t strength) override {
    return delegate_->TurnOnTorchWithStrengthLevel(strength);
  }

  status_t GetTorchStrengthLevel(int32_t& strength) const override {
    return delegate_->GetTorchStrengthLevel(strength);
  }

  status_t DumpState(int fd) override { return delegate_->DumpState(fd); }

  status_t CreateCameraDeviceSessionHwl(
      gch::CameraBufferAllocatorHwl* allocator,
      std::unique_ptr<gch::CameraDeviceSessionHwl>* session) override {
    if (session == nullptr) return BAD_VALUE;
    std::unique_ptr<gch::CameraDeviceSessionHwl> delegate_session;
    status_t result = delegate_->CreateCameraDeviceSessionHwl(
        allocator, &delegate_session);
    if (result != OK) return result;
    if (delegate_session == nullptr) return NO_INIT;
    *session = std::make_unique<VcamCameraDeviceSessionHwl>(
        public_id_, delegate_id_, std::move(delegate_session));
    return OK;
  }

  bool IsStreamCombinationSupported(
      const gch::StreamConfiguration& config) override {
    return delegate_->IsStreamCombinationSupported(config);
  }

  std::unique_ptr<google::camera_common::Profiler> GetProfiler(
      uint32_t camera_id, int option) override {
    if (camera_id != public_id_) return nullptr;
    return delegate_->GetProfiler(delegate_id_, option);
  }

 private:
  const uint32_t public_id_;
  const uint32_t delegate_id_;
  std::unique_ptr<gch::CameraDeviceHwl> delegate_;
};

class VcamCameraProviderHwl final : public gch::CameraProviderHwl {
 public:
  explicit VcamCameraProviderHwl(
      std::unique_ptr<gch::CameraProviderHwl> delegate)
      : delegate_(std::move(delegate)) {}

  status_t SetCallback(const gch::HwlCameraProviderCallback& callback) override {
    delegate_callback_ = {
        .camera_device_status_change =
            [callback](uint32_t delegate_id, gch::CameraDeviceStatus status) {
              uint32_t public_id = 0;
              if (ToPublicId(delegate_id, &public_id) &&
                  callback.camera_device_status_change) {
                callback.camera_device_status_change(public_id, status);
              }
            },
        .physical_camera_device_status_change =
            [](uint32_t, uint32_t, gch::CameraDeviceStatus) {},
        .torch_mode_status_change =
            [callback](uint32_t delegate_id, gch::TorchModeStatus status) {
              uint32_t public_id = 0;
              if (ToPublicId(delegate_id, &public_id) &&
                  callback.torch_mode_status_change) {
                callback.torch_mode_status_change(public_id, status);
              }
            },
    };
    return delegate_->SetCallback(delegate_callback_);
  }

  status_t TriggerDeferredCallbacks() override {
    return delegate_->TriggerDeferredCallbacks();
  }

  status_t GetVendorTags(
      std::vector<gch::VendorTagSection>* sections) override {
    if (sections == nullptr) return BAD_VALUE;
    status_t result = delegate_->GetVendorTags(sections);
    if (result != OK) return result;
    gch::VendorTagSection section;
    section.section_name = vcam::kVcamClientPackageSection;
    section.tags.push_back({
        .tag_id = vcam::kVcamClientPackageTag,
        .tag_name = vcam::kVcamClientPackageName,
        .tag_type = gch::CameraMetadataType::kByte,
    });
    sections->push_back(std::move(section));
    return OK;
  }

  status_t GetVisibleCameraIds(std::vector<uint32_t>* camera_ids) override {
    if (camera_ids == nullptr) return BAD_VALUE;
    std::vector<uint32_t> delegate_ids;
    status_t result = delegate_->GetVisibleCameraIds(&delegate_ids);
    if (result != OK) return result;
    camera_ids->clear();
    for (uint32_t delegate_id : delegate_ids) {
      uint32_t public_id = 0;
      if (ToPublicId(delegate_id, &public_id)) {
        camera_ids->push_back(public_id);
      }
    }
    return OK;
  }

  bool IsSetTorchModeSupported() override {
    return delegate_->IsSetTorchModeSupported();
  }

  status_t GetConcurrentStreamingCameraIds(
      std::vector<std::unordered_set<uint32_t>>* combinations) override {
    if (combinations == nullptr) return BAD_VALUE;
    combinations->clear();
    return OK;
  }

  status_t IsConcurrentStreamCombinationSupported(
      const std::vector<gch::CameraIdAndStreamConfiguration>&,
      bool* supported) override {
    if (supported == nullptr) return BAD_VALUE;
    *supported = false;
    return OK;
  }

  status_t CreateCameraDeviceHwl(
      uint32_t public_id,
      std::unique_ptr<gch::CameraDeviceHwl>* camera_device) override {
    if (camera_device == nullptr) return BAD_VALUE;
    uint32_t delegate_id = 0;
    if (!ToDelegateId(public_id, &delegate_id)) return NAME_NOT_FOUND;
    std::unique_ptr<gch::CameraDeviceHwl> delegate_device;
    status_t result =
        delegate_->CreateCameraDeviceHwl(delegate_id, &delegate_device);
    if (result != OK) return result;
    if (delegate_device == nullptr) return NO_INIT;
    *camera_device = std::make_unique<VcamCameraDeviceHwl>(
        public_id, delegate_id, std::move(delegate_device));
    return OK;
  }

  status_t CreateBufferAllocatorHwl(
      std::unique_ptr<gch::CameraBufferAllocatorHwl>* allocator) override {
    return delegate_->CreateBufferAllocatorHwl(allocator);
  }

  status_t NotifyDeviceStateChange(gch::DeviceState state) override {
    return delegate_->NotifyDeviceStateChange(state);
  }

 private:
  std::unique_ptr<gch::CameraProviderHwl> delegate_;
  gch::HwlCameraProviderCallback delegate_callback_;
};

}  // namespace

extern "C" void VcamSetActiveFrame(uint32_t camera_id,
                                    uint32_t frame_number) {
  g_active_frame = {
      .camera_id = camera_id,
      .frame_number = frame_number,
      .valid = camera_id < g_routed_frames.size(),
  };
}

extern "C" bool VcamAdjustCameraMetadata(
    uint32_t camera_id, gch::HalCameraMetadata* metadata) {
  if (metadata == nullptr) return false;
  if (camera_id > kDelegateFrontCameraId) return true;
  // Some emulator front-camera configurations advertise HIDL 3.5 HAL buffer
  // management even though this standalone AIDL provider has no vendor buffer
  // allocator. Leaving the imported flag in place makes CameraService switch
  // allocation models for only one of the two virtual cameras.
  if (metadata->Erase(ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION) != OK) {
    return false;
  }
  return AddHighResolutionMetadata(metadata) == OK;
}

extern "C" bool VcamRenderYuv420(uint8_t* y, uint8_t* cb, uint8_t* cr,
                                  uint32_t width, uint32_t height,
                                  uint32_t y_stride, uint32_t cbcr_stride,
                                  uint32_t cbcr_step, size_t bytes_per_pixel) {
  if (!g_active_frame.valid || bytes_per_pixel != 1 || y == nullptr ||
      cb == nullptr || cr == nullptr) {
    return false;
  }
  RoutedFrameState& state = g_routed_frames[g_active_frame.camera_id];
  std::lock_guard<std::mutex> lock(state.mutex);
  state.renderer.reload();
  return state.renderer.fillYuv420(
      width, height,
      {.y = y,
       .cb = cb,
       .cr = cr,
       .yStride = y_stride,
       .cStride = cbcr_stride,
       .chromaStep = cbcr_step,
       .yStep = bytes_per_pixel},
      g_active_frame.frame_number, g_active_frame.camera_id, state.transform);
}

extern "C" bool VcamRenderRgb(uint8_t* image, uint32_t width,
                               uint32_t height, uint32_t stride,
                               uint32_t layout) {
  if (!g_active_frame.valid || image == nullptr || width == 0 || height == 0 ||
      layout > 2) {
    return false;
  }
  const uint32_t bytes_per_pixel = layout == 0 ? 3 : 4;
  if (stride < width * bytes_per_pixel) return false;

  RoutedFrameState& state = g_routed_frames[g_active_frame.camera_id];
  std::lock_guard<std::mutex> lock(state.mutex);
  state.renderer.reload();
  std::vector<uint8_t> row(static_cast<size_t>(width) * 3);
  for (uint32_t y = 0; y < height; ++y) {
    if (!state.renderer.fillRgbRow(width, height, y, row.data(),
                                   g_active_frame.frame_number,
                                   g_active_frame.camera_id, state.transform)) {
      return false;
    }
    uint8_t* output = image + static_cast<size_t>(y) * stride;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t* rgb = row.data() + static_cast<size_t>(x) * 3;
      if (layout == 0) {
        memcpy(output + static_cast<size_t>(x) * 3, rgb, 3);
      } else if (layout == 1) {
        uint8_t* rgba = output + static_cast<size_t>(x) * 4;
        rgba[0] = rgb[0];
        rgba[1] = rgb[1];
        rgba[2] = rgb[2];
        rgba[3] = 255;
      } else {
        uint8_t* argb = output + static_cast<size_t>(x) * 4;
        argb[0] = 255;
        argb[1] = rgb[0];
        argb[2] = rgb[1];
        argb[3] = rgb[2];
      }
    }
  }
  return true;
}

extern "C" gch::CameraProviderHwl* CreateCameraProviderHwl() {
  std::unique_ptr<gch::CameraProviderHwl> delegate =
      EmulatedCameraProviderHwlImpl::Create();
  if (delegate == nullptr) {
    ALOGE("Unable to create the upstream EmulatedCamera HWL");
    return nullptr;
  }
  return new VcamCameraProviderHwl(std::move(delegate));
}

}  // namespace android
