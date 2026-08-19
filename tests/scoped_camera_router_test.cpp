#include <assert.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "vcam/ScopedCameraRouter.h"

int main() {
    const auto nonce = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
    const std::filesystem::path rootPath =
            std::filesystem::temp_directory_path() /
            ("android-vcam-scoped-route-test-" + std::to_string(nonce));
    const std::string root = rootPath.string();
    const std::string providers = root + "/providers";
    const std::string provider = providers + "/movie";
    assert(std::filesystem::create_directories(provider));

    const std::string enabled = provider + "/enabled";
    std::ofstream marker(enabled);
    assert(marker.good());
    marker.close();

    const std::string routes = root + "/routes.tsv";
    std::ofstream table(routes);
    assert(table.good());
    table << "com.example.virtual\t0\tmovie\n"
          << "com.example.virtual\t1\tmovie\n"
          << "com.example.physical\t0\tphysical-1\n"
          << "com.example.stopped\t0\tstopped\n"
          << "*\t1\tmovie\n";
    table.close();

    const auto back = vcam::ScopedCameraRouter::resolve(
            "com.example.virtual", "0", routes, providers);
    assert(back.redirected);
    assert(back.requestedCameraId == "0");
    assert(back.effectiveCameraId == "1000");
    assert(back.providerId == "movie");
    assert(back.match == vcam::RouteMatchKind::Package);

    const auto front = vcam::ScopedCameraRouter::resolve(
            "com.example.virtual", "1", routes, providers);
    assert(front.redirected);
    assert(front.effectiveCameraId == "1001");

    const auto physical = vcam::ScopedCameraRouter::resolve(
            "com.example.physical", "0", routes, providers);
    assert(!physical.redirected);
    assert(physical.effectiveCameraId == "0");
    assert(physical.providerId == "physical-1");

    const auto stopped = vcam::ScopedCameraRouter::resolve(
            "com.example.stopped", "0", routes, providers);
    assert(stopped.configured);
    assert(!stopped.available);
    assert(!stopped.redirected);
    assert(stopped.providerId == "stopped");
    assert(stopped.effectiveCameraId == "1000");

    const auto unknown = vcam::ScopedCameraRouter::resolve(
            "com.example.unknown", "0", routes, providers);
    assert(!unknown.redirected);
    assert(!unknown.configured);
    assert(unknown.available);
    assert(unknown.effectiveCameraId == "0");
    assert(unknown.providerId == "physical-0");

    const auto global = vcam::ScopedCameraRouter::resolve(
            "", "1", routes, providers);
    assert(global.configured);
    assert(global.available);
    assert(global.redirected);
    assert(global.providerId == "movie");
    assert(global.effectiveCameraId == "1001");
    assert(global.match == vcam::RouteMatchKind::Global);

    const auto explicitInternal = vcam::ScopedCameraRouter::resolve(
            "com.example.virtual", "1000", routes, providers);
    assert(!explicitInternal.redirected);
    assert(explicitInternal.effectiveCameraId == "1000");

    assert(vcam::ScopedCameraRouter::isInternalCameraId("1000"));
    assert(vcam::ScopedCameraRouter::isInternalCameraId("1001"));
    assert(!vcam::ScopedCameraRouter::isInternalCameraId("0"));
    assert(vcam::ScopedCameraRouter::visibleCameraId("1000") == "0");
    assert(vcam::ScopedCameraRouter::visibleCameraId("1001") == "1");
    assert(vcam::ScopedCameraRouter::visibleCameraId("2") == "2");

    assert(std::filesystem::remove_all(rootPath) >= 5);
    return 0;
}
