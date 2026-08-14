/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include "vcam/PatternGenerator.h"

#include <cassert>
#include <cstdint>
#include <numeric>
#include <vector>

namespace {

uint64_t checksum(const std::vector<uint8_t>& bytes) {
    return std::accumulate(bytes.begin(), bytes.end(), uint64_t{0});
}

}  // namespace

int main() {
    constexpr uint32_t width = 640;
    constexpr uint32_t height = 480;
    std::vector<uint8_t> y(width * height);
    std::vector<uint8_t> cb(width * height / 2, 0);
    std::vector<uint8_t> cr(width * height / 2, 0);

    vcam::Yuv420Layout layout{
            y.data(), cb.data(), cr.data(), width, width, 2};
    assert(vcam::PatternGenerator::fillYuv420(width, height, 1, 0, layout));
    const uint64_t first = checksum(y) + checksum(cb) + checksum(cr);
    assert(first != 0);

    assert(vcam::PatternGenerator::fillYuv420(width, height, 33, 1, layout));
    const uint64_t second = checksum(y) + checksum(cb) + checksum(cr);
    assert(second != 0);
    assert(first != second);

    vcam::Yuv420Layout invalid{};
    assert(!vcam::PatternGenerator::fillYuv420(width, height, 1, 0, invalid));
    return 0;
}
