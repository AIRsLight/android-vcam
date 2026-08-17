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

constexpr uint8_t kRgbMagic[8] = {'V', 'C', 'A', 'M', 'R', 'G', 'B', '1'};
constexpr uint8_t kYuvMagic[8] = {'V', 'C', 'A', 'M', 'Y', 'U', 'V', '1'};

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

RgbFrame::PixelFormat RgbFrame::formatFromMagic(const uint8_t* bytes) {
    if (bytes == nullptr) return PixelFormat::kNone;
    if (memcmp(bytes, kRgbMagic, sizeof(kRgbMagic)) == 0) return PixelFormat::kRgb888;
    if (memcmp(bytes, kYuvMagic, sizeof(kYuvMagic)) == 0) return PixelFormat::kI420;
    return PixelFormat::kNone;
}

uint64_t RgbFrame::expectedPayload(PixelFormat format, uint32_t width,
                                   uint32_t height) {
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (format == PixelFormat::kRgb888) return pixels * 3;
    if (format == PixelFormat::kI420 && (width & 1U) == 0 && (height & 1U) == 0) {
        return pixels + pixels / 2;
    }
    return 0;
}

void RgbFrame::reset() {
    format_ = PixelFormat::kNone;
    width_ = 0;
    height_ = 0;
    sequence_ = 0;
    pixels_.clear();
}

bool RgbFrame::load(const uint8_t* bytes, size_t size) {
    if (bytes == nullptr || size < kHeaderSize) return false;

    const PixelFormat format = formatFromMagic(bytes);
    const uint32_t width = readLe32(bytes + 8);
    const uint32_t height = readLe32(bytes + 12);
    const uint32_t payloadSize = readLe32(bytes + 16);
    const uint32_t sequence = readLe32(bytes + 20);
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (format == PixelFormat::kNone || width == 0 || height == 0 ||
        width > kMaxDimension || height > kMaxDimension || pixels > kMaxPixels) {
        return false;
    }

    const uint64_t expected = expectedPayload(format, width, height);
    if (expected > std::numeric_limits<uint32_t>::max() ||
        payloadSize != expected || size != kHeaderSize + expected) {
        return false;
    }

    std::vector<uint8_t> replacement(bytes + kHeaderSize, bytes + size);
    format_ = format;
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
        reset();
        return false;
    }

    uint8_t header[kHeaderSize];
    const bool headerRead = fread(header, 1, sizeof(header), file) == sizeof(header);
    const PixelFormat format = headerRead ? formatFromMagic(header) : PixelFormat::kNone;
    if (!headerRead || format == PixelFormat::kNone) {
        fclose(file);
        return valid();
    }

    const uint32_t width = readLe32(header + 8);
    const uint32_t height = readLe32(header + 12);
    const uint32_t payloadSize = readLe32(header + 16);
    const uint32_t sequence = readLe32(header + 20);
    if (valid() && format == format_ && width == width_ && height == height_ &&
        sequence == sequence_) {
        fclose(file);
        return true;
    }

    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (width == 0 || height == 0 || width > kMaxDimension ||
        height > kMaxDimension || pixels > kMaxPixels ||
        expectedPayload(format, width, height) != payloadSize) {
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

bool RgbFrame::mapCoordinate(uint32_t targetWidth, uint32_t targetHeight,
                             uint32_t x, uint32_t y,
                             const RgbTransform& transform,
                             uint32_t* sourceX, uint32_t* sourceY) const {
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
        return false;
    }
    *sourceX = static_cast<uint32_t>(sampledX);
    *sourceY = static_cast<uint32_t>(sampledY);
    return true;
}

RgbFrame::Yuv RgbFrame::sampleYuv(uint32_t targetWidth, uint32_t targetHeight,
                                  uint32_t x, uint32_t y,
                                  const RgbTransform& transform) const {
    uint32_t sourceX = 0;
    uint32_t sourceY = 0;
    if (!mapCoordinate(targetWidth, targetHeight, x, y, transform,
                       &sourceX, &sourceY)) {
        return {0, 128, 128};
    }
    const size_t pixel = static_cast<size_t>(sourceY) * width_ + sourceX;
    if (format_ == PixelFormat::kI420) {
        const size_t ySize = static_cast<size_t>(width_) * height_;
        const size_t chromaWidth = width_ / 2;
        const size_t chromaOffset = static_cast<size_t>(sourceY / 2) * chromaWidth +
                                    sourceX / 2;
        return {pixels_[pixel], pixels_[ySize + chromaOffset],
                pixels_[ySize + ySize / 4 + chromaOffset]};
    }
    const size_t offset = pixel * 3;
    const Rgb rgb{pixels_[offset], pixels_[offset + 1], pixels_[offset + 2]};
    return {
            clamp(16 + ((66 * rgb.r + 129 * rgb.g + 25 * rgb.b + 128) >> 8)),
            clamp(128 + ((-38 * rgb.r - 74 * rgb.g + 112 * rgb.b + 128) >> 8)),
            clamp(128 + ((112 * rgb.r - 94 * rgb.g - 18 * rgb.b + 128) >> 8)),
    };
}

RgbFrame::Rgb RgbFrame::toRgb(Yuv value) {
    const int y = std::max(0, static_cast<int>(value.y) - 16);
    const int cb = static_cast<int>(value.cb) - 128;
    const int cr = static_cast<int>(value.cr) - 128;
    return {
            clamp((298 * y + 409 * cr + 128) >> 8),
            clamp((298 * y - 100 * cb - 208 * cr + 128) >> 8),
            clamp((298 * y + 516 * cb + 128) >> 8),
    };
}

RgbFrame::Rgb RgbFrame::sampleRgb(uint32_t targetWidth, uint32_t targetHeight,
                                  uint32_t x, uint32_t y,
                                  const RgbTransform& transform) const {
    if (format_ == PixelFormat::kI420) {
        return toRgb(sampleYuv(targetWidth, targetHeight, x, y, transform));
    }
    uint32_t sourceX = 0;
    uint32_t sourceY = 0;
    if (!mapCoordinate(targetWidth, targetHeight, x, y, transform,
                       &sourceX, &sourceY)) {
        return {0, 0, 0};
    }
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
            yRow[x * layout.yStep] = sampleYuv(width, height, x, y, transform).y;
        }
    }

    for (uint32_t y = 0; y < height / 2; ++y) {
        uint8_t* cbRow = layout.cb + y * layout.cStride;
        uint8_t* crRow = layout.cr + y * layout.cStride;
        for (uint32_t x = 0; x < width / 2; ++x) {
            const Yuv yuv = sampleYuv(width, height, x * 2, y * 2, transform);
            cbRow[x * layout.chromaStep] = yuv.cb;
            crRow[x * layout.chromaStep] = yuv.cr;
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
        const Rgb value = sampleRgb(width, height, x, row, transform);
        rgb[x * 3] = value.r;
        rgb[x * 3 + 1] = value.g;
        rgb[x * 3 + 2] = value.b;
    }
    return true;
}

}  // namespace vcam
