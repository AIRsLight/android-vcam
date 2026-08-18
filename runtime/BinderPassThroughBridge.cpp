#include "vcam/BinderPassThroughBridge.h"

namespace vcam::runtime {
namespace {

BinderPassThroughBridge gBridge;

extern "C" __attribute__((noinline, visibility("hidden"))) std::int32_t
vcam_camera_service_on_transact_bridge(
        void* service,
        std::uint32_t code,
        const void* dataParcel,
        void* replyParcel,
        std::uint32_t flags) {
    return gBridge.invoke(service, code, dataParcel, replyParcel, flags);
}

}  // namespace

bool BinderPassThroughBridge::bindOnce(CameraServiceOnTransact original) {
    if (original == nullptr) {
        return false;
    }
    CameraServiceOnTransact expected = nullptr;
    return original_.compare_exchange_strong(
            expected, original, std::memory_order_release, std::memory_order_relaxed);
}

bool BinderPassThroughBridge::isBound() const {
    return original_.load(std::memory_order_acquire) != nullptr;
}

std::int32_t BinderPassThroughBridge::invoke(
        void* service,
        std::uint32_t code,
        const void* dataParcel,
        void* replyParcel,
        std::uint32_t flags) const {
    const CameraServiceOnTransact original = original_.load(std::memory_order_acquire);
    if (original == nullptr) {
        return kOriginalNotBound;
    }
    return original(service, code, dataParcel, replyParcel, flags);
}

std::uintptr_t binderPassThroughEntryAddress() {
    return reinterpret_cast<std::uintptr_t>(&vcam_camera_service_on_transact_bridge);
}

bool bindGlobalBinderPassThroughOriginal(CameraServiceOnTransact original) {
    return gBridge.bindOnce(original);
}

}  // namespace vcam::runtime
