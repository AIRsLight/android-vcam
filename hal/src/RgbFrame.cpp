/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include "vcam/RgbFrame.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <limits>

namespace vcam {
namespace {

constexpr uint8_t kMagic[8] = {'V', 'C', 'A', 'M', 'R', 'G', 'B', '1'};

}  // namespace

uint32_t RgbFrame::readLe32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint8_t RgbFrame::clamp(int value) {
    return static_cast<uint8_t>(std::max(0, std::min(255, value)));
}

bool RgbFrame::load(const uint8_t* bytes, size_t size) {
    if (bytes == nullptr || size < kHeaderSize ||
        memcmp(bytes, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }

    const uint32_t width = readLe32(bytes + 8);
    const uint32_t height = readLe32(bytes + 12);
    const uint32_t payloadSize = readLe32(bytes + 16);
    const uint32_t sequence = readLe32(bytes + 20);
    if (width == 0 || height == 0 || width > kMaxDimension ||
        height > kMaxDimension) {
        return false;
    }

    const uint64_t expected = static_cast<uint64_t>(width) * height * 3;
    if (expected > std::numeric_limits<uint32_t>::max() ||
        payloadSize != expected || size != kHeaderSize + expected) {
        return false;
    }

    std::vector<uint8_t> replacement(bytes + kHeaderSize, bytes + size);
    width_ = width;
    height_ = height;
    sequence_ = sequence;
    pixels_ = std::move(replacement);
    return true;
}

bool RgbFrame::reloadIfChanged(const char* path) {
    if (path == nullptr) return valid();
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        width_ = 0;
        height_ = 0;
        sequence_ = 0;
        pixels_.clear();
        return false;
    }

    uint8_t header[kHeaderSize];
    const bool headerRead = fread(header, 1, sizeof(header), file) == sizeof(header);
    if (!headerRead || memcmp(header, kMagic, sizeof(kMagic)) != 0) {
        fclose(file);
        return valid();
    }

    const uint32_t width = readLe32(header + 8);
    const uint32_t height = readLe32(header + 12);
    const uint32_t payloadSize = readLe32(header + 16);
    const uint32_t sequence = readLe32(header + 20);
    if (valid() && width == width_ && height == height_ && sequence == sequence_) {
        fclose(file);
        return true;
    }

    if (width == 0 || height == 0 || width > kMaxDimension ||
        height > kMaxDimension ||
        static_cast<uint64_t>(width) * height * 3 != payloadSize) {
        fclose(file);
        return valid();
    }

    std::vector<uint8_t> bytes(kHeaderSize + payloadSize);
    memcpy(bytes.data(), header, kHeaderSize);
    const bool payloadRead = fread(bytes.data() + kHeaderSize, 1, payloadSize, file) ==
            payloadSize;
    const int trailing = fgetc(file);
    fclose(file);
    if (!payloadRead || trailing != EOF) return valid();
    return load(bytes.data(), bytes.size()) || valid();
}

RgbFrame::Rgb RgbFrame::sample(uint32_t targetWidth, uint32_t targetHeight,
                               uint32_t x, uint32_t y,
                               const RgbTransform& transform) const {
    const int rotationDegrees = transform.rotationDegrees;
    uint32_t logicalWidth = targetWidth;
    uint32_t logicalHeight = targetHeight;
    uint32_t logicalX = x;
    uint32_t logicalY = y;
    if (rotationDegrees == -90 || rotationDegrees == 270) {
        logicalWidth = targetHeight;
        logicalHeight = targetWidth;
        logicalX = targetHeight - 1 - y;
        logicalY = x;
    } else if (rotationDegrees == 90 || rotationDegrees == -270) {
        logicalWidth = targetHeight;
        logicalHeight = targetWidth;
        logicalX = y;
        logicalY = targetWidth - 1 - x;
    } else if (rotationDegrees == 180 || rotationDegrees == -180) {
        logicalX = targetWidth - 1 - x;
        logicalY = targetHeight - 1 - y;
    }

    double cropWidth = width_;
    double cropHeight = height_;

    if (static_cast<uint64_t>(width_) * logicalHeight >
        static_cast<uint64_t>(logicalWidth) * height_) {
        cropWidth = std::max<double>(1.0,
                static_cast<double>(height_) * logicalWidth / logicalHeight);
    } else {
        cropHeight = std::max<double>(1.0,
                static_cast<double>(width_) * logicalHeight / logicalWidth);
    }

    const float safeScale = std::max(0.1f, std::min(8.0f, transform.scale));
    cropWidth = std::max<double>(1.0, cropWidth / safeScale);
    cropHeight = std::max<double>(1.0, cropHeight / safeScale);
    const float centerX = std::max(0.0f, std::min(1.0f, transform.centerX));
    const float centerY = std::max(0.0f, std::min(1.0f, transform.centerY));
    double cropX = centerX * width_ - cropWidth / 2.0;
    double cropY = centerY * height_ - cropHeight / 2.0;
    if (cropWidth <= width_) cropX = std::max(0.0, std::min(cropX, width_ - cropWidth));
    if (cropHeight <= height_) cropY = std::max(0.0, std::min(cropY, height_ - cropHeight));
    const double sampledX = cropX + static_cast<double>(logicalX) * cropWidth / logicalWidth;
    const double sampledY = cropY + static_cast<double>(logicalY) * cropHeight / logicalHeight;
    if (sampledX < 0.0 || sampledY < 0.0 || sampledX >= width_ || sampledY >= height_) {
        return {0, 0, 0};
    }
    const uint32_t sourceX = static_cast<uint32_t>(sampledX);
    const uint32_t sourceY = static_cast<uint32_t>(sampledY);
    const size_t offset = (static_cast<size_t>(sourceY) * width_ + sourceX) * 3;
    return {pixels_[offset], pixels_[offset + 1], pixels_[offset + 2]};
}

bool RgbFrame::fillYuv420(uint32_t width, uint32_t height,
                          const Yuv420Layout& layout,
                          const RgbTransform& transform) const {
    if (!valid() || width == 0 || height == 0 || layout.y == nullptr ||
        layout.cb == nullptr || layout.cr == nullptr || layout.yStep == 0 ||
        layout.yStride < (width - 1) * layout.yStep + 1 ||
        layout.cStride == 0 || layout.chromaStep == 0) {
        return false;
    }

    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* yRow = layout.y + y * layout.yStride;
        for (uint32_t x = 0; x < width; ++x) {
            const Rgb rgb = sample(width, height, x, y, transform);
            yRow[x * layout.yStep] = clamp(
                    (77 * rgb.r + 150 * rgb.g + 29 * rgb.b) >> 8);
        }
    }

