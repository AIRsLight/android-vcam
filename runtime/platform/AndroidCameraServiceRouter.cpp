#define LOG_TAG "VcamCameraRouter"

#include "vcam/Android14BinderShadowObserver.h"
#include "vcam/Android14CameraDeviceUserRouter.h"
#include "vcam/Android14CameraIdRewriter.h"
#include "vcam/Android14CameraListenerFilter.h"
#include "vcam/Android14CameraServiceProfile.h"
#include "vcam/AndroidCameraServiceRouter.h"
#include "vcam/CameraServerBootstrapPaths.h"
#include "vcam/CameraCallerIdentityClassifier.h"
#include "vcam/CameraServiceRouterMode.h"
#include "vcam/ScopedCameraRouter.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <sys/system_properties.h>
#include <sys/stat.h>
#include <utility>
#include <unistd.h>

#include <binder/Binder.h>
#include <binder/IBinder.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/PermissionController.h>
#include <binder/Status.h>
#include <log/log.h>
#include <utils/String16.h>
#include <utils/String8.h>
#include <utils/Vector.h>

namespace vcam::runtime {
namespace {

constexpr char kCameraServiceName[] = "media.camera";
constexpr char16_t kCameraServiceDescriptor[] = u"android.hardware.ICameraService";
constexpr std::uint32_t kServiceWaitAttempts = 3000;
constexpr useconds_t kServiceWaitDelayMicroseconds = 10000;
constexpr unsigned int kStatsPublishIntervalSeconds = 1;
constexpr char kRuntimeRoutesPath[] = "/data/vendor/camera/vcam/routes.tsv";
constexpr char kRuntimeRoutesDisabledPath[] =
        "/data/vendor/camera/vcam/routes.tsv.disabled";
constexpr char kRuntimeProvidersPath[] = "/data/vendor/camera/vcam/providers";
constexpr char kRuntimeTopologyPath[] =
        "/data/vendor/camera/vcam/topology.conf";

android::status_t rejectCameraRequest(
        android::Parcel* reply, const char* message) {
    if (reply == nullptr) return android::BAD_VALUE;
    reply->freeData();
    return android::binder::Status::fromExceptionCode(
                   android::binder::Status::EX_ILLEGAL_ARGUMENT, message)
            .writeToParcel(reply);
}

bool routingPolicyActive() {
    struct stat info {};
    return access(kRuntimeRoutesDisabledPath, F_OK) != 0 &&
            stat(kRuntimeRoutesPath, &info) == 0 && info.st_size > 0;
}

bool routingMapsReady() {
    return access(::vcam::ScopedCameraRouter::kDefaultTargetMapPath, R_OK) == 0 &&
            access(::vcam::ScopedCameraRouter::kDefaultCamera1TargetMapPath,
                   R_OK) == 0 &&
            access(::vcam::ScopedCameraRouter::kDefaultCamera1MapPath, R_OK) == 0 &&
            access(kRuntimeTopologyPath, R_OK) == 0;
}

std::atomic<AndroidCameraServiceRouterState> gState {
        AndroidCameraServiceRouterState::kNotStarted};
android::sp<android::IBinder> gOriginalService;
android::sp<android::IBinder> gRouterService;
std::atomic<Android14BinderShadowObserver*> gShadowObserver {nullptr};
std::atomic<const char*> gObserverProfile {"none"};
std::atomic<std::uint64_t> gVerifiedPackageClaims {0};
std::atomic<std::uint64_t> gRejectedPackageClaims {0};
std::atomic<std::uint64_t> gUnavailablePackageLookups {0};
std::atomic<std::uint64_t> gUniqueUidPackageResolutions {0};
std::atomic<std::uint64_t> gAmbiguousUidPackageResolutions {0};
std::atomic<std::uint64_t> gUnavailableUidPackageResolutions {0};
std::atomic<std::uint64_t> gPackageRouteCandidates {0};
std::atomic<std::uint64_t> gGlobalRouteCandidates {0};
std::atomic<std::uint64_t> gPhysicalRouteDecisions {0};
std::atomic<std::uint64_t> gUnavailableRouteProviders {0};
std::atomic<std::uint64_t> gPhysicalRewriteAttempts {0};
std::atomic<std::uint64_t> gPhysicalRewriteSuccesses {0};
std::atomic<std::uint64_t> gPhysicalRewriteFailures {0};
std::atomic<std::uint64_t> gInternalCameraRequestsRejected {0};

std::string buildFingerprint() {
    char value[PROP_VALUE_MAX] {};
    const int length = __system_property_get("ro.build.fingerprint", value);
    return length > 0 ? std::string(value, static_cast<std::size_t>(length))
                      : std::string();
}

int buildSdk() {
    char value[PROP_VALUE_MAX] {};
    const int length = __system_property_get("ro.build.version.sdk", value);
    if (length <= 0) return -1;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    return end != value && end != nullptr && *end == '\0' && parsed >= 0 &&
                   parsed <= 1000
            ? static_cast<int>(parsed) : -1;
}

void* statsPublisher(void*) {
    for (;;) {
        const Android14BinderShadowObserver* observer =
                gShadowObserver.load(std::memory_order_acquire);
        const Android14ShadowObservationStats stats = observer == nullptr
                ? Android14ShadowObservationStats {}
                : observer->stats();
        char payload[1536] {};
        const int length = std::snprintf(
                payload,
                sizeof(payload),
                "schema=2\n"
                "state=%s\n"
                "profile=%s\n"
                "protocol_verdict=%s\n"
                "protocol_required_mask=0x%016llx\n"
                "protocol_seen_mask=0x%016llx\n"
                "protocol_valid_mask=0x%016llx\n"
                "protocol_invalid_mask=0x%016llx\n"
                "protocol_unsupported_mask=0x%016llx\n"
                "transactions_total=%llu\n"
                "transactions_observed=%llu\n"
                "transactions_ignored=%llu\n"
                "transactions_rejected=%llu\n"
                "transactions_unsupported=%llu\n"
                "identity_claimed_package=%llu\n"
                "identity_uid_only=%llu\n"
                "identity_unavailable=%llu\n"
                "package_claims_verified=%llu\n"
                "package_claims_rejected=%llu\n"
                "package_lookups_unavailable=%llu\n"
                "uid_packages_unique=%llu\n"
                "uid_packages_ambiguous=%llu\n"
                "uid_packages_unavailable=%llu\n"
                "route_candidates_package=%llu\n"
                "route_candidates_global=%llu\n"
                "route_decisions_physical=%llu\n"
                "route_providers_unavailable=%llu\n"
                "physical_rewrite_attempts=%llu\n"
                "physical_rewrite_successes=%llu\n"
                "physical_rewrite_failures=%llu\n"
                "device_user_wrappers=%llu\n"
                "request_batches_rewritten=%llu\n"
                "requests_rewritten=%llu\n"
                "request_batches_skipped=%llu\n"
                "listener_wrappers=%llu\n"
                "listener_callbacks_filtered=%llu\n"
                "listener_status_records_filtered=%llu\n"
                "internal_camera_requests_rejected=%llu\n",
                androidCameraServiceRouterStateName(
                        gState.load(std::memory_order_acquire)),
                gObserverProfile.load(std::memory_order_acquire),
                android14ProtocolEvidenceVerdictName(
                        stats.protocolEvidence.verdict),
                static_cast<unsigned long long>(
                        stats.protocolEvidence.requiredMask),
                static_cast<unsigned long long>(
                        stats.protocolEvidence.seenMask),
                static_cast<unsigned long long>(
                        stats.protocolEvidence.validMask),
                static_cast<unsigned long long>(
                        stats.protocolEvidence.invalidMask),
                static_cast<unsigned long long>(
                        stats.protocolEvidence.unsupportedMask),
                static_cast<unsigned long long>(stats.total),
                static_cast<unsigned long long>(stats.observed),
                static_cast<unsigned long long>(stats.ignored),
                static_cast<unsigned long long>(stats.rejected),
                static_cast<unsigned long long>(stats.unsupported),
                static_cast<unsigned long long>(stats.claimedPackage),
                static_cast<unsigned long long>(stats.uidOnly),
                static_cast<unsigned long long>(stats.identityUnavailable),
                static_cast<unsigned long long>(
                        gVerifiedPackageClaims.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gRejectedPackageClaims.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gUnavailablePackageLookups.load(
                                std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gUniqueUidPackageResolutions.load(
                                std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gAmbiguousUidPackageResolutions.load(
                                std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gUnavailableUidPackageResolutions.load(
                                std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gPackageRouteCandidates.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gGlobalRouteCandidates.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gPhysicalRouteDecisions.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gUnavailableRouteProviders.load(
                                std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gPhysicalRewriteAttempts.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gPhysicalRewriteSuccesses.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        gPhysicalRewriteFailures.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                        android14CameraDeviceUserWrappers()),
                static_cast<unsigned long long>(
                        android14CameraRequestBatchesRewritten()),
                static_cast<unsigned long long>(
                        android14CameraRequestsRewritten()),
                static_cast<unsigned long long>(
                        android14CameraRequestBatchesSkipped()),
                static_cast<unsigned long long>(
                        android14CameraListenerWrappers()),
                static_cast<unsigned long long>(
                        android14CameraListenerCallbacksFiltered()),
                static_cast<unsigned long long>(
                        android14CameraStatusRecordsFiltered()),
                static_cast<unsigned long long>(
                        gInternalCameraRequestsRejected.load(
                                std::memory_order_relaxed)));
        if (length > 0 && static_cast<std::size_t>(length) < sizeof(payload)) {
            const int fd = open(
                    bootstrap::kRouterStatsPath,
                    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                    0600);
            if (fd >= 0) {
                const ssize_t written = write(
                        fd, payload, static_cast<std::size_t>(length));
                if (written != static_cast<ssize_t>(length)) {
                    ALOGW("short router stats write: expected=%d actual=%zd",
                          length, written);
                }
                close(fd);
            }
        }
        sleep(kStatsPublishIntervalSeconds);
    }
}

void startStatsPublisher() {
    pthread_t thread {};
    const int status = pthread_create(&thread, nullptr, &statsPublisher, nullptr);
    if (status != 0) {
        ALOGW("could not start router stats publisher: status=%d", status);
        return;
    }
    pthread_detach(thread);
}

class CameraServicePassThrough final : public android::BBinder {
public:
    CameraServicePassThrough(
            android::sp<android::IBinder> target,
            AbiRecipe observationRecipe,
            bool physicalRoutingEnabled)
        : target_(std::move(target)),
          observer_(std::move(observationRecipe)),
          physicalRoutingEnabled_(physicalRoutingEnabled) {
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
        const ParcelObservation observation = observer_.observe(code, &data);
        if (physicalRoutingEnabled_ &&
            observation.status == ParcelObservationStatus::kObserved &&
            (observation.transaction.payloadShape == BinderPayloadShape::kListener ||
             observation.transaction.payloadShape ==
                     BinderPayloadShape::kListenerRemoval)) {
            const bool addingListener =
                    observation.transaction.payloadShape ==
                    BinderPayloadShape::kListener;
            android::Parcel rewrittenListenerRequest;
            const CameraListenerRequestRouteStatus requestStatus =
                    addingListener
                    ? wrapAndroid14CameraListenerRequest(
                              data, &rewrittenListenerRequest)
                    : wrapAndroid14CameraListenerRemovalRequest(
                              data, &rewrittenListenerRequest);
            if (requestStatus == CameraListenerRequestRouteStatus::kWrapped) {
                const android::status_t forwardStatus = target_->transact(
                        code, rewrittenListenerRequest, reply, flags);
                if (addingListener && forwardStatus == android::OK) {
                    const CameraStatusReplyFilterStatus filterStatus =
                            filterAndroid14CameraStatusReply(reply);
                    if (filterStatus !=
                                CameraStatusReplyFilterStatus::kFiltered &&
                        filterStatus !=
                                CameraStatusReplyFilterStatus::kUnchanged &&
                        filterStatus !=
                                CameraStatusReplyFilterStatus::kServiceError) {
                        ALOGW("camera-status reply filter failed: status=%s",
                              cameraStatusReplyFilterStatusName(filterStatus));
                    }
                }
                return forwardStatus;
            }
            if (requestStatus !=
                    CameraListenerRequestRouteStatus::kNoRegisteredWrapper) {
                ALOGW("camera listener route failed: status=%s",
                      cameraListenerRequestRouteStatusName(requestStatus));
            }
        }
        if (physicalRoutingEnabled_ &&
            observation.status == ParcelObservationStatus::kObserved &&
            observation.transaction.cameraScoped &&
            ::vcam::ScopedCameraRouter::isInternalCameraId(
                    observation.cameraId)) {
            const android::status_t writeStatus =
                    rejectCameraRequest(reply, "Unknown camera ID");
            if (writeStatus == android::OK) {
                gInternalCameraRequestsRejected.fetch_add(
                        1, std::memory_order_relaxed);
            }
            return writeStatus;
        }
        if (!physicalRoutingEnabled_) {
            // Pass-through observation ends here. In particular, a generic
            // probe candidate never resolves caller packages or route files.
            return target_->transact(code, data, reply, flags);
        }
        const bool routesConfigured = routingPolicyActive();
        if (routesConfigured &&
            observation.status == ParcelObservationStatus::kObserved &&
            observation.transaction.cameraScoped && !routingMapsReady()) {
            gUnavailableRouteProviders.fetch_add(1, std::memory_order_relaxed);
            ALOGE("camera routing maps are not ready");
            return rejectCameraRequest(
                    reply, "Camera routing topology unavailable");
        }
        const CameraCallerIdentityClassification identity =
                classifyCameraCallerIdentity(
                        observation.transaction.cameraScoped,
                        observation.transaction.carriesPackageName,
                        observation.callingUid,
                        observation.callingPid,
                        observation.packageName);
        std::string verifiedPackage;
        std::string replacementCameraId;
        if (observation.status == ParcelObservationStatus::kObserved &&
            identity.kind == CameraCallerIdentityKind::kClaimedPackage) {
            android::Vector<android::String16> packages;
            permissionController_.getPackagesForUid(
                    static_cast<uid_t>(observation.callingUid), packages);
            if (packages.isEmpty()) {
                gUnavailablePackageLookups.fetch_add(
                        1, std::memory_order_relaxed);
            } else {
                const android::String16 claimedPackage(
                        observation.packageName.c_str());
                bool verified = false;
                for (const android::String16& package : packages) {
                    if (package == claimedPackage) {
                        verified = true;
                        break;
                    }
                }
                (verified ? gVerifiedPackageClaims : gRejectedPackageClaims)
                        .fetch_add(1, std::memory_order_relaxed);
                if (verified) {
                    verifiedPackage = observation.packageName;
                }
            }
        } else if (routesConfigured &&
                   observation.status == ParcelObservationStatus::kObserved &&
                   identity.kind == CameraCallerIdentityKind::kUidOnly) {
            android::Vector<android::String16> packages;
            permissionController_.getPackagesForUid(
                    static_cast<uid_t>(observation.callingUid), packages);
            if (packages.isEmpty()) {
                gUnavailableUidPackageResolutions.fetch_add(
                        1, std::memory_order_relaxed);
            } else if (packages.size() == 1) {
                verifiedPackage = android::String8(packages[0]).c_str();
                gUniqueUidPackageResolutions.fetch_add(
                        1, std::memory_order_relaxed);
            } else {
                gAmbiguousUidPackageResolutions.fetch_add(
                        1, std::memory_order_relaxed);
            }
        }
        if (routesConfigured &&
            observation.status == ParcelObservationStatus::kObserved &&
            observation.transaction.cameraScoped &&
            !observation.cameraId.empty()) {
            const bool integerCameraId =
                    observation.transaction.payloadShape ==
                            BinderPayloadShape::kConnectApi1 ||
                    observation.transaction.payloadShape ==
                            BinderPayloadShape::kIntegerCameraId;
            const ::vcam::ScopedCameraRoute route =
                    ::vcam::ScopedCameraRouter::resolve(
                    verifiedPackage,
                    observation.cameraId,
                    kRuntimeRoutesPath,
                    kRuntimeProvidersPath,
                    integerCameraId
                            ? ::vcam::ScopedCameraRouter::
                                      kDefaultCamera1TargetMapPath
                            : ::vcam::ScopedCameraRouter::kDefaultTargetMapPath);
            if (route.configured && !route.available) {
                gUnavailableRouteProviders.fetch_add(
                        1, std::memory_order_relaxed);
            } else if (route.redirected) {
                std::atomic<std::uint64_t>& counter =
                        route.match == ::vcam::RouteMatchKind::Package
                        ? gPackageRouteCandidates
                        : gGlobalRouteCandidates;
                counter.fetch_add(1, std::memory_order_relaxed);
                replacementCameraId = route.effectiveCameraId;
            } else {
                gPhysicalRouteDecisions.fetch_add(
                        1, std::memory_order_relaxed);
                const int physicalId =
                        ::vcam::RouteResolver::physicalIdFromProvider(
                                route.providerId);
                if (route.configured && physicalId >= 0 &&
                    std::to_string(physicalId) != observation.cameraId) {
                    replacementCameraId = std::to_string(physicalId);
                }
            }
        }
        if (physicalRoutingEnabled_ && !replacementCameraId.empty()) {
            gPhysicalRewriteAttempts.fetch_add(1, std::memory_order_relaxed);
            if (observation.transaction.payloadShape ==
                            BinderPayloadShape::kConnectApi1 ||
                observation.transaction.payloadShape ==
                            BinderPayloadShape::kIntegerCameraId) {
                const std::string camera1Index =
                        ::vcam::ScopedCameraRouter::camera1IndexForId(
                                replacementCameraId);
                if (camera1Index.empty()) {
                    gPhysicalRewriteFailures.fetch_add(
                            1, std::memory_order_relaxed);
                    ALOGE("Camera1 route has no index mapping: cameraId=%s",
                          replacementCameraId.c_str());
                    return rejectCameraRequest(
                            reply, "Camera1 routing map unavailable");
                }
                replacementCameraId = camera1Index;
            }
            android::Parcel rewritten;
            const CameraIdRewriteStatus rewriteStatus =
                    rewriteAndroid14CameraId(
                            observation,
                            replacementCameraId,
                            data,
                            &rewritten);
            if (rewriteStatus == CameraIdRewriteStatus::kRewritten) {
                gPhysicalRewriteSuccesses.fetch_add(1, std::memory_order_relaxed);
                const android::status_t forwardStatus =
                        target_->transact(code, rewritten, reply, flags);
                if (forwardStatus == android::OK &&
                    observation.transaction.payloadShape ==
                            BinderPayloadShape::kConnectDevice) {
                    const CameraDeviceUserReplyRouteStatus replyStatus =
                            wrapAndroid14CameraDeviceUserReply(
                                    reply,
                                    observation.cameraId,
                                    replacementCameraId);
                    if (replyStatus !=
                                CameraDeviceUserReplyRouteStatus::kWrapped &&
                        replyStatus !=
                                CameraDeviceUserReplyRouteStatus::kServiceError) {
                        ALOGW("device-user reply route failed: status=%s",
                              cameraDeviceUserReplyRouteStatusName(replyStatus));
                    }
                }
                return forwardStatus;
            }
            gPhysicalRewriteFailures.fetch_add(1, std::memory_order_relaxed);
        }
        // target_ is a local BBinder in this process. Calling transact() here
        // reaches its onTransact() directly without a nested Binder driver call,
        // so IPCThreadState retains the original app PID/UID/SID.
        return target_->transact(code, data, reply, flags);
    }

private:
    const android::sp<android::IBinder> target_;
    Android14BinderShadowObserver observer_;
    android::PermissionController permissionController_;
    const bool physicalRoutingEnabled_;
};

void setTerminalState(AndroidCameraServiceRouterState state, const char* detail) {
    gState.store(state, std::memory_order_release);
    ALOGI("router state=%s detail=%s",
          androidCameraServiceRouterStateName(state), detail);
    if (state == AndroidCameraServiceRouterState::kPreflightReady ||
        state == AndroidCameraServiceRouterState::kPassThroughReady ||
        state == AndroidCameraServiceRouterState::kPhysicalRouteReady) {
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

    const std::string fingerprint = buildFingerprint();
    Android14CameraServiceProtocolSelection protocol =
            selectAndroid14CameraServiceProtocol(buildSdk(), fingerprint);
    if (mode == CameraServiceRouterMode::kPhysicalRoute &&
        !protocol.routingAllowed) {
        setTerminalState(AndroidCameraServiceRouterState::kProtocolUnqualified,
                         "camera Binder protocol is not qualified for routing");
        return nullptr;
    }
    if (protocol.observationAllowed) {
        gObserverProfile.store(protocol.profileName, std::memory_order_release);
        ALOGI("enabled read-only Binder observer profile=%s confidence=%s",
              protocol.profileName,
              android14CameraServiceProtocolConfidenceName(protocol.confidence));
    } else {
        ALOGW("no read-only Binder observation template for this platform; "
              "transactions remain unparsed");
    }
    android::sp<android::IBinder> router =
            android::sp<CameraServicePassThrough>::make(
                    original,
                    std::move(protocol.recipe),
                    mode == CameraServiceRouterMode::kPhysicalRoute);
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
    if (mode == CameraServiceRouterMode::kPhysicalRoute) {
        setTerminalState(
                AndroidCameraServiceRouterState::kPhysicalRouteReady,
                "media.camera allows qualified segmented physical ID routing");
    } else {
        setTerminalState(AndroidCameraServiceRouterState::kPassThroughReady,
                         "media.camera now forwards to the original local Binder");
    }
    startStatsPublisher();
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
        case AndroidCameraServiceRouterState::kPhysicalRouteReady:
            return "physical_route_ready";
        case AndroidCameraServiceRouterState::kProtocolUnqualified:
            return "protocol_unqualified";
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

extern "C" __attribute__((visibility("default"))) const char*
vcam_camera_service_router_protocol_verdict() {
    return vcam::runtime::android14ProtocolEvidenceVerdictName(
            observerStats().protocolEvidence.verdict);
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_protocol_required_mask() {
    return observerStats().protocolEvidence.requiredMask;
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_protocol_seen_mask() {
    return observerStats().protocolEvidence.seenMask;
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_protocol_valid_mask() {
    return observerStats().protocolEvidence.validMask;
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_protocol_invalid_mask() {
    return observerStats().protocolEvidence.invalidMask;
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_protocol_unsupported_mask() {
    return observerStats().protocolEvidence.unsupportedMask;
}

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

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_verified_package_claims() {
    return vcam::runtime::gVerifiedPackageClaims.load(std::memory_order_relaxed);
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_rejected_package_claims() {
    return vcam::runtime::gRejectedPackageClaims.load(std::memory_order_relaxed);
}

extern "C" __attribute__((visibility("default"))) std::uint64_t
vcam_camera_service_router_unavailable_package_lookups() {
    return vcam::runtime::gUnavailablePackageLookups.load(
            std::memory_order_relaxed);
}
