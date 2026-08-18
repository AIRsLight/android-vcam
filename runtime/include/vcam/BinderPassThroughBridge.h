#pragma once

#include <atomic>
#include <cstdint>

namespace vcam::runtime {

using CameraServiceOnTransact = std::int32_t (*)(
        void* service,
        std::uint32_t code,
        const void* dataParcel,
        void* replyParcel,
        std::uint32_t flags);

class BinderPassThroughBridge final {
public:
    static constexpr std::int32_t kOriginalNotBound = -38;  // -ENOSYS

    bool bindOnce(CameraServiceOnTransact original);
    bool isBound() const;
    std::int32_t invoke(
            void* service,
            std::uint32_t code,
            const void* dataParcel,
            void* replyParcel,
            std::uint32_t flags) const;

private:
    std::atomic<CameraServiceOnTransact> original_{nullptr};
};

std::uintptr_t binderPassThroughEntryAddress();
bool bindGlobalBinderPassThroughOriginal(CameraServiceOnTransact original);

}  // namespace vcam::runtime