    for (uint32_t y = 0; y < height / 2; ++y) {
        uint8_t* cbRow = layout.cb + y * layout.cStride;
        uint8_t* crRow = layout.cr + y * layout.cStride;
        for (uint32_t x = 0; x < width / 2; ++x) {
            const Rgb rgb = sample(width, height, x * 2, y * 2, transform);
            cbRow[x * layout.chromaStep] = clamp(
                    128 + ((-43 * rgb.r - 85 * rgb.g + 128 * rgb.b) >> 8));
            crRow[x * layout.chromaStep] = clamp(
                    128 + ((128 * rgb.r - 107 * rgb.g - 21 * rgb.b) >> 8));
        }
    }
    return true;
}

bool RgbFrame::fillRgbRow(uint32_t width, uint32_t height, uint32_t row,
                          uint8_t* rgb, const RgbTransform& transform) const {
    if (!valid() || width == 0 || height == 0 || row >= height || rgb == nullptr) {
        return false;
    }
    for (uint32_t x = 0; x < width; ++x) {
        const Rgb value = sample(width, height, x, row, transform);
        rgb[x * 3] = value.r;
        rgb[x * 3 + 1] = value.g;
        rgb[x * 3 + 2] = value.b;
    }
    return true;
}

}  // namespace vcam
