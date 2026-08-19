/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include "vcam/ScopedCameraRouter.h"

#include "vcam/RouteResolver.h"

namespace vcam {
namespace {

int targetId(const std::string& cameraId) {
    if (cameraId == "0") return 0;
    if (cameraId == "1") return 1;
    return -1;
}

}  // namespace

ScopedCameraRoute ScopedCameraRouter::resolve(
        const std::string& packageName,
        const std::string& requestedCameraId,
        const std::string& routesPath,
        const std::string& providersPath) {
    ScopedCameraRoute route{
            requestedCameraId,
            requestedCameraId,
            {},
            false,
            true,
            false,
    };

    const int target = targetId(requestedCameraId);
    if (target < 0) return route;

    const ProviderSelection selection = RouteResolver::resolveProviderForPackage(
            packageName, target, routesPath, providersPath);
    route.providerId = selection.providerId;
    route.configured = selection.configured;
    route.available = selection.available;
    if (RouteResolver::physicalIdFromProvider(route.providerId) >= 0) {
        return route;
    }

    route.effectiveCameraId = target == 0
            ? kBackVirtualCameraId : kFrontVirtualCameraId;
    route.redirected = route.available;
    return route;
}

bool ScopedCameraRouter::isInternalCameraId(const std::string& cameraId) {
    return cameraId == kBackVirtualCameraId ||
            cameraId == kFrontVirtualCameraId;
}

std::string ScopedCameraRouter::visibleCameraId(
        const std::string& cameraId) {
    if (cameraId == kBackVirtualCameraId) return "0";
    if (cameraId == kFrontVirtualCameraId) return "1";
    return cameraId;
}

}  // namespace vcam
