#include "vcam/RuntimeAbiGuard.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#if defined(__ANDROID__) || defined(__linux__)
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vcam::runtime {
namespace {

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string lowerHex(const std::uint8_t* bytes, std::size_t size) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        stream << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return stream.str();
}

bool decodeHex(const std::string& text, std::vector<std::uint8_t>* bytes) {
    if (text.empty() || (text.size() % 2) != 0) {
        return false;
    }
    bytes->clear();
    bytes->reserve(text.size() / 2);
    const auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = nibble(text[i]);
        const int low = nibble(text[i + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        bytes->push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

bool parseUnsignedDecimal(const std::string& text, std::uint64_t* value) {
    if (text.empty() || value == nullptr) {
        return false;
    }
    std::uint64_t parsed = 0;
    for (char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    *value = parsed;
    return true;
}

std::string trimCarriageReturn(std::string value) {
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return value;
}

constexpr std::array<std::uint32_t, 64> kSha256Constants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

std::uint32_t rotateRight(std::uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32 - count));
}

class Sha256 {
public:
    void update(const std::uint8_t* input, std::size_t size) {
        totalBytes_ += size;
        while (size > 0) {
            const std::size_t copied = std::min(size, block_.size() - blockSize_);
            std::memcpy(block_.data() + blockSize_, input, copied);
            blockSize_ += copied;
            input += copied;
            size -= copied;
            if (blockSize_ == block_.size()) {
                transform(block_.data());
                blockSize_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finish() {
        const std::uint64_t bitCount = totalBytes_ * 8;
        block_[blockSize_++] = 0x80;
        if (blockSize_ > 56) {
            std::fill(block_.begin() + blockSize_, block_.end(), 0);
            transform(block_.data());
            blockSize_ = 0;
        }
        std::fill(block_.begin() + blockSize_, block_.begin() + 56, 0);
        for (unsigned int i = 0; i < 8; ++i) {
            block_[63 - i] = static_cast<std::uint8_t>(bitCount >> (i * 8));
        }
        transform(block_.data());

        std::array<std::uint8_t, 32> digest {};
        for (std::size_t i = 0; i < state_.size(); ++i) {
            for (unsigned int byte = 0; byte < 4; ++byte) {
                digest[i * 4 + byte] =
                        static_cast<std::uint8_t>(state_[i] >> (24 - byte * 8));
            }
        }
        return digest;
    }

private:
    void transform(const std::uint8_t* block) {
        std::array<std::uint32_t, 64> words {};
        for (std::size_t i = 0; i < 16; ++i) {
            words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                       (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                       static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (std::size_t i = 16; i < words.size(); ++i) {
            const std::uint32_t s0 = rotateRight(words[i - 15], 7) ^
                                     rotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const std::uint32_t s1 = rotateRight(words[i - 2], 17) ^
                                     rotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t i = 0; i < words.size(); ++i) {
            const std::uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + sum1 + choice + kSha256Constants[i] + words[i];
            const std::uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_ = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    std::array<std::uint8_t, 64> block_ {};
    std::size_t blockSize_ = 0;
    std::uint64_t totalBytes_ = 0;
};

#if defined(__ANDROID__) || defined(__linux__)

struct LoadedModule {
    std::string path;
    std::uintptr_t base = 0;
    const ElfW(Phdr)* programHeaders = nullptr;
    ElfW(Half) programHeaderCount = 0;
};

std::string currentExecutablePath() {
    std::vector<char> buffer(1024);
    for (;;) {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (length < 0) {
            return {};
        }
        if (static_cast<std::size_t>(length) < buffer.size() - 1) {
            buffer[static_cast<std::size_t>(length)] = '\0';
            return buffer.data();
        }
        buffer.resize(buffer.size() * 2);
    }
}

struct FindModuleContext {
    const std::string* suffix;
    LoadedModule* module;
};

int findModuleCallback(dl_phdr_info* info, std::size_t, void* opaque) {
    auto* context = static_cast<FindModuleContext*>(opaque);
    std::string path = info->dlpi_name == nullptr ? "" : info->dlpi_name;
    if (path.empty()) {
        path = currentExecutablePath();
    }
    if (!endsWith(path, *context->suffix)) {
        return 0;
    }
    context->module->path = std::move(path);
    context->module->base = static_cast<std::uintptr_t>(info->dlpi_addr);
    context->module->programHeaders = info->dlpi_phdr;
    context->module->programHeaderCount = info->dlpi_phnum;
    return 1;
}

std::string readBuildId(const LoadedModule& module) {
    constexpr std::size_t kAlignment = 4;
    const auto align = [](std::size_t value) {
        return (value + kAlignment - 1) & ~(kAlignment - 1);
    };
    for (ElfW(Half) index = 0; index < module.programHeaderCount; ++index) {
        const ElfW(Phdr)& header = module.programHeaders[index];
        if (header.p_type != PT_NOTE || header.p_memsz < sizeof(ElfW(Nhdr))) {
            continue;
        }
        const auto* cursor = reinterpret_cast<const std::uint8_t*>(module.base + header.p_vaddr);
        const auto* end = cursor + header.p_memsz;
        while (cursor + sizeof(ElfW(Nhdr)) <= end) {
            const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(cursor);
            cursor += sizeof(ElfW(Nhdr));
            const std::size_t nameSize = align(note->n_namesz);
            const std::size_t descriptorSize = align(note->n_descsz);
            if (nameSize > static_cast<std::size_t>(end - cursor)) {
                break;
            }
            const auto* name = cursor;
            cursor += nameSize;
            if (descriptorSize > static_cast<std::size_t>(end - cursor)) {
                break;
            }
            const auto* descriptor = cursor;
            cursor += descriptorSize;
            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz >= 3 &&
                std::memcmp(name, "GNU", 3) == 0) {
                return lowerHex(descriptor, note->n_descsz);
            }
        }
    }
    return {};
}

bool isInsideExecutableSegment(const LoadedModule& module, std::uintptr_t address,
                               std::size_t length) {
    if (length > std::numeric_limits<std::uintptr_t>::max() - address) {
        return false;
    }
    const std::uintptr_t addressEnd = address + length;
    for (ElfW(Half) index = 0; index < module.programHeaderCount; ++index) {
        const ElfW(Phdr)& header = module.programHeaders[index];
        if (header.p_type != PT_LOAD || (header.p_flags & PF_X) == 0) {
            continue;
        }
        const std::uintptr_t start = module.base + header.p_vaddr;
        const std::uintptr_t end = start + header.p_memsz;
        if (address >= start && addressEnd <= end) {
            return true;
        }
    }
    return false;
}

#endif

ProbeResult failure(ProbeStatus status, std::string message) {
    ProbeResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

}  // namespace

bool parseAbiRecipe(const std::string& path, AbiRecipe* recipe, std::string* error) {
    if (recipe == nullptr) {
        if (error != nullptr) {
            *error = "recipe output is null";
        }
        return false;
    }
    std::ifstream input(path);
    if (!input) {
        if (error != nullptr) {
            *error = "cannot open recipe: " + path;
        }
        return false;
    }

    AbiRecipe parsed;
    std::string line;
    unsigned int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        line = trimCarriageReturn(std::move(line));
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t firstTab = line.find('\t');
        if (firstTab == std::string::npos) {
            if (error != nullptr) {
                *error = "line " + std::to_string(lineNumber) + " has no tab separator";
            }
            return false;
        }
        const std::string key = line.substr(0, firstTab);
        const std::string value = line.substr(firstTab + 1);
        if (key == "schema") {
            std::uint64_t number = 0;
            if (!parseUnsignedDecimal(value, &number) ||
                number > std::numeric_limits<unsigned int>::max()) {
                if (error != nullptr) *error = "invalid schema on line " + std::to_string(lineNumber);
                return false;
            }
            parsed.schema = static_cast<unsigned int>(number);
        } else if (key == "module") {
            parsed.moduleSuffix = value;
        } else if (key == "file_size") {
            std::uint64_t number = 0;
            if (!parseUnsignedDecimal(value, &number)) {
                if (error != nullptr) *error = "invalid file_size on line " + std::to_string(lineNumber);
                return false;
            }
            parsed.fileSize = number;
        } else if (key == "sha256") {
            parsed.sha256Hex = value;
        } else if (key == "build_id") {
            parsed.buildIdHex = value;
        } else if (key == "symbol") {
            const std::size_t secondTab = value.find('\t');
            if (secondTab == std::string::npos) {
                if (error != nullptr) *error = "symbol has no code prefix on line " + std::to_string(lineNumber);
                return false;
            }
            SymbolRequirement requirement;
            requirement.name = value.substr(0, secondTab);
            if (requirement.name.empty() ||
                !decodeHex(value.substr(secondTab + 1), &requirement.codePrefix)) {
                if (error != nullptr) *error = "invalid symbol on line " + std::to_string(lineNumber);
                return false;
            }
            parsed.symbols.push_back(std::move(requirement));
        } else {
            if (error != nullptr) *error = "unknown key on line " + std::to_string(lineNumber) + ": " + key;
            return false;
        }
    }

    std::vector<std::uint8_t> buildId;
    std::vector<std::uint8_t> sha256;
    if (parsed.schema != 1 || parsed.moduleSuffix.empty() || parsed.fileSize == 0 ||
        !decodeHex(parsed.buildIdHex, &buildId) || buildId.size() < 8 ||
        !decodeHex(parsed.sha256Hex, &sha256) || sha256.size() != 32 ||
        parsed.symbols.empty()) {
        if (error != nullptr) {
            *error = "recipe is incomplete or uses an unsupported schema";
        }
        return false;
    }
    *recipe = std::move(parsed);
    return true;
}

bool sha256FileHex(const std::string& path, std::string* digestHex, std::string* error) {
    if (digestHex == nullptr) {
        if (error != nullptr) *error = "digest output is null";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error != nullptr) *error = "cannot open module for hashing: " + path;
        return false;
    }
    Sha256 digest;
    std::array<std::uint8_t, 64 * 1024> buffer {};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const std::streamsize count = input.gcount();
        if (count > 0) {
            digest.update(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    if (!input.eof()) {
        if (error != nullptr) *error = "failed while hashing module: " + path;
        return false;
    }
    const auto result = digest.finish();
    *digestHex = lowerHex(result.data(), result.size());
    return true;
}

ProbeResult validateLoadedModule(const AbiRecipe& recipe) {
#if !defined(__ANDROID__) && !defined(__linux__)
    (void)recipe;
    return failure(ProbeStatus::kUnsupportedPlatform, "ELF runtime probing is only supported on Android/Linux");
#else
    if (recipe.schema != 1 || recipe.moduleSuffix.empty() || recipe.fileSize == 0 ||
        recipe.buildIdHex.empty() || recipe.symbols.empty()) {
        return failure(ProbeStatus::kInvalidRecipe, "incomplete ABI recipe");
    }

    LoadedModule module;
    FindModuleContext context{&recipe.moduleSuffix, &module};
    dl_iterate_phdr(findModuleCallback, &context);
    if (module.programHeaders == nullptr) {
        return failure(ProbeStatus::kModuleNotLoaded,
                       "module is not loaded: " + recipe.moduleSuffix);
    }

    ProbeResult result;
    result.modulePath = module.path;
    result.moduleBase = module.base;
    result.observedBuildIdHex = readBuildId(module);

    struct stat metadata {};
    if (stat(module.path.c_str(), &metadata) != 0 ||
        static_cast<std::uint64_t>(metadata.st_size) != recipe.fileSize) {
        result.status = ProbeStatus::kFileSizeMismatch;
        result.message = "loaded module file size does not match recipe";
        return result;
    }
    if (result.observedBuildIdHex.empty() || result.observedBuildIdHex != recipe.buildIdHex) {
        result.status = ProbeStatus::kBuildIdMismatch;
        result.message = "loaded module Build ID does not match recipe";
        return result;
    }
    std::string observedSha256;
    std::string hashError;
    if (!sha256FileHex(module.path, &observedSha256, &hashError) ||
        observedSha256 != recipe.sha256Hex) {
        result.status = ProbeStatus::kFileHashMismatch;
        result.message = hashError.empty() ? "loaded module SHA-256 does not match recipe" : hashError;
        return result;
    }

    const bool mainExecutable = module.path == currentExecutablePath();
    void* handle = nullptr;
    if (mainExecutable) {
        handle = dlopen(nullptr, RTLD_NOW);
    } else {
#ifdef RTLD_NOLOAD
        handle = dlopen(module.path.c_str(), RTLD_NOW | RTLD_NOLOAD);
#else
        handle = dlopen(module.path.c_str(), RTLD_NOW);
#endif
    }
    if (handle == nullptr) {
        result.status = ProbeStatus::kModuleNotLoaded;
        result.message = std::string("cannot acquire loaded module handle: ") + dlerror();
        return result;
    }

    for (const SymbolRequirement& requirement : recipe.symbols) {
        dlerror();
        void* symbol = dlsym(handle, requirement.name.c_str());
        const char* symbolError = dlerror();
        if (symbol == nullptr || symbolError != nullptr) {
            result.status = ProbeStatus::kSymbolMissing;
            result.message = "required symbol is missing: " + requirement.name;
            dlclose(handle);
            return result;
        }
        const auto address = reinterpret_cast<std::uintptr_t>(symbol);
        if (!isInsideExecutableSegment(module, address, requirement.codePrefix.size())) {
            result.status = ProbeStatus::kSymbolOutsideExecutableSegment;
            result.message = "symbol is outside an executable segment: " + requirement.name;
            dlclose(handle);
            return result;
        }
        if (!std::equal(requirement.codePrefix.begin(), requirement.codePrefix.end(),
                        reinterpret_cast<const std::uint8_t*>(symbol))) {
            result.status = ProbeStatus::kCodePrefixMismatch;
            result.message = "symbol code prefix does not match: " + requirement.name;
            dlclose(handle);
            return result;
        }
    }
    dlclose(handle);
    result.status = ProbeStatus::kAllowed;
    result.message = "ABI recipe matched; runtime interception may be enabled explicitly";
    return result;
#endif
}

const char* probeStatusName(ProbeStatus status) {
    switch (status) {
        case ProbeStatus::kAllowed: return "allowed";
        case ProbeStatus::kInvalidRecipe: return "invalid_recipe";
        case ProbeStatus::kModuleNotLoaded: return "module_not_loaded";
        case ProbeStatus::kFileSizeMismatch: return "file_size_mismatch";
        case ProbeStatus::kFileHashMismatch: return "file_hash_mismatch";
        case ProbeStatus::kBuildIdMismatch: return "build_id_mismatch";
        case ProbeStatus::kSymbolMissing: return "symbol_missing";
        case ProbeStatus::kSymbolOutsideExecutableSegment: return "symbol_outside_executable_segment";
        case ProbeStatus::kCodePrefixMismatch: return "code_prefix_mismatch";
        case ProbeStatus::kUnsupportedPlatform: return "unsupported_platform";
    }
    return "unknown";
}

}  // namespace vcam::runtime
