/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include "vcam/MetadataBuilder.h"

namespace vcam {

MetadataBuilder::MetadataBuilder(size_t entryCapacity, size_t dataCapacity)
    : metadata_(allocate_camera_metadata(entryCapacity, dataCapacity)) {}

MetadataBuilder::~MetadataBuilder() {
    if (metadata_ != nullptr) {
        free_camera_metadata(metadata_);
    }
}

camera_metadata_t* MetadataBuilder::release() {
    if (metadata_ != nullptr) {
        sort_camera_metadata(metadata_);
    }
    camera_metadata_t* released = metadata_;
    metadata_ = nullptr;
    return released;
}

}  // namespace vcam
