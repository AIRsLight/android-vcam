/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <array>
#include <mutex>
#include <string>
#include <vector>

#include <hardware/camera3.h>
#include <hardware/camera_common.h>
#include <hardware/gralloc.h>
#include <hardware/gralloc1.h>
#include <system/camera_metadata.h>

#include "vcam/FrameRenderer.h"

namespace vcam {

class VirtualCamera final {
  public:
    explicit VirtualCamera(int id);
    ~VirtualCamera();

    VirtualCamera(const VirtualCamera&) = delete;
    VirtualCamera& operator=(const VirtualCamera&) = delete;

    int open(const hw_module_t* module, hw_device_t** device);
    int getInfo(camera_info* info);
    void setSourcePath(std::string path);

  private:
    static int closeDevice(hw_device_t* device);
    static int initialize(const camera3_device_t* device,
                          const camera3_callback_ops_t* callbacks);
    static int configureStreams(const camera3_device_t* device,
                                camera3_stream_configuration_t* streams);
    static const camera_metadata_t* constructDefaultRequestSettings(
            const camera3_device_t* device, int type);
    static int processCaptureRequest(const camera3_device_t* device,
                                     camera3_capture_request_t* request);
    static void dump(const camera3_device_t* device, int fd);
    static int flush(const camera3_device_t* device);

    static VirtualCamera* self(const camera3_device_t* device);

    int closeLocked();
    int initializeLocked(const camera3_callback_ops_t* callbacks);
    int configureStreamsLocked(camera3_stream_configuration_t* streams);
    void loadSourceConfigurationLocked();
    const camera_metadata_t* defaultRequestLocked(int type);
    int processRequestLocked(camera3_capture_request_t* request);
    void dumpLocked(int fd) const;

    bool fillBuffer(const camera3_stream_buffer_t& input,
                    camera3_stream_buffer_t* output, uint64_t frameNumber);
    bool fillYuv(camera3_stream_buffer_t* buffer, uint64_t frameNumber);
    bool fillJpeg(camera3_stream_buffer_t* buffer, uint64_t frameNumber);
    void notifyShutter(uint32_t frameNumber, uint64_t timestamp) const;
    void notifyBufferError(uint32_t frameNumber, camera3_stream_t* stream) const;

    camera_metadata_t* buildStaticMetadata() const;
    camera_metadata_t* buildDefaultRequest(int type) const;
    camera_metadata_t* buildResultMetadata(uint64_t timestamp) const;

    static camera3_device_ops_t operations_;

    const int id_;
    mutable std::mutex mutex_;
    camera3_device_t device_{};
    bool open_ = false;
    bool configured_ = false;
    bool layoutLogged_ = false;
    bool requestLogged_ = false;
    bool bufferErrorLogged_ = false;
    bool resultLogged_ = false;
    uint64_t lastFrameTimestampNs_ = 0;
    const camera3_callback_ops_t* callbacks_ = nullptr;
    const gralloc_module_t* gralloc_ = nullptr;
    gralloc1_device_t* gralloc1_ = nullptr;
    GRALLOC1_PFN_GET_NUM_FLEX_PLANES getNumFlexPlanes_ = nullptr;
    GRALLOC1_PFN_LOCK lock1_ = nullptr;
    GRALLOC1_PFN_LOCK_FLEX lockFlex1_ = nullptr;
    GRALLOC1_PFN_UNLOCK unlock1_ = nullptr;
    camera_metadata_t* staticMetadata_ = nullptr;
    camera_metadata_t* lastSettings_ = nullptr;
    std::array<camera_metadata_t*, CAMERA3_TEMPLATE_COUNT> templates_{};
    std::vector<camera3_stream_t*> streams_;
    FrameRenderer frameRenderer_;
    RgbTransform sourceTransform_{};
    int64_t sourceFrameDurationNs_ = 33333333;
};

}  // namespace vcam
