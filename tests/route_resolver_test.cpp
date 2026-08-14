#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "vcam/RouteResolver.h"

int main() {
    const std::string root = "/data/local/tmp/android-vcam-route-test-" +
            std::to_string(getpid());
    const std::string providers = root + "/providers";
    const std::string provider = providers + "/demo";
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
    fputs("com.example.app\t0\tdemo\n", table);
    fputs("com.example.app\t1\tphysical-0\n", table);
    fputs("com.invalid.app\t0\t../escape\n", table);
    fclose(table);

    assert(vcam::RouteResolver::providerForPackage(
            "com.example.app", 0, routes, providers) == "demo");
    assert(vcam::RouteResolver::providerForPackage(
            "com.example.app", 1, routes, providers) == "physical-0");
    assert(vcam::RouteResolver::providerForPackage(
            "com.unknown", 0, routes, providers) == "physical-0");
    assert(vcam::RouteResolver::providerForPackage(
            "com.invalid.app", 0, routes, providers) == "physical-0");
    assert(vcam::RouteResolver::physicalIdFromProvider("physical-1") == 1);
    assert(vcam::RouteResolver::physicalIdFromProvider("demo") == -1);
    assert(vcam::RouteResolver::framePath("demo", providers) ==
            provider + "/frame.rgb");

    assert(unlink(routes.c_str()) == 0);
    assert(unlink(enabled.c_str()) == 0);
    assert(rmdir(provider.c_str()) == 0);
    assert(rmdir(providers.c_str()) == 0);
    assert(rmdir(root.c_str()) == 0);
    return 0;
}
