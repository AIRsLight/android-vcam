/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include "vcam/RouteResolver.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fstream>

namespace vcam {

bool RouteResolver::validProviderId(const std::string& value) {
    if (value.empty() || value.size() > 64) return false;
    for (const char character : value) {
        const bool allowed = (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == '.' ||
                character == '_' || character == '-';
        if (!allowed) return false;
    }
    return true;
}

std::string RouteResolver::defaultPhysicalProvider(int cameraId) {
    return "physical-" + std::to_string(cameraId);
}

std::string RouteResolver::providerForPackage(
        const std::string& packageName, int cameraId,
        const std::string& routesPath, const std::string& providersPath) {
    const ProviderSelection selection = resolveProviderForPackage(
            packageName, cameraId, routesPath, providersPath);
    return selection.available
            ? selection.providerId : defaultPhysicalProvider(cameraId);
}

ProviderSelection RouteResolver::resolveProviderForPackage(
        const std::string& packageName, int cameraId,
        const std::string& routesPath, const std::string& providersPath) {
    const std::string fallback = defaultPhysicalProvider(cameraId);
    if (packageName.empty()) return {fallback, false, true};
    std::ifstream input(routesPath);
    if (!input) return {fallback, false, true};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find('\t');
        const size_t second = first == std::string::npos
                ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
                line.substr(0, first) != packageName ||
                line.substr(first + 1, second - first - 1) !=
                        std::to_string(cameraId)) {
            continue;
        }
        const std::string provider = line.substr(second + 1);
        if (!validProviderId(provider)) return {provider, true, false};
        if (physicalIdFromProvider(provider) >= 0) {
            return {provider, true, true};
        }
        const std::string enabled = providersPath + "/" + provider + "/enabled";
        return {provider, true, access(enabled.c_str(), R_OK) == 0};
    }
    return {fallback, false, true};
}

int RouteResolver::physicalIdFromProvider(const std::string& provider) {
    constexpr const char* prefix = "physical-";
    if (provider.compare(0, strlen(prefix), prefix) != 0) return -1;
    const char* value = provider.c_str() + strlen(prefix);
    char* end = nullptr;
    const long parsed = strtol(value, &end, 10);
    return end != nullptr && *value != '\0' && *end == '\0' && parsed >= 0 &&
                   parsed <= 1
            ? static_cast<int>(parsed) : -1;
}

std::string RouteResolver::framePath(
        const std::string& provider, const std::string& providersPath) {
    return providersPath + "/" + provider + "/frame.rgb";
}

}  // namespace vcam
