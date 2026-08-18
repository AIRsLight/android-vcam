#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vcam/Arm64PatchPlanner.h"

namespace vcam::runtime {

struct ProcessMapEntry {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    bool readable = false;
    bool writable = false;
    bool executable = false;
    bool privateMapping = false;
    std::string path;
};

struct ActivationSnapshot {
    std::vector<ProcessMapEntry> mappings;
    std::vector<std::int32_t> threadIds;
    std::int32_t currentThreadId = -1;
    std::vector<std::uint8_t> observedTargetBytes;
    std::string collectionError;
};

enum class ActivationPreflightStatus {
    kReadyReadOnly = 0,
    kInvalidPlan,
    kSnapshotError,
    kTargetMappingMissing,
    kTargetRangeSplit,
    kTargetPermissionMismatch,
    kTargetModuleMismatch,
    kTargetBytesMismatch,
    kThreadInventoryInvalid,
    kCurrentThreadMissing,
};

struct ActivationPreflightResult {
    ActivationPreflightStatus status = ActivationPreflightStatus::kInvalidPlan;
    const char* message = "";
    ProcessMapEntry targetMapping;
    std::size_t threadCount = 0;

    explicit operator bool() const {
        return status == ActivationPreflightStatus::kReadyReadOnly;
    }
};

bool parseProcMapsText(
        const std::string& text,
        std::vector<ProcessMapEntry>* mappings,
        std::string* error);

// Reads /proc/self/maps and /proc/self/task, then copies targetSize readable
// bytes only when one mapping contains the complete range. No process state is
// modified and no thread is stopped.
ActivationSnapshot collectCurrentProcessActivationSnapshot(
        std::uintptr_t targetAddress, std::size_t targetSize);

ActivationPreflightResult evaluateActivationPreflight(
        const Arm64PatchPlan& plan,
        std::uintptr_t targetAddress,
        const std::string& expectedModuleBasename,
        const ActivationSnapshot& snapshot);

const char* activationPreflightStatusName(ActivationPreflightStatus status);

}  // namespace vcam::runtime
