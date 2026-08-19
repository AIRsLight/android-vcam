/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <string>

namespace vcam {

enum class RouteMatchKind {
    None = 0,
    Package,
    Global,
};

struct ProviderSelection {
    std::string providerId;
    bool configured = false;
    bool available = true;
    RouteMatchKind match = RouteMatchKind::None;
};

class RouteResolver final {
  public:
    static constexpr const char* kDefaultRoutesPath =
            "/data/vendor/camera/vcam/routes.tsv";
    static constexpr const char* kDefaultProvidersPath =
            "/data/vendor/camera/vcam/providers";

    static std::string providerForPackage(
            const std::string& packageName, int cameraId,
            const std::string& routesPath = kDefaultRoutesPath,
            const std::string& providersPath = kDefaultProvidersPath);
    static ProviderSelection resolveProviderForPackage(
            const std::string& packageName, int cameraId,
            const std::string& routesPath = kDefaultRoutesPath,
            const std::string& providersPath = kDefaultProvidersPath);
    static int physicalIdFromProvider(const std::string& provider);
    static std::string framePath(
            const std::string& provider,
            const std::string& providersPath = kDefaultProvidersPath);

  private:
    static bool validProviderId(const std::string& value);
    static std::string defaultPhysicalProvider(int cameraId);
};

}  // namespace vcam
