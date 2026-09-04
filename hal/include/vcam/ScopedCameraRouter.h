/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <string>

#include "vcam/RouteResolver.h"

namespace vcam {

struct ScopedCameraRoute {
    std::string requestedCameraId;
    std::string effectiveCameraId;
    std::string providerId;
    bool configured = false;
    bool available = true;
    bool redirected = false;
    RouteMatchKind match = RouteMatchKind::None;
};

class ScopedCameraRouter final {
  public:
    static constexpr const char* kBackVirtualCameraId = "1000";
    static constexpr const char* kFrontVirtualCameraId = "1001";
    static constexpr const char* kDefaultRoutesPath =
            "/data/vendor/camera/vcam/routes.tsv";
    static constexpr const char* kDefaultProvidersPath =
            "/data/vendor/camera/vcam/providers";
    static constexpr const char* kDefaultTargetMapPath =
            "/data/vendor/camera/vcam/targets.tsv";
    static constexpr const char* kDefaultCamera1TargetMapPath =
            "/data/vendor/camera/vcam/camera1-targets.tsv";
    static constexpr const char* kDefaultCamera1MapPath =
            "/data/vendor/camera/vcam/camera1-map.tsv";

    static ScopedCameraRoute resolve(
            const std::string& packageName,
            const std::string& requestedCameraId,
            const std::string& routesPath = kDefaultRoutesPath,
            const std::string& providersPath = kDefaultProvidersPath,
            const std::string& targetMapPath = kDefaultTargetMapPath);

    static bool isInternalCameraId(const std::string& cameraId);
    static std::string visibleCameraId(
            const std::string& cameraId,
            const std::string& targetMapPath = kDefaultTargetMapPath);
    // Physical provider IDs are logical front/back slots. Resolve them through
    // the generated Camera2 target map instead of treating the slot as a
    // device ID. An empty result means the requested physical slot is absent.
    static std::string physicalCameraIdForProvider(
            const std::string& providerId,
            const std::string& targetMapPath = kDefaultTargetMapPath);
    // Camera1 connect() carries an integer index, not the Camera2 string ID.
    // The map is generated from CameraService's "Device N maps to ID" table
    // after the virtual provider has registered.
    static std::string camera1IndexForId(
            const std::string& cameraId,
            const std::string& camera1MapPath = kDefaultCamera1MapPath);
};

}  // namespace vcam
