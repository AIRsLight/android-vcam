/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "vcam/PatternGenerator.h"

namespace vcam {

struct RgbTransform {
    int rotationDegrees = 0;
    float scale = 1.0f;
    float centerX = 0.5f;
    float centerY = 0.5f;
};

// Decoder for VCAMRGB1 packed RGB888 frames and VCAMYUV1 planar I420 frames.
// Video providers prefer I420 so the HAL can write camera YUV buffers without
// converting every pixel from RGB first. RGB remains supported for upgrades
// and legacy WebUI publishers.
class RgbFrame final {
  public:
    static constexpr size_t kHeaderSize = 24;
    static constexpr uint32_t kMaxDimension = 4096;
    static constexpr uint64_t kMaxPixels = 4096ULL * 3072ULL;

    bool load(const uint8_t* bytes, size_t size);
    bool reloadIfChanged(const char* path);

    bool valid() const { return !pixels_.empty(); }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t sequence() const { return sequence_; }

    bool fillYuv420(uint32_t width, uint32_t height,
                    const Yuv420Layout& layout,
                    const RgbTransform& transform = {}) const;
    bool fillRgbRow(uint32_t width, uint32_t height, uint32_t row,
                    uint8_t* rgb, const RgbTransform& transform = {}) const;

  private:
    enum class PixelFormat {
        kNone,
        kRgb888,
        kI420,
    };

    struct Rgb {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    struct Yuv {
        uint8_t y;
        uint8_t cb;
        uint8_t cr;
    };

    static uint32_t readLe32(const uint8_t* bytes);
    static uint8_t clamp(int value);
    static PixelFormat formatFromMagic(const uint8_t* bytes);
    static uint64_t expectedPayload(PixelFormat format, uint32_t width,
                                    uint32_t height);
    static Rgb toRgb(Yuv value);
    bool mapCoordinate(uint32_t targetWidth, uint32_t targetHeight,
                       uint32_t x, uint32_t y, const RgbTransform& transform,
                       uint32_t* sourceX, uint32_t* sourceY) const;
    Yuv sampleYuv(uint32_t targetWidth, uint32_t targetHeight,
                  uint32_t x, uint32_t y, const RgbTransform& transform) const;
    Rgb sampleRgb(uint32_t targetWidth, uint32_t targetHeight,
                  uint32_t x, uint32_t y, const RgbTransform& transform) const;
    void reset();

    PixelFormat format_ = PixelFormat::kNone;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t sequence_ = 0;
    std::vector<uint8_t> pixels_;
};

}  // namespace vcam
