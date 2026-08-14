/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <system/camera_metadata.h>

namespace vcam {

class MetadataBuilder final {
  public:
    explicit MetadataBuilder(size_t entryCapacity = 192,
                             size_t dataCapacity = 16384);
    ~MetadataBuilder();

    MetadataBuilder(const MetadataBuilder&) = delete;
    MetadataBuilder& operator=(const MetadataBuilder&) = delete;

    template <typename T>
    bool add(uint32_t tag, const T* values, size_t count) {
        return metadata_ != nullptr &&
               add_camera_metadata_entry(metadata_, tag, values, count) == 0;
    }

    template <typename T, size_t N>
    bool add(uint32_t tag, const T (&values)[N]) {
        return add(tag, values, N);
    }

    template <typename T>
    bool addOne(uint32_t tag, T value) {
        return add(tag, &value, 1);
    }

    camera_metadata_t* get() const { return metadata_; }
    camera_metadata_t* release();

  private:
    camera_metadata_t* metadata_ = nullptr;
};

}  // namespace vcam
