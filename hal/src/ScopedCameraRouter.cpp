/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include "vcam/ScopedCameraRouter.h"

#include "vcam/RouteResolver.h"

#include <fstream>

namespace vcam {
namespace {

int legacyTargetSlot(const std::string& cameraId) {
    if (cameraId == "0") return 0;
    if (cameraId == "1") return 1;
    return -1;
}

bool parseTargetMapLine(const std::string& line,
                        std::string* visibleId,
                        int* targetSlot) {
    if (visibleId == nullptr || targetSlot == nullptr) return false;
    const size_t separator = line.find('\t');
    if (separator == std::string::npos || separator == 0 ||
        line.find('\t', separator + 1) != std::string::npos) {
        return false;
    }
    const std::string slot = line.substr(separator + 1);
    if (slot != "0" && slot != "1") return false;
    *visibleId = line.substr(0, separator);
    *targetSlot = slot[0] - '0';
    return true;
}

int targetSlot(const std::string& cameraId,
               const std::string& targetMapPath) {
    std::ifstream input(targetMapPath);
    if (!input) return legacyTargetSlot(cameraId);

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string visibleId;
        int slot = -1;
        if (parseTargetMapLine(line, &visibleId, &slot) &&
            visibleId == cameraId) {
            return slot;
        }
    }
    return -1;
}

std::string visibleIdForSlot(int wantedSlot,
                             const std::string& targetMapPath) {
    std::ifstream input(targetMapPath);
    if (!input) return wantedSlot == 0 ? "0" : "1";

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string visibleId;
        int slot = -1;
        if (parseTargetMapLine(line, &visibleId, &slot) &&
            slot == wantedSlot) {
            return visibleId;
        }
    }
    return {};
}

bool isNonNegativeInteger(const std::string& value) {
    if (value.empty()) return false;
    for (const char character : value) {
        if (character < '0' || character > '9') return false;
    }
    return true;
}

}  // namespace

ScopedCameraRoute ScopedCameraRouter::resolve(
        const std::string& packageName,
        const std::string& requestedCameraId,
        const std::string& routesPath,
        const std::string& providersPath,
        const std::string& targetMapPath) {
    ScopedCameraRoute route{
            requestedCameraId,
            requestedCameraId,
            {},
            false,
            true,
            false,
            RouteMatchKind::None,
    };

    const int target = targetSlot(requestedCameraId, targetMapPath);
    if (target < 0) return route;

    const ProviderSelection selection = RouteResolver::resolveProviderForPackage(
            packageName, target, routesPath, providersPath);
    route.providerId = selection.providerId;
    route.configured = selection.configured;
    route.available = selection.available;
    route.match = selection.match;
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
        const std::string& cameraId,
        const std::string& targetMapPath) {
    if (cameraId == kBackVirtualCameraId) {
        return visibleIdForSlot(0, targetMapPath);
    }
    if (cameraId == kFrontVirtualCameraId) {
        return visibleIdForSlot(1, targetMapPath);
    }
    return cameraId;
}

std::string ScopedCameraRouter::physicalCameraIdForProvider(
        const std::string& providerId,
        const std::string& targetMapPath) {
    const int physicalSlot = RouteResolver::physicalIdFromProvider(providerId);
    if (physicalSlot < 0) return {};
    return visibleIdForSlot(physicalSlot, targetMapPath);
}

std::string ScopedCameraRouter::camera1IndexForId(
        const std::string& cameraId,
        const std::string& camera1MapPath) {
    std::ifstream input(camera1MapPath);
    if (!input) return {};

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t separator = line.find('\t');
        if (separator == std::string::npos || separator == 0 ||
            line.find('\t', separator + 1) != std::string::npos) {
            continue;
        }
        const std::string mappedId = line.substr(0, separator);
        const std::string index = line.substr(separator + 1);
        if (mappedId == cameraId && isNonNegativeInteger(index)) {
            return index;
        }
    }
    return {};
}

}  // namespace vcam
