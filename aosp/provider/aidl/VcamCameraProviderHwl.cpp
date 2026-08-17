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
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include "EmulatedCameraProviderHWLImpl.h"
#include "vcam/VendorTags.h"

namespace android {
namespace {

namespace gch = google_camera_hal;

constexpr uint32_t kBackCameraId = 1000;
constexpr uint32_t kFrontCameraId = 1001;
constexpr uint32_t kDelegateBackCameraId = 0;
constexpr uint32_t kDelegateFrontCameraId = 1;

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
    *camera_ids = {kBackCameraId, kFrontCameraId};
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
