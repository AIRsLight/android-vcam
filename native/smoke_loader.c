#include <dlfcn.h>
#include <stdio.h>

#include <hardware/camera_common.h>
#include <hardware/hardware.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/camera.vcam.so\n", argv[0]);
        return 2;
    }

    void* handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 3;
    }

    camera_module_t* module =
            (camera_module_t*)dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR);
    if (module == NULL) {
        fprintf(stderr, "dlsym(%s) failed: %s\n", HAL_MODULE_INFO_SYM_AS_STR,
                dlerror());
        dlclose(handle);
        return 4;
    }

    printf("module=%s api=0x%04x cameras=%d\n", module->common.name,
           module->common.module_api_version, module->get_number_of_cameras());
    dlclose(handle);
    return 0;
}
