#include "vcam/BinderPassThroughBridge.h"

#include <cassert>
#include <cstdint>

namespace {

void* observedService = nullptr;
const void* observedData = nullptr;
void* observedReply = nullptr;
std::uint32_t observedCode = 0;
std::uint32_t observedFlags = 0;
std::uint32_t shadowCode = 0;
const void* shadowData = nullptr;
void* shadowContext = nullptr;
bool shadowRanBeforeOriginal = false;

void shadowObserver(
        std::uint32_t code, const void* dataParcel, void* context) noexcept {
    shadowCode = code;
    shadowData = dataParcel;
    shadowContext = context;
}

std::int32_t original(
        void* service,
        std::uint32_t code,
        const void* dataParcel,
        void* replyParcel,
        std::uint32_t flags) {
    shadowRanBeforeOriginal = shadowCode == code && shadowData == dataParcel;
    observedService = service;
    observedCode = code;
    observedData = dataParcel;
    observedReply = replyParcel;
    observedFlags = flags;
    return static_cast<std::int32_t>(code + flags + 17);
}

}  // namespace

int main() {
    vcam::runtime::BinderPassThroughBridge bridge;
    int service = 1;
    int data = 2;
    int reply = 3;
    assert(!bridge.isBound());
    assert(!bridge.hasObserver());
    assert(bridge.invoke(&service, 4, &data, &reply, 8) ==
           vcam::runtime::BinderPassThroughBridge::kOriginalNotBound);
    assert(!bridge.bindOnce(nullptr));
    assert(bridge.bindOnce(&original));
    assert(bridge.isBound());
    assert(!bridge.bindOnce(&original));
    assert(!bridge.bindObserverOnce(nullptr, nullptr));
    int observerContext = 9;
    assert(bridge.bindObserverOnce(&shadowObserver, &observerContext));
    assert(bridge.hasObserver());
    assert(!bridge.bindObserverOnce(&shadowObserver, nullptr));

    assert(bridge.invoke(&service, 4, &data, &reply, 8) == 29);
    assert(observedService == &service);
    assert(observedCode == 4);
    assert(observedData == &data);
    assert(observedReply == &reply);
    assert(observedFlags == 8);
    assert(shadowCode == 4);
    assert(shadowData == &data);
    assert(shadowContext == &observerContext);
    assert(shadowRanBeforeOriginal);
    assert(vcam::runtime::binderPassThroughEntryAddress() != 0);

    shadowCode = 0;
    shadowData = nullptr;
    shadowContext = nullptr;
    shadowRanBeforeOriginal = false;
    int globalObserverContext = 10;
    assert(vcam::runtime::bindGlobalBinderShadowObserver(
            &shadowObserver, &globalObserverContext));
    assert(vcam::runtime::bindGlobalBinderPassThroughOriginal(&original));
    const auto globalEntry = reinterpret_cast<vcam::runtime::CameraServiceOnTransact>(
            vcam::runtime::binderPassThroughEntryAddress());
    assert(globalEntry(&service, 6, &data, &reply, 4) == 27);
    assert(shadowContext == &globalObserverContext);
    assert(shadowRanBeforeOriginal);
    return 0;
}
