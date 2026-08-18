#include "vcam/RuntimeActivationPreflight.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace vcam::runtime {
namespace {

bool parseHexAddress(const std::string& value, std::uintptr_t* output) {
    if (value.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 16);
    if (errno != 0 || end == value.c_str() || *end != '\0' ||
        parsed > std::numeric_limits<std::uintptr_t>::max()) {
        return false;
    }
    *output = static_cast<std::uintptr_t>(parsed);
    return true;
}

bool validPermissions(const std::string& permissions) {
    return permissions.size() == 4 &&
            (permissions[0] == 'r' || permissions[0] == '-') &&
            (permissions[1] == 'w' || permissions[1] == '-') &&
            (permissions[2] == 'x' || permissions[2] == '-') &&
            (permissions[3] == 'p' || permissions[3] == 's');
}

std::string trimLeadingSpaces(std::string value) {
    const std::size_t first = value.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : value.substr(first);
}

std::string basename(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

ActivationPreflightResult result(
        ActivationPreflightStatus status, const char* message) {
    ActivationPreflightResult value;
    value.status = status;
    value.message = message;
    return value;
}

bool rangeEnd(std::uintptr_t start, std::size_t size, std::uintptr_t* end) {
    if (size == 0 || start > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }
    *end = start + size;
    return true;
}

}  // namespace

bool parseProcMapsText(
        const std::string& text,
        std::vector<ProcessMapEntry>* mappings,
        std::string* error) {
    if (mappings == nullptr) {
        if (error != nullptr) {
            *error = "mapping output is null";
        }
        return false;
    }
    mappings->clear();
    std::istringstream input(text);
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) {
            continue;
        }
        std::istringstream fields(line);
        std::string range;
        std::string permissions;
        std::string offset;
        std::string device;
        std::string inode;
        if (!(fields >> range >> permissions >> offset >> device >> inode)) {
            if (error != nullptr) {
                *error = "malformed /proc maps line " + std::to_string(lineNumber);
            }
            mappings->clear();
            return false;
        }
        const std::size_t dash = range.find('-');
        ProcessMapEntry entry;
        if (dash == std::string::npos ||
            !parseHexAddress(range.substr(0, dash), &entry.start) ||
            !parseHexAddress(range.substr(dash + 1), &entry.end) ||
            entry.start >= entry.end || !validPermissions(permissions)) {
            if (error != nullptr) {
                *error = "invalid /proc maps range or permissions on line " +
                        std::to_string(lineNumber);
            }
            mappings->clear();
            return false;
        }
        entry.readable = permissions[0] == 'r';
        entry.writable = permissions[1] == 'w';
        entry.executable = permissions[2] == 'x';
        entry.privateMapping = permissions[3] == 'p';
        std::string path;
        std::getline(fields, path);
        entry.path = trimLeadingSpaces(std::move(path));
        if (!mappings->empty() && entry.start < mappings->back().end) {
            if (error != nullptr) {
                *error = "/proc maps entries are unsorted or overlapping on line " +
                        std::to_string(lineNumber);
            }
            mappings->clear();
            return false;
        }
        mappings->push_back(std::move(entry));
    }
    if (mappings->empty()) {
        if (error != nullptr) {
            *error = "/proc maps contained no entries";
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

ActivationSnapshot collectCurrentProcessActivationSnapshot(
        std::uintptr_t targetAddress, std::size_t targetSize) {
    ActivationSnapshot snapshot;
    constexpr std::size_t kMaxPreflightTargetSize = 4096;
    if (targetAddress == 0 || targetSize == 0 ||
        targetSize > kMaxPreflightTargetSize ||
        targetAddress > static_cast<std::uintptr_t>(std::numeric_limits<off_t>::max())) {
        snapshot.collectionError = "target range is invalid for read-only preflight";
        return snapshot;
    }
    std::ifstream mapsFile("/proc/self/maps");
    if (!mapsFile) {
        snapshot.collectionError = "could not open /proc/self/maps";
        return snapshot;
    }
    const std::string mapsText(
            (std::istreambuf_iterator<char>(mapsFile)), std::istreambuf_iterator<char>());
    if (!parseProcMapsText(mapsText, &snapshot.mappings, &snapshot.collectionError)) {
        return snapshot;
    }

    std::uintptr_t targetEnd = 0;
    if (rangeEnd(targetAddress, targetSize, &targetEnd)) {
        const auto mapping = std::find_if(
                snapshot.mappings.begin(), snapshot.mappings.end(),
                [&](const ProcessMapEntry& entry) {
                    return entry.start <= targetAddress && targetEnd <= entry.end;
                });
        if (mapping != snapshot.mappings.end() && mapping->readable) {
            snapshot.observedTargetBytes.resize(targetSize);
            const int memory = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
            if (memory < 0) {
                snapshot.collectionError = "could not open /proc/self/mem read-only";
                return snapshot;
            }
            const ssize_t bytesRead = pread(
                    memory, snapshot.observedTargetBytes.data(), targetSize,
                    static_cast<off_t>(targetAddress));
            close(memory);
            if (bytesRead < 0 || static_cast<std::size_t>(bytesRead) != targetSize) {
                snapshot.observedTargetBytes.clear();
                snapshot.collectionError = "could not read the complete verified target range";
                return snapshot;
            }
        }
    }

    DIR* taskDirectory = opendir("/proc/self/task");
    if (taskDirectory == nullptr) {
        snapshot.collectionError = "could not open /proc/self/task";
        return snapshot;
    }
    while (true) {
        errno = 0;
        dirent* entry = readdir(taskDirectory);
        if (entry == nullptr) {
            if (errno != 0) {
                snapshot.collectionError = "could not enumerate all entries in /proc/self/task";
            }
            break;
        }
        errno = 0;
        char* end = nullptr;
        const long value = std::strtol(entry->d_name, &end, 10);
        if (errno == 0 && end != entry->d_name && *end == '\0' && value > 0 &&
            value <= std::numeric_limits<std::int32_t>::max()) {
            snapshot.threadIds.push_back(static_cast<std::int32_t>(value));
        }
    }
    closedir(taskDirectory);
    if (!snapshot.collectionError.empty()) {
        return snapshot;
    }
    std::sort(snapshot.threadIds.begin(), snapshot.threadIds.end());
    snapshot.threadIds.erase(
            std::unique(snapshot.threadIds.begin(), snapshot.threadIds.end()),
            snapshot.threadIds.end());
    snapshot.currentThreadId = static_cast<std::int32_t>(syscall(SYS_gettid));
    return snapshot;
}

ActivationPreflightResult evaluateActivationPreflight(
        const Arm64PatchPlan& plan,
        std::uintptr_t targetAddress,
        const std::string& expectedModuleBasename,
        const ActivationSnapshot& snapshot) {
    std::uintptr_t targetEnd = 0;
    if (plan.status != Arm64PlanStatus::kReady || targetAddress == 0 ||
        (targetAddress & 3u) != 0 || plan.overwriteSize == 0 ||
        plan.originalBytes.size() != plan.overwriteSize ||
        !rangeEnd(targetAddress, plan.overwriteSize, &targetEnd) ||
        plan.resumeAddress != targetEnd || expectedModuleBasename.empty() ||
        expectedModuleBasename.find('/') != std::string::npos) {
        return result(ActivationPreflightStatus::kInvalidPlan,
                      "plan, target address or expected module basename is invalid");
    }
    if (!snapshot.collectionError.empty()) {
        return result(ActivationPreflightStatus::kSnapshotError,
                      "current process snapshot collection failed");
    }
    const auto mapping = std::find_if(
            snapshot.mappings.begin(), snapshot.mappings.end(),
            [&](const ProcessMapEntry& entry) {
                return entry.start <= targetAddress && targetAddress < entry.end;
            });
    if (mapping == snapshot.mappings.end()) {
        return result(ActivationPreflightStatus::kTargetMappingMissing,
                      "target address is not present in the process map");
    }
    if (targetEnd > mapping->end) {
        return result(ActivationPreflightStatus::kTargetRangeSplit,
                      "entry patch would cross a process mapping boundary");
    }
    if (!mapping->readable || mapping->writable || !mapping->executable ||
        !mapping->privateMapping) {
        return result(ActivationPreflightStatus::kTargetPermissionMismatch,
                      "target mapping must be private, readable, executable and not writable");
    }
    if (basename(mapping->path) != expectedModuleBasename) {
        return result(ActivationPreflightStatus::kTargetModuleMismatch,
                      "target mapping does not belong to the expected module");
    }
    if (snapshot.observedTargetBytes != plan.originalBytes) {
        return result(ActivationPreflightStatus::kTargetBytesMismatch,
                      "live target bytes do not match the reviewed ARM64 plan");
    }
    if (snapshot.threadIds.empty() || snapshot.currentThreadId <= 0 ||
        !std::is_sorted(snapshot.threadIds.begin(), snapshot.threadIds.end()) ||
        std::any_of(snapshot.threadIds.begin(), snapshot.threadIds.end(),
                    [](std::int32_t tid) { return tid <= 0; }) ||
        std::adjacent_find(snapshot.threadIds.begin(), snapshot.threadIds.end()) !=
                snapshot.threadIds.end()) {
        return result(ActivationPreflightStatus::kThreadInventoryInvalid,
                      "thread inventory is empty, duplicated or invalid");
    }
    if (!std::binary_search(snapshot.threadIds.begin(), snapshot.threadIds.end(),
                            snapshot.currentThreadId)) {
        return result(ActivationPreflightStatus::kCurrentThreadMissing,
                      "current thread is absent from the thread inventory");
    }
    ActivationPreflightResult ready = result(
            ActivationPreflightStatus::kReadyReadOnly,
            "read-only activation preflight passed; no thread was stopped or memory changed");
    ready.targetMapping = *mapping;
    ready.threadCount = snapshot.threadIds.size();
    return ready;
}

const char* activationPreflightStatusName(ActivationPreflightStatus status) {
    switch (status) {
        case ActivationPreflightStatus::kReadyReadOnly: return "ready_read_only";
        case ActivationPreflightStatus::kInvalidPlan: return "invalid_plan";
        case ActivationPreflightStatus::kSnapshotError: return "snapshot_error";
        case ActivationPreflightStatus::kTargetMappingMissing:
            return "target_mapping_missing";
        case ActivationPreflightStatus::kTargetRangeSplit: return "target_range_split";
        case ActivationPreflightStatus::kTargetPermissionMismatch:
            return "target_permission_mismatch";
        case ActivationPreflightStatus::kTargetModuleMismatch:
            return "target_module_mismatch";
        case ActivationPreflightStatus::kTargetBytesMismatch:
            return "target_bytes_mismatch";
        case ActivationPreflightStatus::kThreadInventoryInvalid:
            return "thread_inventory_invalid";
        case ActivationPreflightStatus::kCurrentThreadMissing:
            return "current_thread_missing";
    }
    return "unknown";
}

}  // namespace vcam::runtime
