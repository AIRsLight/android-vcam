#include <assert.h>

#include <cstring>

#include "vcam/Android14CameraServiceProfile.h"

int main() {
    using vcam::runtime::Android14CameraServiceProtocolConfidence;
    using vcam::runtime::android14CameraServiceProtocolConfidenceName;
    using vcam::runtime::kAndroid14InitialCandidateProfileName;
    using vcam::runtime::kNx769jAndroid14Fingerprint;
    using vcam::runtime::kNx769jAndroid14ProfileName;
    using vcam::runtime::selectAndroid14CameraServiceProtocol;

    const auto qualified =
            selectAndroid14CameraServiceProtocol(34, kNx769jAndroid14Fingerprint);
    assert(qualified.observationAllowed);
    assert(qualified.routingAllowed);
    assert(qualified.confidence ==
           Android14CameraServiceProtocolConfidence::kQualified);
    assert(std::strcmp(qualified.profileName, kNx769jAndroid14ProfileName) == 0);
    assert(qualified.recipe.schema == 2);
    assert(qualified.recipe.transactions.size() == 12);

    const auto candidate = selectAndroid14CameraServiceProtocol(
            34, "vendor/product/device:14/UP1A/build:user/release-keys");
    assert(candidate.observationAllowed);
    assert(!candidate.routingAllowed);
    assert(candidate.confidence ==
           Android14CameraServiceProtocolConfidence::kProbeCandidate);
    assert(std::strcmp(candidate.profileName,
                       kAndroid14InitialCandidateProfileName) == 0);
    assert(candidate.recipe.transactions.size() == 12);

    const auto unsupported =
            selectAndroid14CameraServiceProtocol(33, kNx769jAndroid14Fingerprint);
    assert(!unsupported.observationAllowed);
    assert(!unsupported.routingAllowed);
    assert(unsupported.confidence ==
           Android14CameraServiceProtocolConfidence::kUnsupported);
    assert(unsupported.recipe.transactions.empty());
    assert(std::strcmp(android14CameraServiceProtocolConfidenceName(
                               candidate.confidence),
                       "probe_candidate") == 0);
    return 0;
}
