#include "vcam/BinderTransactionClassifier.h"

#include <cassert>

int main() {
    vcam::runtime::AbiRecipe recipe;
    recipe.transactions = {
        {"connect_api1", 3},
        {"connect_device", 4},
        {"add_listener", 5},
        {"get_concurrent_camera_ids", 6},
        {"concurrent_session_support", 7},
        {"remove_listener", 8},
        {"get_camera_characteristics", 9},
        {"get_legacy_parameters", 12},
        {"supports_camera_api", 13},
        {"set_torch_mode", 16},
    };

    const auto connect = vcam::runtime::classifyBinderTransaction(recipe, 4);
    assert(connect.role == "connect_device");
    assert(connect.payloadShape == vcam::runtime::BinderPayloadShape::kConnectDevice);
    assert(connect.cameraScoped);
    assert(connect.carriesPackageName);

    const auto metadata = vcam::runtime::classifyBinderTransaction(recipe, 9);
    assert(metadata.payloadShape == vcam::runtime::BinderPayloadShape::kStringCameraId);
    assert(metadata.cameraScoped);
    assert(!metadata.carriesPackageName);

    const auto legacy = vcam::runtime::classifyBinderTransaction(recipe, 12);
    assert(legacy.payloadShape == vcam::runtime::BinderPayloadShape::kIntegerCameraId);

    const auto listener = vcam::runtime::classifyBinderTransaction(recipe, 5);
    assert(listener.payloadShape == vcam::runtime::BinderPayloadShape::kListener);
    assert(!listener.cameraScoped);

    const auto listenerRemoval =
            vcam::runtime::classifyBinderTransaction(recipe, 8);
    assert(listenerRemoval.payloadShape ==
           vcam::runtime::BinderPayloadShape::kListenerRemoval);
    assert(!listenerRemoval.cameraScoped);

    const auto concurrent = vcam::runtime::classifyBinderTransaction(recipe, 7);
    assert(concurrent.payloadShape ==
           vcam::runtime::BinderPayloadShape::kConcurrentSessionConfiguration);
    assert(concurrent.cameraScoped);

    const auto unknown = vcam::runtime::classifyBinderTransaction(recipe, 999);
    assert(unknown.role.empty());
    assert(unknown.payloadShape == vcam::runtime::BinderPayloadShape::kPassThrough);
    assert(!unknown.cameraScoped);
    return 0;
}
