#define LOG_TAG "VcamCameraLauncher"

#include "vcam/CameraServerBootstrapMode.h"
#include "vcam/CameraServerBootstrapPaths.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <log/log.h>
#include <android/binder_process.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr std::size_t kMaximumModeBytes = 32;
constexpr char kPendingPayload[] = "pending\n";

enum class AttemptArmStatus {
    kArmed,
    kAlreadyPending,
    kError,
};

bool isTrimCharacter(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

vcam::runtime::CameraServerBootstrapMode readBootstrapMode() {
    const int fd = open(vcam::runtime::bootstrap::kModePath,
                        O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return vcam::runtime::CameraServerBootstrapMode::kStock;
    }

    struct stat metadata {};
    if (fstat(fd, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        metadata.st_size < 0 ||
        metadata.st_size > static_cast<off_t>(kMaximumModeBytes)) {
        close(fd);
        return vcam::runtime::CameraServerBootstrapMode::kInvalid;
    }

    char buffer[kMaximumModeBytes + 1] {};
    const ssize_t count = read(fd, buffer, kMaximumModeBytes);
    const int readError = errno;
    close(fd);
    if (count < 0) {
        errno = readError;
        return vcam::runtime::CameraServerBootstrapMode::kInvalid;
    }

    std::size_t begin = 0;
    std::size_t end = static_cast<std::size_t>(count);
    while (begin < end && isTrimCharacter(buffer[begin])) {
        ++begin;
    }
    while (end > begin && isTrimCharacter(buffer[end - 1])) {
        --end;
    }
    buffer[end] = '\0';
    return vcam::runtime::parseCameraServerBootstrapMode(buffer + begin);
}

bool isCameraServerDomain() {
    const int fd = open("/proc/self/attr/current", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    char buffer[128] {};
    const ssize_t count = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (count <= 0) {
        return false;
    }
    buffer[count] = '\0';
    return std::strncmp(buffer, vcam::runtime::bootstrap::kExpectedDomainPrefix,
                        std::strlen(vcam::runtime::bootstrap::kExpectedDomainPrefix)) == 0;
}

AttemptArmStatus armBootstrapAttempt() {
    const int fd = open(vcam::runtime::bootstrap::kPendingPath,
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                        0600);
    if (fd < 0) {
        return errno == EEXIST
                ? AttemptArmStatus::kAlreadyPending
                : AttemptArmStatus::kError;
    }

    const ssize_t written = write(fd, kPendingPayload, sizeof(kPendingPayload) - 1);
    const bool durable = written == static_cast<ssize_t>(sizeof(kPendingPayload) - 1) &&
                         fsync(fd) == 0;
    const int markerError = errno;
    close(fd);
    if (!durable) {
        unlink(vcam::runtime::bootstrap::kPendingPath);
        errno = markerError;
        return AttemptArmStatus::kError;
    }
    return AttemptArmStatus::kArmed;
}

[[noreturn]] void runCameraServer(bool routerLoaded) {
    if (!routerLoaded) {
        unsetenv("VCAM_BINDER_ROUTER_MODE");
    }

    // AOSP forbids cameraserver from executing another file without a domain
    // transition. Recreate the stable main_cameraserver entry sequence in this
    // already transitioned process instead of execing a relocated binary.
    void* const cameraService = dlopen("libcameraservice.so", RTLD_NOW | RTLD_GLOBAL);
    if (cameraService == nullptr) {
        ALOGE("could not load libcameraservice: %s", dlerror());
        _exit(125);
    }
    constexpr char kInstantiateSymbol[] =
            "_ZN7android13CameraService11instantiateEv";
    dlerror();
    void* const instantiateAddress = dlsym(cameraService, kInstantiateSymbol);
    const char* const symbolError = dlerror();
    if (symbolError != nullptr || instantiateAddress == nullptr) {
        ALOGE("CameraService instantiate interface is unavailable: %s",
              symbolError == nullptr ? "symbol resolved to null" : symbolError);
        _exit(125);
    }

    using InstantiateCameraService = void (*)();
    const auto instantiate =
            reinterpret_cast<InstantiateCameraService>(instantiateAddress);

    signal(SIGPIPE, SIG_IGN);
    android::hardware::configureRpcThreadpool(5, false);
    ABinderProcess_setThreadPoolMaxThreadCount(5);

    const android::sp<android::ProcessState> process =
            android::ProcessState::self();
    const android::sp<android::IServiceManager> serviceManager =
            android::defaultServiceManager();
    ALOGI("ServiceManager: %p", serviceManager.get());
    instantiate();
    ALOGI("ServiceManager: %p done instantiate", serviceManager.get());
    process->startThreadPool();
    ABinderProcess_startThreadPool();
    android::IPCThreadState::self()->joinThreadPool();
    ABinderProcess_joinThreadPool();
    _exit(0);
}

}  // namespace

int main() {
    // A stats file belongs to one cameraserver lifetime only. In stock mode no
    // router will recreate it, so readers cannot mistake old telemetry for a
    // currently active proxy.
    if (unlink(vcam::runtime::bootstrap::kRouterStatsPath) != 0 &&
        errno != ENOENT) {
        ALOGW("could not remove stale router stats: errno=%d", errno);
    }

    const vcam::runtime::CameraServerBootstrapMode mode = readBootstrapMode();
    if (mode == vcam::runtime::CameraServerBootstrapMode::kStock ||
        mode == vcam::runtime::CameraServerBootstrapMode::kInvalid) {
        if (mode == vcam::runtime::CameraServerBootstrapMode::kInvalid) {
            ALOGE("invalid bootstrap mode; starting unrouted CameraService");
        }
        runCameraServer(false);
    }

    if (!isCameraServerDomain()) {
        ALOGE("launcher did not enter cameraserver SELinux domain; skipping router");
        runCameraServer(false);
    }
    if (access(vcam::runtime::bootstrap::kRouterLibraryPath, R_OK) != 0) {
        ALOGE("router library is unavailable; starting unrouted CameraService");
        runCameraServer(false);
    }

    const AttemptArmStatus attempt = armBootstrapAttempt();
    if (attempt != AttemptArmStatus::kArmed) {
        ALOGE("bootstrap attempt not armed (%s); starting unrouted CameraService",
              attempt == AttemptArmStatus::kAlreadyPending
                      ? "previous attempt pending"
                      : "marker creation failed");
        runCameraServer(false);
    }

    const char* routerMode =
            mode == vcam::runtime::CameraServerBootstrapMode::kPreflight
            ? "preflight"
            : mode == vcam::runtime::CameraServerBootstrapMode::kPhysicalRoute
            ? "physical-route"
            : "passthrough";
    if (setenv("VCAM_BINDER_ROUTER_MODE", routerMode, 1) != 0) {
        ALOGE("could not prepare router environment; starting unrouted "
              "CameraService");
        runCameraServer(false);
    }

    void* const router = dlopen(vcam::runtime::bootstrap::kRouterLibraryPath,
                                RTLD_NOW | RTLD_GLOBAL);
    if (router == nullptr) {
        ALOGE("could not load router library: %s; starting unrouted CameraService",
              dlerror());
        runCameraServer(false);
    }

    ALOGI("starting in-process cameraserver with router mode=%s",
          vcam::runtime::cameraServerBootstrapModeName(mode));
    runCameraServer(true);
}
