#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <jni.h>

#include <cstdio>

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_androidvcam_test_ProtocolProbeActivity_runNativeCameraManagerProbe(
        JNIEnv* env,
        jclass) {
    ACameraManager* manager = ACameraManager_create();
    if (manager == nullptr) {
        return env->NewStringUTF("NDK CameraManager create failed");
    }

    ACameraIdList* cameraIds = nullptr;
    const camera_status_t listStatus =
            ACameraManager_getCameraIdList(manager, &cameraIds);
    int visibleCount = 0;
    int characteristicsCount = 0;
    if (listStatus == ACAMERA_OK && cameraIds != nullptr) {
        visibleCount = cameraIds->numCameras;
        for (int index = 0; index < cameraIds->numCameras; ++index) {
            ACameraMetadata* metadata = nullptr;
            if (ACameraManager_getCameraCharacteristics(
                        manager, cameraIds->cameraIds[index], &metadata) ==
                    ACAMERA_OK &&
                metadata != nullptr) {
                ++characteristicsCount;
            }
            if (metadata != nullptr) {
                ACameraMetadata_free(metadata);
            }
        }
        ACameraManager_deleteCameraIdList(cameraIds);
    }

    // Releasing the last NDK manager reference destroys its process-global
    // listener and issues the matching CameraService removeListener call.
    ACameraManager_delete(manager);

    char result[160] {};
    std::snprintf(
            result,
            sizeof(result),
            "NDK status=%d cameras=%d characteristics=%d",
            static_cast<int>(listStatus),
            visibleCount,
            characteristicsCount);
    return env->NewStringUTF(result);
}
