#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vcam::runtime {

enum class ProbeStatus {
    kAllowed = 0,
    kInvalidRecipe,
    kModuleNotLoaded,
    kFileSizeMismatch,
    kFileHashMismatch,
    kBuildIdMismatch,
    kSymbolMissing,
    kSymbolOutsideExecutableSegment,
    kCodePrefixMismatch,
    kUnsupportedPlatform,
};

struct SymbolRequirement {
    std::string name;
    std::vector<std::uint8_t> codePrefix;
};

struct HookRequirement {
    std::string role;
    std::string symbol;
};

struct BinderTransaction {
    std::string role;
    std::uint32_t code = 0;
};

struct ResolvedSymbol {
    std::string name;
    std::uintptr_t address = 0;
};

struct AbiRecipe {
    unsigned int schema = 0;
    std::string moduleSuffix;
    std::uint64_t fileSize = 0;
    std::string sha256Hex;
    std::string buildIdHex;
    std::string architecture;
    std::vector<SymbolRequirement> symbols;
    std::vector<HookRequirement> hooks;
    std::vector<BinderTransaction> transactions;
};

struct ProbeResult {
    ProbeStatus status = ProbeStatus::kInvalidRecipe;
    std::string message;
    std::string modulePath;
    std::string observedBuildIdHex;
    std::uintptr_t moduleBase = 0;
    std::vector<ResolvedSymbol> resolvedSymbols;

    explicit operator bool() const { return status == ProbeStatus::kAllowed; }
};

bool parseAbiRecipe(const std::string& path, AbiRecipe* recipe, std::string* error);
bool sha256FileHex(const std::string& path, std::string* digestHex, std::string* error);
ProbeResult validateLoadedModule(const AbiRecipe& recipe);
const char* probeStatusName(ProbeStatus status);

}  // namespace vcam::runtime
