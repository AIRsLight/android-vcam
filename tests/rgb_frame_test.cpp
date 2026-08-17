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

    // New video providers publish planar I420. The decoder must copy YUV
    // directly for camera buffers while still converting it for JPEG rows.
    std::vector<uint8_t> yuvFrame(vcam::RgbFrame::kHeaderSize + 6, 0);
    const uint8_t yuvMagic[] = {'V', 'C', 'A', 'M', 'Y', 'U', 'V', '1'};
    for (size_t i = 0; i < sizeof(yuvMagic); ++i) yuvFrame[i] = yuvMagic[i];
    writeLe32(&yuvFrame, 8, 2);
    writeLe32(&yuvFrame, 12, 2);
    writeLe32(&yuvFrame, 16, 6);
    writeLe32(&yuvFrame, 20, 8);
    yuvFrame[vcam::RgbFrame::kHeaderSize] = 82;
    yuvFrame[vcam::RgbFrame::kHeaderSize + 1] = 82;
    yuvFrame[vcam::RgbFrame::kHeaderSize + 2] = 82;
    yuvFrame[vcam::RgbFrame::kHeaderSize + 3] = 82;
    yuvFrame[vcam::RgbFrame::kHeaderSize + 4] = 90;
    yuvFrame[vcam::RgbFrame::kHeaderSize + 5] = 240;
    assert(decoded.load(yuvFrame.data(), yuvFrame.size()));
    std::vector<uint8_t> directY(4);
    std::vector<uint8_t> directCb(1);
    std::vector<uint8_t> directCr(1);
    vcam::Yuv420Layout directLayout{
            directY.data(), directCb.data(), directCr.data(), 2, 1, 1};
    assert(decoded.fillYuv420(2, 2, directLayout));
    assert(directY[0] == 82 && directCb[0] == 90 && directCr[0] == 240);
    std::vector<uint8_t> convertedRgb(2 * 3);
    assert(decoded.fillRgbRow(2, 2, 0, convertedRgb.data()));
    assert(convertedRgb[0] > 245 && convertedRgb[1] < 10 && convertedRgb[2] < 10);

    frame[0] = 'X';
    assert(!decoded.load(frame.data(), frame.size()));
    assert(decoded.valid());  // A rejected replacement preserves the last good frame.
    return 0;
}
