#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "vcam/Android14ParcelObserver.h"

namespace vcam::runtime {

enum class Android14ProtocolEvidenceVerdict {
    kPending = 0,
    kRejected,
    kProbeCompatible,
};

struct Android14ProtocolEvidenceSnapshot {
    std::uint64_t requiredMask = 0;
    std::uint64_t seenMask = 0;
    std::uint64_t validMask = 0;
    std::uint64_t invalidMask = 0;
    std::uint64_t unsupportedMask = 0;
    Android14ProtocolEvidenceVerdict verdict =
            Android14ProtocolEvidenceVerdict::kPending;
};

// Stable bit assignment for the Android 14 initial-release transaction
// template. Zero means that the role is not part of the reviewed template.
std::uint64_t android14ProtocolRoleBit(const std::string& role) noexcept;

// Baseline roles that an ordinary qualification app can deterministically
// exercise. Legacy-only parameter lookup and the nested concurrent-session
// payload remain optional evidence outside this mask.
std::uint64_t android14ProtocolRequiredEvidenceMask() noexcept;

const char* android14ProtocolEvidenceVerdictName(
        Android14ProtocolEvidenceVerdict verdict) noexcept;

// Thread-safe, privacy-preserving qualification evidence. It records role
// masks only; camera IDs, package names, UIDs and PIDs are never retained.
class Android14ProtocolEvidence final {
public:
    void record(const ParcelObservation& observation) noexcept;
    Android14ProtocolEvidenceSnapshot snapshot() const noexcept;

private:
    std::atomic<std::uint64_t> seenMask_{0};
    std::atomic<std::uint64_t> validMask_{0};
    std::atomic<std::uint64_t> invalidMask_{0};
    std::atomic<std::uint64_t> unsupportedMask_{0};
};

}  // namespace vcam::runtime
