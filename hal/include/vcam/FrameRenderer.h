/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "vcam/RgbFrame.h"

namespace vcam {

// Transport-independent frame renderer shared by the legacy camera_module,
// HIDL provider and AIDL provider frontends. It deliberately knows nothing
// about camera3 buffers, Binder or gralloc handles.
class FrameRenderer final {
  public:
    void setSourcePath(std::string path);
    const std::string& sourcePath() const { return sourcePath_; }

    bool reload();
    bool loadFrame(const uint8_t* bytes, size_t size);
    bool hasSourceFrame() const { return sourceFrame_.valid(); }

    bool fillYuv420(uint32_t width, uint32_t height,
                    const Yuv420Layout& layout, uint64_t frameNumber,
                    int cameraId, const RgbTransform& transform = {}) const;
    bool fillRgbRow(uint32_t width, uint32_t height, uint32_t row,
                    uint8_t* rgb, uint64_t frameNumber, int cameraId,
                    const RgbTransform& transform = {}) const;

  private:
    RgbFrame sourceFrame_;
    std::string sourcePath_ = "/data/vendor/camera/vcam/source.rgb";
};

}  // namespace vcam
