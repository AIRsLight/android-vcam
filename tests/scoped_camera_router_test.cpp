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
    const std::string targets = root + "/targets.tsv";
    const std::string camera1Targets = root + "/camera1-targets.tsv";
    const std::string camera1Map = root + "/camera1-map.tsv";
    std::ofstream table(routes);
    assert(table.good());
    table << "com.example.virtual\t0\tmovie\n"
          << "com.example.virtual\t1\tmovie\n"
          << "com.example.physical\t0\tphysical-1\n"
          << "com.example.stopped\t0\tstopped\n"
          << "com.example.mapped\t0\tmovie\n"
          << "*\t1\tmovie\n";
    table.close();

    std::ofstream targetMap(targets);
    assert(targetMap.good());
    targetMap << "10\t0\n"
              << "front-main\t1\n";
    targetMap.close();

    std::ofstream camera1TargetMap(camera1Targets);
    assert(camera1TargetMap.good());
    camera1TargetMap << "0\t0\n"
                     << "1\t1\n";
    camera1TargetMap.close();

    std::ofstream camera1Table(camera1Map);
    assert(camera1Table.good());
    camera1Table << "10\t0\n"
                 << "1000\t1\n"
                 << "1001\t2\n"
                 << "broken\tnot-an-index\n";
    camera1Table.close();

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

    const auto mapped = vcam::ScopedCameraRouter::resolve(
            "com.example.mapped", "10", routes, providers, targets);
    assert(mapped.redirected);
    assert(mapped.requestedCameraId == "10");
    assert(mapped.effectiveCameraId == "1000");
    assert(mapped.providerId == "movie");

    const auto unmapped = vcam::ScopedCameraRouter::resolve(
            "com.example.mapped", "0", routes, providers, targets);
    assert(!unmapped.configured);
    assert(!unmapped.redirected);

    const auto mappedCamera1 = vcam::ScopedCameraRouter::resolve(
            "com.example.mapped", "0", routes, providers, camera1Targets);
    assert(mappedCamera1.redirected);
    assert(mappedCamera1.effectiveCameraId == "1000");

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
    assert(vcam::ScopedCameraRouter::visibleCameraId("1000", targets) == "10");
    assert(vcam::ScopedCameraRouter::visibleCameraId("1001", targets) ==
           "front-main");
    assert(vcam::ScopedCameraRouter::camera1IndexForId("10", camera1Map) ==
           "0");
    assert(vcam::ScopedCameraRouter::camera1IndexForId("1000", camera1Map) ==
           "1");
    assert(vcam::ScopedCameraRouter::camera1IndexForId("1001", camera1Map) ==
           "2");
    assert(vcam::ScopedCameraRouter::camera1IndexForId("broken", camera1Map)
                   .empty());
    assert(vcam::ScopedCameraRouter::camera1IndexForId("missing", camera1Map)
                   .empty());

    std::ofstream routingDisabled(routes + ".disabled");
    assert(routingDisabled.good());
    routingDisabled.close();
    const auto disabled = vcam::ScopedCameraRouter::resolve(
            "com.example.virtual", "0", routes, providers);
    assert(!disabled.configured);
    assert(disabled.available);
    assert(!disabled.redirected);
    assert(disabled.effectiveCameraId == "0");
    assert(disabled.providerId == "physical-0");

    assert(std::filesystem::remove_all(rootPath) >= 5);
    return 0;
}
