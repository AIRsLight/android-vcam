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

    static ScopedCameraRoute resolve(
            const std::string& packageName,
            const std::string& requestedCameraId,
            const std::string& routesPath = kDefaultRoutesPath,
            const std::string& providersPath = kDefaultProvidersPath);

    static bool isInternalCameraId(const std::string& cameraId);
    static std::string visibleCameraId(const std::string& cameraId);
};

}  // namespace vcam
