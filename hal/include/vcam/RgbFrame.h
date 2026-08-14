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

// Decoder for the deliberately small VCAMRGB1 interchange format written by
// the manager app. Pixels are packed RGB888, top-to-bottom and left-to-right.
class RgbFrame final {
  public:
    static constexpr size_t kHeaderSize = 24;
    static constexpr uint32_t kMaxDimension = 4096;

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
    struct Rgb {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    static uint32_t readLe32(const uint8_t* bytes);
    static uint8_t clamp(int value);
    Rgb sample(uint32_t targetWidth, uint32_t targetHeight,
               uint32_t x, uint32_t y, const RgbTransform& transform) const;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t sequence_ = 0;
    std::vector<uint8_t> pixels_;
};

}  // namespace vcam
