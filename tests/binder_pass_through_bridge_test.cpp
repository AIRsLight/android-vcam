#include "vcam/BinderPassThroughBridge.h"

#include <cassert>
#include <cstdint>

namespace {

void* observedService = nullptr;
const void* observedData = nullptr;
void* observedReply = nullptr;
std::uint32_t observedCode = 0;
std::uint32_t observedFlags = 0;

std::int32_t original(
        void* service,
        std::uint32_t code,
        const void* dataParcel,
        void* replyParcel,
        std::uint32_t flags) {
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
    assert(bridge.invoke(&service, 4, &data, &reply, 8) ==
           vcam::runtime::BinderPassThroughBridge::kOriginalNotBound);
    assert(!bridge.bindOnce(nullptr));
    assert(bridge.bindOnce(&original));
    assert(bridge.isBound());
    assert(!bridge.bindOnce(&original));

    assert(bridge.invoke(&service, 4, &data, &reply, 8) == 29);
    assert(observedService == &service);
    assert(observedCode == 4);
    assert(observedData == &data);
    assert(observedReply == &reply);
    assert(observedFlags == 8);
    assert(vcam::runtime::binderPassThroughEntryAddress() != 0);
    return 0;
}
