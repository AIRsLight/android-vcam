#include "vcam/RuntimeAbiGuard.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern "C" __attribute__((noinline, visibility("default"))) int vcam_runtime_probe_marker(int value) {
    return value * 7 + 3;
}

namespace {

std::string executablePath() {
    char buffer[4096];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    assert(length > 0);
    buffer[length] = '\0';
    return buffer;
}

std::string baseName(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::uint64_t fileSize(const std::string& path) {
    struct stat metadata {};
    assert(stat(path.c_str(), &metadata) == 0);
    return static_cast<std::uint64_t>(metadata.st_size);
}

std::string hex(const std::uint8_t* bytes, std::size_t count) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        output.push_back(digits[bytes[i] >> 4]);
        output.push_back(digits[bytes[i] & 0xf]);
    }
    return output;
}

}  // namespace

int main() {
    const std::string hashVectorPath =
            "/tmp/vcam-runtime-sha256-" + std::to_string(getpid()) + ".txt";
    {
        std::ofstream output(hashVectorPath, std::ios::binary);
        output << "abc";
    }
    std::string vectorDigest;
    std::string vectorError;
    assert(vcam::runtime::sha256FileHex(hashVectorPath, &vectorDigest, &vectorError));
    assert(vectorDigest == "ba7816bf8f01cfea414140de5dae2223"
                           "b00361a396177a9cb410ff61f20015ad");
    std::remove(hashVectorPath.c_str());

    const std::string executable = executablePath();
    vcam::runtime::AbiRecipe recipe;
    recipe.schema = 1;
    recipe.moduleSuffix = baseName(executable);
    recipe.fileSize = fileSize(executable);
    std::string hashError;
    assert(vcam::runtime::sha256FileHex(executable, &recipe.sha256Hex, &hashError));
    recipe.buildIdHex = "0011223344556677";
    recipe.symbols.push_back({"vcam_runtime_probe_marker", {0}});

    vcam::runtime::ProbeResult result = vcam::runtime::validateLoadedModule(recipe);
    assert(result.status == vcam::runtime::ProbeStatus::kBuildIdMismatch);
    assert(!result.observedBuildIdHex.empty());

    recipe.buildIdHex = result.observedBuildIdHex;
    const auto* marker = reinterpret_cast<const std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(&vcam_runtime_probe_marker));
    recipe.symbols[0].codePrefix.assign(marker, marker + 12);
    result = vcam::runtime::validateLoadedModule(recipe);
    assert(result.status == vcam::runtime::ProbeStatus::kAllowed);

    ++recipe.fileSize;
    result = vcam::runtime::validateLoadedModule(recipe);
    assert(result.status == vcam::runtime::ProbeStatus::kFileSizeMismatch);
    --recipe.fileSize;

    recipe.sha256Hex[0] = recipe.sha256Hex[0] == '0' ? '1' : '0';
    result = vcam::runtime::validateLoadedModule(recipe);
    assert(result.status == vcam::runtime::ProbeStatus::kFileHashMismatch);
    assert(vcam::runtime::sha256FileHex(executable, &recipe.sha256Hex, &hashError));

    recipe.symbols[0].codePrefix[0] ^= 0xff;
    result = vcam::runtime::validateLoadedModule(recipe);
    assert(result.status == vcam::runtime::ProbeStatus::kCodePrefixMismatch);
    recipe.symbols[0].codePrefix[0] ^= 0xff;

    recipe.symbols[0].name = "vcam_symbol_that_does_not_exist";
    result = vcam::runtime::validateLoadedModule(recipe);
    assert(result.status == vcam::runtime::ProbeStatus::kSymbolMissing);

    const std::string recipePath = "/tmp/vcam-runtime-recipe-" + std::to_string(getpid()) + ".tsv";
    {
        std::ofstream output(recipePath);
        output << "schema\t1\n"
               << "module\t" << recipe.moduleSuffix << "\n"
               << "file_size\t" << recipe.fileSize << "\n"
               << "sha256\t" << recipe.sha256Hex << "\n"
               << "build_id\t" << recipe.buildIdHex << "\n"
               << "symbol\tvcam_runtime_probe_marker\t" << hex(marker, 12) << "\n";
    }
    vcam::runtime::AbiRecipe parsed;
    std::string error;
    assert(vcam::runtime::parseAbiRecipe(recipePath, &parsed, &error));
    assert(parsed.moduleSuffix == recipe.moduleSuffix);
    assert(parsed.symbols.size() == 1);
    assert(vcam::runtime::validateLoadedModule(parsed).status ==
           vcam::runtime::ProbeStatus::kAllowed);

    {
        std::ofstream output(recipePath);
        output << "schema\t2\n"
               << "architecture\tarm64\n"
               << "module\t" << recipe.moduleSuffix << "\n"
               << "file_size\t" << recipe.fileSize << "\n"
               << "sha256\t" << recipe.sha256Hex << "\n"
               << "build_id\t" << recipe.buildIdHex << "\n"
               << "symbol\tvcam_runtime_probe_marker\t" << hex(marker, 12) << "\n"
               << "hook\ton_transact\tvcam_runtime_probe_marker\n"
               << "transaction\tconnect_device\t4\n";
    }
    assert(vcam::runtime::parseAbiRecipe(recipePath, &parsed, &error));
    assert(parsed.schema == 2);
    assert(parsed.architecture == "arm64");
    assert(parsed.hooks.size() == 1);
    assert(parsed.transactions.size() == 1);
    assert(parsed.transactions[0].code == 4);
    std::remove(recipePath.c_str());

    return 0;
}
