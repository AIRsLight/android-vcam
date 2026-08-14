#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "vcam/ScopedCameraRouter.h"

int main() {
    const std::string root = "/data/local/tmp/android-vcam-scoped-route-test-" +
            std::to_string(getpid());
    const std::string providers = root + "/providers";
    const std::string provider = providers + "/movie";
    assert(mkdir(root.c_str(), 0700) == 0);
    assert(mkdir(providers.c_str(), 0700) == 0);
    assert(mkdir(provider.c_str(), 0700) == 0);

    const std::string enabled = provider + "/enabled";
    FILE* marker = fopen(enabled.c_str(), "we");
    assert(marker != nullptr);
    fclose(marker);

    const std::string routes = root + "/routes.tsv";
    FILE* table = fopen(routes.c_str(), "we");
    assert(table != nullptr);
    fputs("com.example.virtual\t0\tmovie\n", table);
    fputs("com.example.virtual\t1\tmovie\n", table);
    fputs("com.example.physical\t0\tphysical-1\n", table);
    fputs("com.example.stopped\t0\tstopped\n", table);
    fclose(table);

    const auto back = vcam::ScopedCameraRouter::resolve(
            "com.example.virtual", "0", routes, providers);
    assert(back.redirected);
    assert(back.requestedCameraId == "0");
    assert(back.effectiveCameraId == "1000");
    assert(back.providerId == "movie");

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

    assert(unlink(routes.c_str()) == 0);
    assert(unlink(enabled.c_str()) == 0);
    assert(rmdir(provider.c_str()) == 0);
    assert(rmdir(providers.c_str()) == 0);
    assert(rmdir(root.c_str()) == 0);
    return 0;
}
