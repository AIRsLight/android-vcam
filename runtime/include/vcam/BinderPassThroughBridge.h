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

using BinderTransactionObserver = void (*)(
        std::uint32_t code,
        const void* dataParcel,
        void* context) noexcept;

class BinderPassThroughBridge final {
public:
    static constexpr std::int32_t kOriginalNotBound = -38;  // -ENOSYS

    bool bindOnce(CameraServiceOnTransact original);
    bool isBound() const;
    // The observer and its context are published once and cannot be replaced.
    // The context must remain valid for the lifetime of this bridge.
    bool bindObserverOnce(BinderTransactionObserver observer, void* context);
    bool hasObserver() const;
    std::int32_t invoke(
            void* service,
            std::uint32_t code,
            const void* dataParcel,
            void* replyParcel,
            std::uint32_t flags) const;

private:
    std::atomic<CameraServiceOnTransact> original_{nullptr};
    std::atomic<bool> observerBindingStarted_{false};
    std::atomic<BinderTransactionObserver> observer_{nullptr};
    void* observerContext_ = nullptr;
};

std::uintptr_t binderPassThroughEntryAddress();
bool bindGlobalBinderPassThroughOriginal(CameraServiceOnTransact original);
bool bindGlobalBinderShadowObserver(BinderTransactionObserver observer, void* context);

}  // namespace vcam::runtime
