/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include "vcam/FrameRenderer.h"

#include <utility>

#include "vcam/PatternGenerator.h"

namespace vcam {

void FrameRenderer::setSourcePath(std::string path) {
    sourcePath_ = std::move(path);
    sourceFrame_ = RgbFrame{};
}

bool FrameRenderer::reload() {
    return sourceFrame_.reloadIfChanged(sourcePath_.c_str());
}

bool FrameRenderer::loadFrame(const uint8_t* bytes, size_t size) {
    return sourceFrame_.load(bytes, size);
}

bool FrameRenderer::fillYuv420(uint32_t width, uint32_t height,
                               const Yuv420Layout& layout,
                               uint64_t frameNumber, int cameraId,
                               const RgbTransform& transform) const {
    return sourceFrame_.valid()
            ? sourceFrame_.fillYuv420(width, height, layout, transform)
            : PatternGenerator::fillYuv420(
                    width, height, frameNumber, cameraId, layout);
}

bool FrameRenderer::fillRgbRow(uint32_t width, uint32_t height, uint32_t row,
                               uint8_t* rgb, uint64_t frameNumber, int cameraId,
                               const RgbTransform& transform) const {
    if (sourceFrame_.valid()) {
        return sourceFrame_.fillRgbRow(width, height, row, rgb, transform);
    }
    PatternGenerator::fillRgbRow(
            width, height, row, frameNumber, cameraId, rgb);
    return true;
}

}  // namespace vcam
