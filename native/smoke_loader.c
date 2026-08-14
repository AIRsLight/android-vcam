#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <hardware/camera_common.h>
#include <hardware/hardware.h>
#include <system/camera_metadata.h>

int main(int argc, char** argv) {
    const uint32_t vcam_client_package_tag = 0x80000000u;
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

    const int camera_count = module->get_number_of_cameras();
    printf("module=%s api=0x%04x cameras=%d\n", module->common.name,
           module->common.module_api_version, camera_count);
    if (camera_count != 2 || module->get_camera_info == NULL) {
        fprintf(stderr, "unexpected camera module shape\n");
        dlclose(handle);
        return 5;
    }
    vendor_tag_ops_t vendor_ops = {0};
    uint32_t vendor_tag = 0;
    if (module->get_vendor_tag_ops == NULL) {
        fprintf(stderr, "vendor tag operations unavailable\n");
        dlclose(handle);
        return 6;
    }
    module->get_vendor_tag_ops(&vendor_ops);
    if (vendor_ops.get_tag_count == NULL ||
            vendor_ops.get_all_tags == NULL ||
            vendor_ops.get_section_name == NULL ||
            vendor_ops.get_tag_name == NULL ||
            vendor_ops.get_tag_type == NULL ||
            vendor_ops.get_tag_count(&vendor_ops) != 1) {
        fprintf(stderr, "invalid vendor tag operations\n");
        dlclose(handle);
        return 7;
    }
    vendor_ops.get_all_tags(&vendor_ops, &vendor_tag);
    const char* vendor_section = vendor_ops.get_section_name(&vendor_ops, vendor_tag);
    const char* vendor_name = vendor_ops.get_tag_name(&vendor_ops, vendor_tag);
    if (vendor_tag != vcam_client_package_tag ||
            vendor_section == NULL || vendor_name == NULL ||
            strcmp(vendor_section, "io.github.androidvcam") != 0 ||
            strcmp(vendor_name, "clientPackage") != 0 ||
            vendor_ops.get_tag_type(&vendor_ops, vendor_tag) != TYPE_BYTE) {
        fprintf(stderr, "unexpected VCAM client-package vendor tag\n");
        dlclose(handle);
        return 8;
    }
    for (int id = 0; id < camera_count; ++id) {
        struct camera_info info = {0};
        if (module->get_camera_info(id, &info) != 0 ||
                info.device_version != CAMERA_DEVICE_API_VERSION_3_5 ||
                info.static_camera_characteristics == NULL) {
            fprintf(stderr, "invalid camera info for id %d\n", id);
            dlclose(handle);
            return 9;
        }
        camera_metadata_ro_entry_t partial_count = {0};
        if (find_camera_metadata_ro_entry(info.static_camera_characteristics,
                ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &partial_count) != 0 ||
                partial_count.count != 1 || partial_count.data.i32[0] != 1) {
            fprintf(stderr, "invalid partial result count for id %d\n", id);
            dlclose(handle);
            return 10;
        }
        camera_metadata_ro_entry_t session_keys = {0};
        if (find_camera_metadata_ro_entry(info.static_camera_characteristics,
                ANDROID_REQUEST_AVAILABLE_SESSION_KEYS, &session_keys) != 0 ||
                session_keys.count != 1 ||
                (uint32_t)session_keys.data.i32[0] != vcam_client_package_tag) {
            fprintf(stderr, "invalid client-package session key for id %d\n", id);
            dlclose(handle);
            return 11;
        }
        printf("camera=%d facing=%d orientation=%d device=0x%04x\n",
               id, info.facing, info.orientation, info.device_version);
    }
    dlclose(handle);
    return 0;
}
