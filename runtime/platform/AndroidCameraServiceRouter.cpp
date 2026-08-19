#define LOG_TAG "VcamCameraRouter"

#include "vcam/Android14BinderShadowObserver.h"
#include "vcam/Android14CameraServiceProfile.h"
#include "vcam/AndroidCameraServiceRouter.h"
#include "vcam/CameraServerBootstrapPaths.h"
#include "vcam/CameraServiceRouterMode.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <pthread.h>
#include <string>
#include <sys/system_properties.h>
#include <utility>
#include <unistd.h>

#include <binder/Binder.h>
#include <binder/IBinder.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <log/log.h>
#include <utils/String16.h>

namespace vcam::runtime {
namespace {

constexpr char kCameraServiceName[] = "media.camera";
constexpr char16_t kCameraServiceDescriptor[] = u"android.hardware.ICameraService";
constexpr std::uint32_t kServiceWaitAttempts = 3000;
constexpr useconds_t kServiceWaitDelayMicroseconds = 10000;

std::atomic<AndroidCameraServiceRouterState> gState {
        AndroidCameraServiceRouterState::kNotStarted};
android::sp<android::IBinder> gOriginalService;
android::sp<android::IBinder> gRouterService;
std::atomic<Android14BinderShadowObserver*> gShadowObserver {nullptr};
std::atomic<const char*> gObserverProfile {"none"};

std::string buildFingerprint() {
    char value[PROP_VALUE_MAX] {};
    const int length = __system_property_get("ro.build.fingerprint", value);
    return length > 0 ? std::string(value, static_cast<std::size_t>(length))
                      : std::string();
}

class CameraServicePassThrough final : public android::BBinder {
public:
    CameraServicePassThrough(
            android::sp<android::IBinder> target,
            AbiRecipe observationRecipe)
        : target_(std::move(target)), observer_(std::move(observationRecipe)) {
        gShadowObserver.store(&observer_, std::memory_order_release);
    }

