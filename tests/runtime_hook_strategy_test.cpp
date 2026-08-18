#include "vcam/RuntimeHookStrategy.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

int main() {
    const std::string onTransact =
            "_ZN7android13CameraService10onTransactEjRKNS_6ParcelEPS1_j";
    vcam::runtime::AbiRecipe recipe;
    recipe.schema = 2;
    recipe.architecture = "arm64";
    recipe.symbols.push_back({onTransact, {
            0x3f, 0x23, 0x03, 0xd5,
            0xff, 0x83, 0x02, 0xd1,
            0xfd, 0x7b, 0x04, 0xa9,
            0xfc, 0x6f, 0x05, 0xa9,
    }});
    recipe.hooks.push_back({"on_transact", onTransact});
    recipe.transactions.push_back({"connect_device", 4});
    recipe.transactions.push_back({"get_camera_characteristics", 9});

    vcam::runtime::ProbeResult probe;
    probe.status = vcam::runtime::ProbeStatus::kAllowed;
    probe.resolvedSymbols.push_back({onTransact, 0x7100101798});

    auto plan = vcam::runtime::planOnTransactStrategy(
            recipe, probe, 0x7200204000);
    assert(plan.status == vcam::runtime::HookStrategyStatus::kReady);
    assert(plan.targetAddress == 0x7100101798);
    assert(plan.transactions.size() == 2);
    assert(plan.patch.resumeAddress == 0x71001017a8);
    const auto connectDevice = std::find_if(
            plan.transactions.begin(), plan.transactions.end(),
            [](const vcam::runtime::BinderTransaction& transaction) {
                return transaction.role == "connect_device";
            });
    assert(connectDevice != plan.transactions.end());
    assert(connectDevice->code == 4);

    probe.status = vcam::runtime::ProbeStatus::kBuildIdMismatch;
    assert(vcam::runtime::planOnTransactStrategy(
            recipe, probe, 0x7200204000).status ==
           vcam::runtime::HookStrategyStatus::kAbiNotAllowed);
    probe.status = vcam::runtime::ProbeStatus::kAllowed;

    probe.resolvedSymbols.clear();
    assert(vcam::runtime::planOnTransactStrategy(
            recipe, probe, 0x7200204000).status ==
           vcam::runtime::HookStrategyStatus::kHookSymbolNotResolved);

    probe.resolvedSymbols.push_back({onTransact, 0x7100101798});
    recipe.symbols[0].codePrefix[0] = 0x00;
    recipe.symbols[0].codePrefix[1] = 0x00;
    recipe.symbols[0].codePrefix[2] = 0x00;
    recipe.symbols[0].codePrefix[3] = 0x14;
    assert(vcam::runtime::planOnTransactStrategy(
            recipe, probe, 0x7200204000).status ==
           vcam::runtime::HookStrategyStatus::kPatchNotRelocatable);
    return 0;
}
