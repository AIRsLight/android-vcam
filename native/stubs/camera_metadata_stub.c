#include <stddef.h>
#include <stdint.h>
#include <system/camera_metadata.h>

camera_metadata_t* allocate_camera_metadata(size_t entry_capacity,
                                            size_t data_capacity) {
    (void)entry_capacity;
    (void)data_capacity;
    return 0;
}

void free_camera_metadata(camera_metadata_t* metadata) {
    (void)metadata;
}

camera_metadata_t* clone_camera_metadata(const camera_metadata_t* src) {
    (void)src;
    return 0;
}

int add_camera_metadata_entry(camera_metadata_t* dst, uint32_t tag,
                              const void* data, size_t data_count) {
    (void)dst;
    (void)tag;
    (void)data;
    (void)data_count;
    return -1;
}

int sort_camera_metadata(camera_metadata_t* dst) {
    (void)dst;
    return -1;
}

int find_camera_metadata_ro_entry(const camera_metadata_t* src, uint32_t tag,
                                  camera_metadata_ro_entry_t* entry) {
    (void)src;
    (void)tag;
    (void)entry;
    return -1;
}

int find_camera_metadata_entry(camera_metadata_t* src, uint32_t tag,
                               camera_metadata_entry_t* entry) {
    (void)src;
    (void)tag;
    (void)entry;
    return -1;
}

size_t get_camera_metadata_entry_count(const camera_metadata_t* metadata) {
    (void)metadata;
    return 0;
}

size_t get_camera_metadata_data_count(const camera_metadata_t* metadata) {
    (void)metadata;
    return 0;
}

int append_camera_metadata(camera_metadata_t* dst,
                           const camera_metadata_t* src) {
    (void)dst;
    (void)src;
    return -1;
}

int update_camera_metadata_entry(camera_metadata_t* dst, size_t index,
                                 const void* data, size_t data_count,
                                 camera_metadata_entry_t* updated_entry) {
    (void)dst;
    (void)index;
    (void)data;
    (void)data_count;
    (void)updated_entry;
    return -1;
}

int get_camera_metadata_ro_entry(const camera_metadata_t* src, size_t index,
                                 camera_metadata_ro_entry_t* entry) {
    (void)src;
    (void)index;
    (void)entry;
    return -1;
}