    const android::String16& getInterfaceDescriptor() const override {
        static const android::String16 descriptor(kCameraServiceDescriptor);
        return descriptor;
    }

protected:
    android::status_t onTransact(
            std::uint32_t code,
            const android::Parcel& data,
            android::Parcel* reply,
            std::uint32_t flags) override {
        observer_.observe(code, &data);
        // target_ is a local BBinder in this process. Calling transact() here
        // reaches its onTransact() directly without a nested Binder driver call,
        // so IPCThreadState retains the original app PID/UID/SID.
        return target_->transact(code, data, reply, flags);
    }

private:
    const android::sp<android::IBinder> target_;
    Android14BinderShadowObserver observer_;
};

void setTerminalState(AndroidCameraServiceRouterState state, const char* detail) {
    gState.store(state, std::memory_order_release);
    ALOGI("router state=%s detail=%s",
          androidCameraServiceRouterStateName(state), detail);
    if (state == AndroidCameraServiceRouterState::kPreflightReady ||
        state == AndroidCameraServiceRouterState::kPassThroughReady) {
        if (unlink(bootstrap::kPendingPath) != 0 && errno != ENOENT) {
            ALOGE("could not clear successful bootstrap marker: errno=%d", errno);
        }
    }
}

void* routerMonitor(void*) {
    const CameraServiceRouterMode mode =
            parseCameraServiceRouterMode(std::getenv("VCAM_BINDER_ROUTER_MODE"));
    if (mode == CameraServiceRouterMode::kDisabled) {
        setTerminalState(AndroidCameraServiceRouterState::kDisabled,
                         "disabled by explicit environment mode");
        return nullptr;
    }
    if (mode == CameraServiceRouterMode::kInvalid) {
        setTerminalState(AndroidCameraServiceRouterState::kInvalidMode,
                         "unrecognized VCAM_BINDER_ROUTER_MODE");
        return nullptr;
    }

    gState.store(AndroidCameraServiceRouterState::kWaitingForService,
                 std::memory_order_release);
    const android::sp<android::IServiceManager> manager =
            android::defaultServiceManager();
    if (manager == nullptr) {
        setTerminalState(AndroidCameraServiceRouterState::kServiceManagerUnavailable,
                         "default service manager is null");
        return nullptr;
    }

    const android::String16 serviceName(kCameraServiceName);
    android::sp<android::IBinder> original;
    for (std::uint32_t attempt = 0; attempt < kServiceWaitAttempts; ++attempt) {
        original = manager->checkService(serviceName);
        if (original != nullptr) {
            break;
        }
        usleep(kServiceWaitDelayMicroseconds);
    }
    if (original == nullptr) {
        setTerminalState(AndroidCameraServiceRouterState::kServiceTimeout,
                         "media.camera was not published within 30 seconds");
        return nullptr;
    }
    if (original->localBinder() == nullptr) {
        setTerminalState(AndroidCameraServiceRouterState::kOriginalServiceNotLocal,
                         "media.camera did not resolve to a local BBinder");
        return nullptr;
    }
    if (original->getInterfaceDescriptor() !=
        android::String16(kCameraServiceDescriptor)) {
        setTerminalState(AndroidCameraServiceRouterState::kWrongInterface,
                         "media.camera has an unexpected interface descriptor");
        return nullptr;
    }

    gOriginalService = original;
    if (mode == CameraServiceRouterMode::kPreflight) {
        setTerminalState(AndroidCameraServiceRouterState::kPreflightReady,
                         "local CameraService Binder verified; no registration changed");
        return nullptr;
    }

    AbiRecipe observationRecipe;
    const std::string fingerprint = buildFingerprint();
    if (matchesNx769jAndroid14CameraServiceProfile(fingerprint)) {
        observationRecipe = makeNx769jAndroid14CameraServiceRecipe();
        gObserverProfile.store(kNx769jAndroid14ProfileName,
                               std::memory_order_release);
        ALOGI("enabled read-only Binder observer profile=%s",
              kNx769jAndroid14ProfileName);
    } else {
        ALOGW("no qualified read-only Binder observer for this build; "
              "transactions remain unparsed");
    }
    android::sp<android::IBinder> router =
            android::sp<CameraServicePassThrough>::make(
                    original, std::move(observationRecipe));
    const android::status_t registration = manager->addService(
            serviceName,
            router,
            false,
            android::IServiceManager::DUMP_FLAG_PRIORITY_DEFAULT);
    if (registration != android::OK) {
        setTerminalState(AndroidCameraServiceRouterState::kRegistrationFailed,
                         "service manager rejected the pass-through Binder");
        return nullptr;
    }
    const android::sp<android::IBinder> observed = manager->checkService(serviceName);
    if (observed.get() != router.get()) {
        setTerminalState(
                AndroidCameraServiceRouterState::kRegistrationVerificationFailed,
                "media.camera did not resolve to the registered pass-through Binder");
        return nullptr;
    }
    gRouterService = std::move(router);
    setTerminalState(AndroidCameraServiceRouterState::kPassThroughReady,
                     "media.camera now forwards to the original local Binder");
    return nullptr;
}

__attribute__((constructor)) void startRouterMonitor() {
    pthread_t thread {};
    const int status = pthread_create(&thread, nullptr, &routerMonitor, nullptr);
    if (status != 0) {
        setTerminalState(AndroidCameraServiceRouterState::kThreadStartFailed,
                         "could not start the CameraService monitor thread");
        return;
    }
    pthread_detach(thread);
}

}  // namespace

const char* androidCameraServiceRouterStateName(
        AndroidCameraServiceRouterState state) noexcept {
    switch (state) {
        case AndroidCameraServiceRouterState::kNotStarted: return "not_started";
        case AndroidCameraServiceRouterState::kWaitingForService:
            return "waiting_for_service";
        case AndroidCameraServiceRouterState::kPreflightReady:
            return "preflight_ready";
        case AndroidCameraServiceRouterState::kPassThroughReady:
            return "passthrough_ready";
        case AndroidCameraServiceRouterState::kDisabled: return "disabled";
        case AndroidCameraServiceRouterState::kInvalidMode: return "invalid_mode";
        case AndroidCameraServiceRouterState::kThreadStartFailed:
            return "thread_start_failed";
        case AndroidCameraServiceRouterState::kServiceManagerUnavailable:
            return "service_manager_unavailable";
        case AndroidCameraServiceRouterState::kServiceTimeout: return "service_timeout";
        case AndroidCameraServiceRouterState::kOriginalServiceNotLocal:
            return "original_service_not_local";
        case AndroidCameraServiceRouterState::kWrongInterface: return "wrong_interface";
        case AndroidCameraServiceRouterState::kRegistrationFailed:
            return "registration_failed";
        case AndroidCameraServiceRouterState::kRegistrationVerificationFailed:
            return "registration_verification_failed";
    }
    return "unknown";
}

}  // namespace vcam::runtime

extern "C" __attribute__((visibility("default"))) int
vcam_camera_service_router_state() {
    return static_cast<int>(vcam::runtime::gState.load(std::memory_order_acquire));
}

extern "C" __attribute__((visibility("default"))) const char*
vcam_camera_service_router_state_name() {
    return vcam::runtime::androidCameraServiceRouterStateName(
            vcam::runtime::gState.load(std::memory_order_acquire));
}

extern "C" __attribute__((visibility("default"))) const char*
vcam_camera_service_router_observer_profile() {
    return vcam::runtime::gObserverProfile.load(std::memory_order_acquire);
}

namespace {

vcam::runtime::Android14ShadowObservationStats observerStats() {
    const auto* observer =
            vcam::runtime::gShadowObserver.load(std::memory_order_acquire);
    return observer == nullptr
            ? vcam::runtime::Android14ShadowObservationStats {}
            : observer->stats();
}

}  // namespace

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_observed_transactions() {
    return observerStats().observed;
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_ignored_transactions() {
    return observerStats().ignored;
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_rejected_transactions() {
    return observerStats().rejected;
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_unsupported_transactions() {
    return observerStats().unsupported;
}
