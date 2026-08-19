#define LOG_TAG "VcamCameraLauncher"

#include "vcam/CameraServerBootstrapMode.h"
#include "vcam/CameraServerBootstrapPaths.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <log/log.h>
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

bool stockExecutableIsSafe() {
    struct stat selfMetadata {};
    struct stat stockMetadata {};
    if (stat("/proc/self/exe", &selfMetadata) != 0 ||
        stat(vcam::runtime::bootstrap::kStockCameraServerPath, &stockMetadata) != 0 ||
        access(vcam::runtime::bootstrap::kStockCameraServerPath, X_OK) != 0) {
        return false;
    }
    return selfMetadata.st_dev != stockMetadata.st_dev ||
           selfMetadata.st_ino != stockMetadata.st_ino;
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

[[noreturn]] void runStockCameraServer(char* const argv[]) {
    unsetenv("LD_PRELOAD");
    unsetenv("VCAM_BINDER_ROUTER_MODE");
    execv(vcam::runtime::bootstrap::kStockCameraServerPath, argv);
    const int launchError = errno;
    ALOGE("stock cameraserver exec failed: path=%s errno=%d (%s)",
          vcam::runtime::bootstrap::kStockCameraServerPath,
          launchError, std::strerror(launchError));
    _exit(127);
}

}  // namespace

int main(int, char* argv[]) {
    if (!stockExecutableIsSafe()) {
        ALOGE("stock cameraserver is missing, not executable, or aliases launcher");
        return 126;
    }

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
            ALOGE("invalid bootstrap mode; starting stock cameraserver");
        }
        runStockCameraServer(argv);
    }

    if (!isCameraServerDomain()) {
        ALOGE("launcher did not enter cameraserver SELinux domain; skipping preload");
        runStockCameraServer(argv);
    }
    if (access(vcam::runtime::bootstrap::kRouterLibraryPath, R_OK) != 0) {
        ALOGE("router library is unavailable; starting stock cameraserver");
        runStockCameraServer(argv);
    }

    const AttemptArmStatus attempt = armBootstrapAttempt();
    if (attempt != AttemptArmStatus::kArmed) {
        ALOGE("bootstrap attempt not armed (%s); starting stock cameraserver",
              attempt == AttemptArmStatus::kAlreadyPending
                      ? "previous attempt pending"
                      : "marker creation failed");
        runStockCameraServer(argv);
    }

    const char* routerMode =
            mode == vcam::runtime::CameraServerBootstrapMode::kPreflight
            ? "preflight"
            : mode == vcam::runtime::CameraServerBootstrapMode::kPhysicalRoute
            ? "physical-route"
            : "passthrough";
    if (setenv("VCAM_BINDER_ROUTER_MODE", routerMode, 1) != 0 ||
        setenv("LD_PRELOAD", vcam::runtime::bootstrap::kRouterLibraryPath, 1) != 0) {
        ALOGE("could not prepare preload environment; starting stock cameraserver");
        runStockCameraServer(argv);
    }

    ALOGI("starting cameraserver with router mode=%s",
          vcam::runtime::cameraServerBootstrapModeName(mode));
    execv(vcam::runtime::bootstrap::kStockCameraServerPath, argv);

    const int preloadError = errno;
    ALOGE("preloaded cameraserver exec failed: errno=%d (%s); retrying stock",
          preloadError, std::strerror(preloadError));
    runStockCameraServer(argv);
}
