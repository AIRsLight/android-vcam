/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include "vcam/RgbFrame.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

void writeLe32(std::vector<uint8_t>* bytes, size_t offset, uint32_t value) {
    (*bytes)[offset] = static_cast<uint8_t>(value);
    (*bytes)[offset + 1] = static_cast<uint8_t>(value >> 8);
    (*bytes)[offset + 2] = static_cast<uint8_t>(value >> 16);
    (*bytes)[offset + 3] = static_cast<uint8_t>(value >> 24);
}

}  // namespace

int main() {
    constexpr uint32_t sourceWidth = 2;
    constexpr uint32_t sourceHeight = 2;
    std::vector<uint8_t> frame(vcam::RgbFrame::kHeaderSize + 12, 0);
    const uint8_t magic[] = {'V', 'C', 'A', 'M', 'R', 'G', 'B', '1'};
    for (size_t i = 0; i < sizeof(magic); ++i) frame[i] = magic[i];
    writeLe32(&frame, 8, sourceWidth);
    writeLe32(&frame, 12, sourceHeight);
    writeLe32(&frame, 16, 12);
    writeLe32(&frame, 20, 7);
    // Red, green, blue, white.
    const uint8_t pixels[] = {
            255, 0, 0, 0, 255, 0,
            0, 0, 255, 255, 255, 255,
    };
    for (size_t i = 0; i < sizeof(pixels); ++i) {
        frame[vcam::RgbFrame::kHeaderSize + i] = pixels[i];
    }

    vcam::RgbFrame decoded;
    assert(decoded.load(frame.data(), frame.size()));
    assert(decoded.width() == 2 && decoded.height() == 2 && decoded.sequence() == 7);

    std::vector<uint8_t> rgb(4 * 3);
    assert(decoded.fillRgbRow(4, 4, 0, rgb.data()));
    assert(rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 0);
    assert(rgb[9] == 0 && rgb[10] == 255 && rgb[11] == 0);

    std::vector<uint8_t> rotated(2 * 3);
    vcam::RgbTransform counterClockwise;
    counterClockwise.rotationDegrees = -90;
    assert(decoded.fillRgbRow(2, 2, 0, rotated.data(), counterClockwise));
    // Counter-clockwise: green, white.
    assert(rotated[0] == 0 && rotated[1] == 255 && rotated[2] == 0);
    assert(rotated[3] == 255 && rotated[4] == 255 && rotated[5] == 255);
    vcam::RgbTransform clockwise;
    clockwise.rotationDegrees = 90;
    assert(decoded.fillRgbRow(2, 2, 0, rotated.data(), clockwise));
    // Clockwise: blue, red.
    assert(rotated[0] == 0 && rotated[1] == 0 && rotated[2] == 255);
    assert(rotated[3] == 255 && rotated[4] == 0 && rotated[5] == 0);

    vcam::RgbTransform zoomedOut;
    zoomedOut.scale = 0.5f;
    std::vector<uint8_t> zoomedRow(4 * 3);
    assert(decoded.fillRgbRow(4, 4, 0, zoomedRow.data(), zoomedOut));
    for (uint8_t value : zoomedRow) assert(value == 0);
    assert(decoded.fillRgbRow(4, 4, 1, zoomedRow.data(), zoomedOut));
    assert(zoomedRow[0] == 0 && zoomedRow[1] == 0 && zoomedRow[2] == 0);
    assert(zoomedRow[3] == 255 && zoomedRow[4] == 0 && zoomedRow[5] == 0);
    assert(zoomedRow[6] == 0 && zoomedRow[7] == 255 && zoomedRow[8] == 0);

    std::vector<uint8_t> y(16);
    std::vector<uint8_t> cb(4);
    std::vector<uint8_t> cr(4);
    vcam::Yuv420Layout layout{y.data(), cb.data(), cr.data(), 4, 2, 1};
    assert(decoded.fillYuv420(4, 4, layout));
    assert(y[0] > 60 && y[0] < 90);  // Red luma in the integer BT.601 transform.

    frame[0] = 'X';
    assert(!decoded.load(frame.data(), frame.size()));
    assert(decoded.valid());  // A rejected replacement preserves the last good frame.
    return 0;
}
