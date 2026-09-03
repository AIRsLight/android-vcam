#include "vcam/Android14ProtocolEvidence.h"

namespace vcam::runtime {
namespace {

constexpr std::uint64_t bit(unsigned int index) noexcept {
    return std::uint64_t{1} << index;
}

constexpr std::uint64_t kConnectApi1 = bit(0);
constexpr std::uint64_t kConnectDevice = bit(1);
constexpr std::uint64_t kAddListener = bit(2);
constexpr std::uint64_t kConcurrentIds = bit(3);
constexpr std::uint64_t kConcurrentSessionSupport = bit(4);
constexpr std::uint64_t kRemoveListener = bit(5);
constexpr std::uint64_t kGetCharacteristics = bit(6);
constexpr std::uint64_t kGetLegacyParameters = bit(7);
constexpr std::uint64_t kSupportsCameraApi = bit(8);
constexpr std::uint64_t kSetTorchMode = bit(9);
constexpr std::uint64_t kTurnOnTorchWithStrength = bit(10);
constexpr std::uint64_t kGetTorchStrength = bit(11);

constexpr std::uint64_t kRequiredMask =
        kConnectApi1 |
        kConnectDevice |
        kAddListener |
        kConcurrentIds |
        kRemoveListener |
        kGetCharacteristics |
        kGetLegacyParameters |
        kSupportsCameraApi |
        kSetTorchMode |
        kTurnOnTorchWithStrength |
        kGetTorchStrength;

}  // namespace

std::uint64_t android14ProtocolRoleBit(const std::string& role) noexcept {
    if (role == "connect_api1") return kConnectApi1;
    if (role == "connect_device") return kConnectDevice;
    if (role == "add_listener") return kAddListener;
    if (role == "get_concurrent_camera_ids") return kConcurrentIds;
    if (role == "concurrent_session_support") return kConcurrentSessionSupport;
    if (role == "remove_listener") return kRemoveListener;
    if (role == "get_camera_characteristics") return kGetCharacteristics;
    if (role == "get_legacy_parameters") return kGetLegacyParameters;
    if (role == "supports_camera_api") return kSupportsCameraApi;
    if (role == "set_torch_mode") return kSetTorchMode;
    if (role == "turn_on_torch_with_strength") {
        return kTurnOnTorchWithStrength;
    }
    if (role == "get_torch_strength") return kGetTorchStrength;
    return 0;
}

std::uint64_t android14ProtocolRequiredEvidenceMask() noexcept {
    return kRequiredMask;
}

const char* android14ProtocolEvidenceVerdictName(
        Android14ProtocolEvidenceVerdict verdict) noexcept {
    switch (verdict) {
        case Android14ProtocolEvidenceVerdict::kPending:
            return "pending";
        case Android14ProtocolEvidenceVerdict::kRejected:
            return "rejected";
        case Android14ProtocolEvidenceVerdict::kProbeCompatible:
            return "probe_compatible";
    }
    return "pending";
}

void Android14ProtocolEvidence::record(
        const ParcelObservation& observation) noexcept {
    const std::uint64_t roleBit =
            android14ProtocolRoleBit(observation.transaction.role);
    if (roleBit == 0) return;

    seenMask_.fetch_or(roleBit, std::memory_order_relaxed);
    switch (observation.status) {
        case ParcelObservationStatus::kObserved:
            validMask_.fetch_or(roleBit, std::memory_order_relaxed);
            break;
        case ParcelObservationStatus::kUnsupportedPayload:
            unsupportedMask_.fetch_or(roleBit, std::memory_order_relaxed);
            break;
        case ParcelObservationStatus::kNullParcel:
        case ParcelObservationStatus::kMalformedHeader:
        case ParcelObservationStatus::kWrongInterface:
        case ParcelObservationStatus::kMalformedPayload:
            invalidMask_.fetch_or(roleBit, std::memory_order_relaxed);
            break;
        case ParcelObservationStatus::kNotRoutedTransaction:
            break;
    }
}

Android14ProtocolEvidenceSnapshot Android14ProtocolEvidence::snapshot()
        const noexcept {
    Android14ProtocolEvidenceSnapshot result;
    result.requiredMask = kRequiredMask;
    result.seenMask = seenMask_.load(std::memory_order_relaxed);
    result.validMask = validMask_.load(std::memory_order_relaxed);
    result.invalidMask = invalidMask_.load(std::memory_order_relaxed);
    result.unsupportedMask = unsupportedMask_.load(std::memory_order_relaxed);
    if ((result.invalidMask & result.requiredMask) != 0 ||
        (result.unsupportedMask & result.requiredMask) != 0) {
        result.verdict = Android14ProtocolEvidenceVerdict::kRejected;
    } else if ((result.validMask & result.requiredMask) == result.requiredMask) {
        result.verdict = Android14ProtocolEvidenceVerdict::kProbeCompatible;
    }
    return result;
}

}  // namespace vcam::runtime
