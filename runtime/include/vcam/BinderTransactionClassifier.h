#pragma once

#include <cstdint>
#include <string>

#include "vcam/RuntimeAbiGuard.h"

namespace vcam::runtime {

enum class BinderPayloadShape {
    kPassThrough = 0,
    kConnectApi1,
    kConnectDevice,
    kStringCameraId,
    kIntegerCameraId,
    kListener,
    kListenerRemoval,
    kConcurrentIds,
    kConcurrentSessionConfiguration,
};

struct BinderTransactionClass {
    std::uint32_t code = 0;
    std::string role;
    BinderPayloadShape payloadShape = BinderPayloadShape::kPassThrough;
    bool cameraScoped = false;
    bool carriesPackageName = false;
};

BinderTransactionClass classifyBinderTransaction(
        const AbiRecipe& recipe, std::uint32_t code);

const char* binderPayloadShapeName(BinderPayloadShape shape);

}  // namespace vcam::runtime
