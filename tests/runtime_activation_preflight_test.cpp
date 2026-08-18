#include "vcam/RuntimeActivationPreflight.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr std::uintptr_t kTargetAddress = 0x1100;
const std::vector<std::uint8_t> kOriginal = {
    0x3f, 0x23, 0x03, 0xd5,
    0xff, 0x83, 0x02, 0xd1,
    0xfd, 0x7b, 0x04, 0xa9,
    0xfc, 0x6f, 0x05, 0xa9,
};

vcam::runtime::Arm64PatchPlan plan(std::uintptr_t target = kTargetAddress) {
    return vcam::runtime::planArm64InlineHook(target, 0x3000, kOriginal);
}

vcam::runtime::ActivationSnapshot snapshot() {
    vcam::runtime::ActivationSnapshot value;
    value.mappings.push_back({
        0x1000, 0x2000, true, false, true, true,
        "/apex/com.android.media/lib64/libcameraservice.so",
    });
    value.mappings.push_back({
        0x2000, 0x3000, true, true, false, true, "[anon:.bss]",
    });
    value.threadIds = {100, 101, 102};
    value.currentThreadId = 101;
    value.observedTargetBytes = kOriginal;
    return value;
}

__attribute__((noinline)) int liveProbeFunction(int value) {
    return value + 1;
}

}  // namespace

int main() {
    const std::string mapsText =
            "1000-2000 r-xp 00000000 00:01 7 /system/lib64/libcameraservice.so\n"
            "2000-3000 rw-p 00001000 00:01 7 /system/lib64/path with spaces.so\n";
    std::vector<vcam::runtime::ProcessMapEntry> mappings;
    std::string error;
    assert(vcam::runtime::parseProcMapsText(mapsText, &mappings, &error));
    assert(error.empty());
    assert(mappings.size() == 2);
    assert(mappings[0].readable && mappings[0].executable &&
           !mappings[0].writable && mappings[0].privateMapping);
    assert(mappings[1].path == "/system/lib64/path with spaces.so");
    assert(!vcam::runtime::parseProcMapsText("not a map\n", &mappings, &error));
    assert(!error.empty());
    assert(!vcam::runtime::parseProcMapsText(
            "2000-3000 r-xp 0 00:01 1 /second.so\n"
            "1000-1800 r-xp 0 00:01 2 /first.so\n",
            &mappings, &error));

    const auto ready = vcam::runtime::evaluateActivationPreflight(
            plan(), kTargetAddress, "libcameraservice.so", snapshot());
    assert(ready);
    assert(ready.threadCount == 3);
    assert(ready.targetMapping.start == 0x1000);

    {
        auto value = snapshot();
        value.mappings[0].writable = true;
        assert(vcam::runtime::evaluateActivationPreflight(
                plan(), kTargetAddress, "libcameraservice.so", value).status ==
               vcam::runtime::ActivationPreflightStatus::kTargetPermissionMismatch);
    }
    {
        auto value = snapshot();
        assert(vcam::runtime::evaluateActivationPreflight(
                plan(), kTargetAddress, "libcamera_client.so", value).status ==
               vcam::runtime::ActivationPreflightStatus::kTargetModuleMismatch);
    }
    {
        auto value = snapshot();
        value.observedTargetBytes[0] ^= 0xff;
        assert(vcam::runtime::evaluateActivationPreflight(
                plan(), kTargetAddress, "libcameraservice.so", value).status ==
               vcam::runtime::ActivationPreflightStatus::kTargetBytesMismatch);
    }
    {
        auto value = snapshot();
        value.threadIds = {100, 102};
        assert(vcam::runtime::evaluateActivationPreflight(
                plan(), kTargetAddress, "libcameraservice.so", value).status ==
               vcam::runtime::ActivationPreflightStatus::kCurrentThreadMissing);
    }
    {
        auto value = snapshot();
        value.threadIds = {102, 101, 100};
        assert(vcam::runtime::evaluateActivationPreflight(
                plan(), kTargetAddress, "libcameraservice.so", value).status ==
               vcam::runtime::ActivationPreflightStatus::kThreadInventoryInvalid);
    }
    {
        auto value = snapshot();
        value.collectionError = "denied";
        assert(vcam::runtime::evaluateActivationPreflight(
                plan(), kTargetAddress, "libcameraservice.so", value).status ==
               vcam::runtime::ActivationPreflightStatus::kSnapshotError);
    }
    {
        auto value = snapshot();
        constexpr std::uintptr_t splitTarget = 0x1ff8;
        assert(vcam::runtime::evaluateActivationPreflight(
                plan(splitTarget), splitTarget, "libcameraservice.so", value).status ==
               vcam::runtime::ActivationPreflightStatus::kTargetRangeSplit);
    }

    const auto live = vcam::runtime::collectCurrentProcessActivationSnapshot(
            reinterpret_cast<std::uintptr_t>(&liveProbeFunction), 1);
    assert(live.collectionError.empty());
    assert(live.observedTargetBytes.size() == 1);
    assert(live.currentThreadId > 0);
    assert(std::binary_search(
            live.threadIds.begin(), live.threadIds.end(), live.currentThreadId));
    return 0;
}
