/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <mutex>

#include <hardware/camera_common.h>

namespace vcam {

class OriginalHal final {
  public:
    static OriginalHal& instance();

    bool load();
    camera_module_t* module() const;
    const char* error() const;

  private:
    OriginalHal() = default;
    ~OriginalHal();

    mutable std::mutex mutex_;
    void* handle_ = nullptr;
    camera_module_t* module_ = nullptr;
    const char* error_ = "not loaded";
};

}  // namespace vcam
