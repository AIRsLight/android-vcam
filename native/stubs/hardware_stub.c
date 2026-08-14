#include <errno.h>
#include <hardware/hardware.h>

int hw_get_module(const char* id, const struct hw_module_t** module) {
    (void)id;
    if (module != 0) *module = 0;
    return -ENOENT;
}

int hw_get_module_by_class(const char* class_id, const char* inst,
                           const struct hw_module_t** module) {
    (void)class_id;
    (void)inst;
    if (module != 0) *module = 0;
    return -ENOENT;
}
