#include <assert.h>
#include <stdint.h>

#include <vector>

#include "vcam/FrameRenderer.h"

namespace {

void appendLe32(std::vector<uint8_t>* bytes, uint32_t value) {
    bytes->push_back(static_cast<uint8_t>(value));
    bytes->push_back(static_cast<uint8_t>(value >> 8));
    bytes->push_back(static_cast<uint8_t>(value >> 16));
    bytes->push_back(static_cast<uint8_t>(value >> 24));
}

std::vector<uint8_t> frame() {
    std::vector<uint8_t> bytes{'V', 'C', 'A', 'M', 'R', 'G', 'B', '1'};
    appendLe32(&bytes, 2);
    appendLe32(&bytes, 2);
    appendLe32(&bytes, 12);
    appendLe32(&bytes, 7);
    const uint8_t pixels[] = {
            255, 0, 0, 0, 255, 0,
            0, 0, 255, 255, 255, 255,
    };
    bytes.insert(bytes.end(), pixels, pixels + sizeof(pixels));
    return bytes;
}

}  // namespace

int main() {
    vcam::FrameRenderer renderer;
    const std::vector<uint8_t> bytes = frame();
    assert(renderer.loadFrame(bytes.data(), bytes.size()));
    assert(renderer.hasSourceFrame());

    uint8_t row[6]{};
    assert(renderer.fillRgbRow(2, 2, 0, row, 1, 0));
    assert(row[0] == 255 && row[1] == 0 && row[2] == 0);
    assert(row[3] == 0 && row[4] == 255 && row[5] == 0);

    vcam::FrameRenderer pattern;
    std::vector<uint8_t> y(16), cb(4), cr(4);
    vcam::Yuv420Layout layout{
            y.data(), cb.data(), cr.data(), 4, 2, 1, 1,
    };
    assert(pattern.fillYuv420(4, 4, layout, 3, 1));
    bool nonzero = false;
    for (uint8_t value : y) nonzero |= value != 0;
    assert(nonzero);
    return 0;
}
