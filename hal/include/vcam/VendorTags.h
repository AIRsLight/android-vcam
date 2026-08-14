/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <stdint.h>

namespace vcam {

// ColorOS/OxygenOS CameraService looks up this tag by its fully-qualified
// name and writes the active client package into the session parameters.
// Keep the OEM section id. ColorOS' metadata marshaling path only preserves
// vendor tags in provider-assigned sections; using the generic first vendor
// section (0x8000) makes the tag resolvable by name in cameraserver but it is
// dropped before Camera HAL receives the session parameters.
// The pinned OEM HAL owns 0x80700000 (com.oplus/is.sdk.camera.package).
// Allocate the next tag in that section for the framework-injected package.
constexpr uint32_t kOplusPackageNameTag = 0x80700001u;
constexpr const char* kOplusPackageNameSection = "com.oplus";
constexpr const char* kOplusPackageNameName = "packageName";

// Provider-owned package routing contract used by the AOSP HIDL/AIDL
// frontends. Provider-scoped vendor-tag descriptors allow this first vendor
// section value to coexist with tags exposed by the OEM camera provider.
constexpr uint32_t kVcamClientPackageTag = 0x80000000u;
constexpr const char* kVcamClientPackageSection = "io.github.androidvcam";
constexpr const char* kVcamClientPackageName = "clientPackage";

}  // namespace vcam
