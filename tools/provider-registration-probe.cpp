// Minimal Android service-manager registration diagnostic.
// This intentionally has no camera dependencies: it isolates Binder domain,
// stability, and SELinux registration failures from camera-provider startup.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <unistd.h>

// binder_manager.h and binder_process.h are platform-only as of NDK r27, but
// libbinder_ndk keeps these stable C symbols. Keep the minimal ABI declarations
// here so this diagnostic can be cross-compiled without a full AOSP tree.
extern "C" {
struct AIBinder;
struct AIBinder_Class;
struct AParcel;
using binder_status_t = int32_t;
using binder_exception_t = int32_t;
using transaction_code_t = uint32_t;
using AIBinder_Class_onCreate = void* (*)(void*);
using AIBinder_Class_onDestroy = void (*)(void*);
using AIBinder_Class_onTransact = binder_status_t (*)(
    AIBinder*, transaction_code_t, const AParcel*, AParcel*);

AIBinder_Class* AIBinder_Class_define(const char*, AIBinder_Class_onCreate,
                                      AIBinder_Class_onDestroy,
                                      AIBinder_Class_onTransact);
AIBinder* AIBinder_new(const AIBinder_Class*, void*);
void AIBinder_decStrong(AIBinder*);
}

constexpr binder_status_t STATUS_UNKNOWN_TRANSACTION = -74;
constexpr binder_exception_t EX_NONE = 0;

namespace {

void* OnCreate(void*) {
  return nullptr;
}

void OnDestroy(void*) {}

binder_status_t OnTransact(AIBinder*, transaction_code_t, const AParcel*,
                           AParcel*) {
  return STATUS_UNKNOWN_TRANSACTION;
}

}  // namespace

int main(int argc, char** argv) {
  const char* instance = argc > 1 ? argv[1] : "android.vcam.IProbe/default";
  using AddService = binder_exception_t (*)(AIBinder*, const char*);
  using ChangeStability = void (*)(AIBinder*);
  using SetThreadPoolMax = void (*)(uint32_t);
  using StartThreadPool = void (*)();
  auto add_service = reinterpret_cast<AddService>(
      dlsym(RTLD_DEFAULT, "AServiceManager_addService"));
  auto set_thread_pool_max = reinterpret_cast<SetThreadPoolMax>(
      dlsym(RTLD_DEFAULT, "ABinderProcess_setThreadPoolMaxThreadCount"));
  auto start_thread_pool = reinterpret_cast<StartThreadPool>(
      dlsym(RTLD_DEFAULT, "ABinderProcess_startThreadPool"));
  if (add_service == nullptr || set_thread_pool_max == nullptr ||
      start_thread_pool == nullptr) {
    std::fprintf(stderr, "Required platform Binder symbols are unavailable: %s\n",
                 dlerror());
    return 1;
  }

  AIBinder_Class* clazz =
      AIBinder_Class_define("android.vcam.IProbe", OnCreate, OnDestroy,
                            OnTransact);
  if (clazz == nullptr) {
    std::fprintf(stderr, "AIBinder_Class_define failed\n");
    return 2;
  }

  AIBinder* binder = AIBinder_new(clazz, nullptr);
  if (binder == nullptr) {
    std::fprintf(stderr, "AIBinder_new failed\n");
    return 3;
  }

  const char* test_stability = std::getenv("ANDROID_VCAM_PROBE_SYSTEM_STABILITY");
  if (test_stability != nullptr && test_stability[0] == '1' &&
      test_stability[1] == '\0') {
    auto mark_vintf = reinterpret_cast<ChangeStability>(
        dlsym(RTLD_DEFAULT, "AIBinder_markVintfStability"));
    auto force_system = reinterpret_cast<ChangeStability>(
        dlsym(RTLD_DEFAULT, "AIBinder_forceDowngradeToSystemStability"));
    if (mark_vintf == nullptr || force_system == nullptr) {
      std::fprintf(stderr, "Stability test symbols are unavailable: %s\n",
                   dlerror());
      AIBinder_decStrong(binder);
      return 5;
    }
    mark_vintf(binder);
    force_system(binder);
    std::fprintf(stderr, "Applied VINTF-to-system stability transition\n");
  }

  set_thread_pool_max(1);
  start_thread_pool();
  const binder_exception_t result =
      add_service(binder, instance);
  std::fprintf(stderr, "AServiceManager_addService(%s)=%d\n", instance,
               result);
  if (result != EX_NONE) {
    AIBinder_decStrong(binder);
    return 4;
  }

  sleep(10);
  AIBinder_decStrong(binder);
  return 0;
}
