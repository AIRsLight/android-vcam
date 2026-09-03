#include "vcam/Android14ProtocolEvidence.h"

#include <cassert>
#include <cstring>
#include <string>

namespace {

vcam::runtime::ParcelObservation observation(
        const char* role,
        vcam::runtime::ParcelObservationStatus status) {
    vcam::runtime::ParcelObservation result;
    result.transaction.role = role;
    result.status = status;
    return result;
}

}  // namespace

int main() {
    using vcam::runtime::Android14ProtocolEvidence;
    using vcam::runtime::Android14ProtocolEvidenceVerdict;
    using vcam::runtime::ParcelObservationStatus;

    assert(vcam::runtime::android14ProtocolRoleBit("unknown") == 0);
    assert(vcam::runtime::android14ProtocolRoleBit("connect_api1") != 0);
    assert(vcam::runtime::android14ProtocolRoleBit("connect_device") !=
           vcam::runtime::android14ProtocolRoleBit("connect_api1"));

    Android14ProtocolEvidence evidence;
    auto snapshot = evidence.snapshot();
    assert(snapshot.requiredMask != 0);
    assert(snapshot.seenMask == 0);
    assert(snapshot.verdict == Android14ProtocolEvidenceVerdict::kPending);

    const char* requiredRoles[] = {
            "connect_api1",
            "connect_device",
            "add_listener",
            "get_concurrent_camera_ids",
            "remove_listener",
            "get_camera_characteristics",
            "supports_camera_api",
            "set_torch_mode",
            "turn_on_torch_with_strength",
            "get_torch_strength",
    };
    for (const char* role : requiredRoles) {
        evidence.record(observation(role, ParcelObservationStatus::kObserved));
    }
    snapshot = evidence.snapshot();
    assert((snapshot.validMask & snapshot.requiredMask) == snapshot.requiredMask);
    assert(snapshot.invalidMask == 0);
    assert(snapshot.verdict ==
           Android14ProtocolEvidenceVerdict::kProbeCompatible);

    // Legacy-parameter lookup is hardware-dependent. It is useful evidence
    // when observed, but a modern Camera2-only device must not remain pending.
    assert((snapshot.requiredMask &
            vcam::runtime::android14ProtocolRoleBit(
                    "get_legacy_parameters")) == 0);
    assert(std::strcmp(vcam::runtime::android14ProtocolEvidenceVerdictName(
                               snapshot.verdict),
                       "probe_compatible") == 0);

    // The currently unsupported nested concurrency Parcel is tracked, but it
    // does not erase successful coverage of the safe observable prefixes.
    evidence.record(observation(
            "concurrent_session_support",
            ParcelObservationStatus::kUnsupportedPayload));
    snapshot = evidence.snapshot();
    assert(snapshot.unsupportedMask != 0);
    assert(snapshot.verdict ==
           Android14ProtocolEvidenceVerdict::kProbeCompatible);

    Android14ProtocolEvidence rejected;
    rejected.record(observation(
            "connect_device", ParcelObservationStatus::kMalformedPayload));
    snapshot = rejected.snapshot();
    assert(snapshot.seenMask != 0);
    assert(snapshot.invalidMask != 0);
    assert(snapshot.verdict == Android14ProtocolEvidenceVerdict::kRejected);

    Android14ProtocolEvidence ignored;
    ignored.record(observation(
            "vendor_extension", ParcelObservationStatus::kNotRoutedTransaction));
    snapshot = ignored.snapshot();
    assert(snapshot.seenMask == 0);
    assert(snapshot.verdict == Android14ProtocolEvidenceVerdict::kPending);
    return 0;
}
