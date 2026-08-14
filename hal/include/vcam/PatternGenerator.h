/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace vcam {

struct Yuv420Layout {
    uint8_t* y = nullptr;
    uint8_t* cb = nullptr;
    uint8_t* cr = nullptr;
    size_t yStride = 0;
    size_t cStride = 0;
    size_t chromaStep = 0;
    size_t yStep = 1;
};

class PatternGenerator final {
  public:
    // Produces moving SMPTE-like color bars in full-range BT.601 YUV.
    static bool fillYuv420(uint32_t width, uint32_t height,
                           uint64_t frameNumber, int cameraId,
                           const Yuv420Layout& layout);

    // Produces one RGB scanline used by the JPEG encoder.
    static void fillRgbRow(uint32_t width, uint32_t height, uint32_t row,
                           uint64_t frameNumber, int cameraId,
                           uint8_t* rgb);

  private:
    struct Rgb {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    static Rgb colorAt(uint32_t width, uint32_t height, uint32_t x,
                       uint32_t y, uint64_t frameNumber, int cameraId);
    static uint8_t clamp(int value);
};

}  // namespace vcam
