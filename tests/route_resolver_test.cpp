#include <assert.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "vcam/RouteResolver.h"

int main() {
    const auto nonce = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
    const std::filesystem::path rootPath =
            std::filesystem::temp_directory_path() /
            ("android-vcam-route-test-" + std::to_string(nonce));
    const std::string root = rootPath.string();
    const std::string providers = root + "/providers";
    const std::string provider = providers + "/demo";
    assert(std::filesystem::create_directories(provider));

    const std::string enabled = provider + "/enabled";
    std::ofstream marker(enabled);
    assert(marker.good());
    marker.close();

    const std::string routes = root + "/routes.tsv";
    std::ofstream table(routes);
    assert(table.good());
    table << "com.example.app\t0\tdemo\n"
          << "com.example.app\t1\tphysical-0\n"
          << "com.invalid.app\t0\t../escape\n"
          << "*\t0\tdemo\n"
          << "*\t1\tphysical-1\n";
    table.close();

    assert(vcam::RouteResolver::providerForPackage(
            "com.example.app", 0, routes, providers) == "demo");
    assert(vcam::RouteResolver::providerForPackage(
            "com.example.app", 1, routes, providers) == "physical-0");
    assert(vcam::RouteResolver::providerForPackage(
            "com.unknown", 0, routes, providers) == "demo");
    assert(vcam::RouteResolver::providerForPackage(
            "", 1, routes, providers) == "physical-1");
    assert(vcam::RouteResolver::providerForPackage(
            "com.invalid.app", 0, routes, providers) == "physical-0");
    const auto configured = vcam::RouteResolver::resolveProviderForPackage(
            "com.example.app", 0, routes, providers);
    assert(configured.configured);
    assert(configured.available);
    assert(configured.providerId == "demo");
    assert(configured.match == vcam::RouteMatchKind::Package);
    const auto invalid = vcam::RouteResolver::resolveProviderForPackage(
            "com.invalid.app", 0, routes, providers);
    assert(invalid.configured);
    assert(!invalid.available);
    const auto missing = vcam::RouteResolver::resolveProviderForPackage(
            "com.unknown", 0, routes, providers);
    assert(missing.configured);
    assert(missing.available);
    assert(missing.match == vcam::RouteMatchKind::Global);
    assert(vcam::RouteResolver::physicalIdFromProvider("physical-1") == 1);
    assert(vcam::RouteResolver::physicalIdFromProvider("demo") == -1);
    assert(vcam::RouteResolver::framePath("demo", providers) ==
            provider + "/frame.rgb");

    assert(std::filesystem::remove_all(rootPath) >= 5);
    return 0;
}
