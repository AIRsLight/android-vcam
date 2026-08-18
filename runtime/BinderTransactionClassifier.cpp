#include "vcam/BinderTransactionClassifier.h"

#include <algorithm>

namespace vcam::runtime {
namespace {

BinderTransactionClass classificationForRole(
        std::uint32_t code, const std::string& role) {
    BinderTransactionClass result;
    result.code = code;
    result.role = role;
    if (role == "connect_api1") {
        result.payloadShape = BinderPayloadShape::kConnectApi1;
        result.cameraScoped = true;
        result.carriesPackageName = true;
    } else if (role == "connect_device") {
        result.payloadShape = BinderPayloadShape::kConnectDevice;
        result.cameraScoped = true;
        result.carriesPackageName = true;
    } else if (role == "get_camera_characteristics" ||
               role == "supports_camera_api" ||
               role == "set_torch_mode" ||
               role == "turn_on_torch_with_strength" ||
               role == "get_torch_strength") {
        result.payloadShape = BinderPayloadShape::kStringCameraId;
        result.cameraScoped = true;
    } else if (role == "get_legacy_parameters") {
        result.payloadShape = BinderPayloadShape::kIntegerCameraId;
        result.cameraScoped = true;
    } else if (role == "add_listener") {
        result.payloadShape = BinderPayloadShape::kListener;
    } else if (role == "get_concurrent_camera_ids") {
        result.payloadShape = BinderPayloadShape::kConcurrentIds;
    } else if (role == "concurrent_session_support") {
        result.payloadShape = BinderPayloadShape::kConcurrentSessionConfiguration;
        result.cameraScoped = true;
    }
    return result;
}

}  // namespace

BinderTransactionClass classifyBinderTransaction(
        const AbiRecipe& recipe, std::uint32_t code) {
    const auto transaction = std::find_if(
            recipe.transactions.begin(), recipe.transactions.end(),
            [&](const BinderTransaction& candidate) { return candidate.code == code; });
    if (transaction == recipe.transactions.end()) {
        BinderTransactionClass result;
        result.code = code;
        return result;
    }
    return classificationForRole(code, transaction->role);
}

const char* binderPayloadShapeName(BinderPayloadShape shape) {
    switch (shape) {
        case BinderPayloadShape::kPassThrough: return "pass_through";
        case BinderPayloadShape::kConnectApi1: return "connect_api1";
        case BinderPayloadShape::kConnectDevice: return "connect_device";
        case BinderPayloadShape::kStringCameraId: return "string_camera_id";
        case BinderPayloadShape::kIntegerCameraId: return "integer_camera_id";
        case BinderPayloadShape::kListener: return "listener";
        case BinderPayloadShape::kConcurrentIds: return "concurrent_ids";
        case BinderPayloadShape::kConcurrentSessionConfiguration:
            return "concurrent_session_configuration";
    }
    return "unknown";
}

}  // namespace vcam::runtime
