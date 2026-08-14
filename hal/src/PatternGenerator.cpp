/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include "vcam/PatternGenerator.h"

#include <algorithm>

namespace vcam {

uint8_t PatternGenerator::clamp(int value) {
    return static_cast<uint8_t>(std::max(0, std::min(255, value)));
}

PatternGenerator::Rgb PatternGenerator::colorAt(
        uint32_t width, uint32_t height, uint32_t x, uint32_t y,
        uint64_t frameNumber, int cameraId) {
    constexpr Rgb kBars[] = {
            {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
            {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0},
    };
    const uint32_t shiftedX = width == 0
            ? 0
            : (x + static_cast<uint32_t>((frameNumber * 3) % width)) % width;
    const size_t bar = width == 0 ? 0 : std::min<size_t>(7, shiftedX * 8 / width);
    Rgb color = kBars[bar];

    // A moving gray strip makes frozen-frame failures obvious during bring-up.
    const uint32_t stripHeight = std::max<uint32_t>(1, height / 12);
    const uint32_t stripTop = height == 0
            ? 0
            : static_cast<uint32_t>((frameNumber * 2) % height);
    if (y >= stripTop && y < std::min(height, stripTop + stripHeight)) {
        const uint8_t gray = static_cast<uint8_t>((x + frameNumber) & 0xff);
        color = {gray, gray, gray};
    }

    // Distinguish front/back devices without depending on text rendering.
    if (cameraId == 1 && x < width / 16) {
        color = {255, 96, 32};
    }
    return color;
}

bool PatternGenerator::fillYuv420(uint32_t width, uint32_t height,
                                  uint64_t frameNumber, int cameraId,
                                  const Yuv420Layout& layout) {
    if (width == 0 || height == 0 || layout.y == nullptr ||
        layout.cb == nullptr || layout.cr == nullptr ||
        layout.yStep == 0 ||
        layout.yStride < (width - 1) * layout.yStep + 1 ||
        layout.cStride == 0 ||
        layout.chromaStep == 0) {
        return false;
    }

    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* yRow = layout.y + y * layout.yStride;
        for (uint32_t x = 0; x < width; ++x) {
            const Rgb rgb = colorAt(width, height, x, y, frameNumber, cameraId);
            const int luma = ((77 * rgb.r + 150 * rgb.g + 29 * rgb.b) >> 8);
            yRow[x * layout.yStep] = clamp(luma);
        }
    }

    for (uint32_t y = 0; y < height / 2; ++y) {
        uint8_t* cbRow = layout.cb + y * layout.cStride;
        uint8_t* crRow = layout.cr + y * layout.cStride;
        for (uint32_t x = 0; x < width / 2; ++x) {
            const Rgb rgb = colorAt(width, height, x * 2, y * 2,
                                    frameNumber, cameraId);
            const int cb = 128 + ((-43 * rgb.r - 85 * rgb.g + 128 * rgb.b) >> 8);
            const int cr = 128 + ((128 * rgb.r - 107 * rgb.g - 21 * rgb.b) >> 8);
            cbRow[x * layout.chromaStep] = clamp(cb);
            crRow[x * layout.chromaStep] = clamp(cr);
        }
    }
    return true;
}

void PatternGenerator::fillRgbRow(uint32_t width, uint32_t height,
                                  uint32_t row, uint64_t frameNumber,
                                  int cameraId, uint8_t* rgb) {
    if (rgb == nullptr) {
        return;
    }
    for (uint32_t x = 0; x < width; ++x) {
        const Rgb value = colorAt(width, height, x, row, frameNumber, cameraId);
        rgb[x * 3] = value.r;
        rgb[x * 3 + 1] = value.g;
        rgb[x * 3 + 2] = value.b;
    }
}

}  // namespace vcam
