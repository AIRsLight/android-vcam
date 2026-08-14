#include <stdio.h>

#include <hardware/gralloc.h>
#include <hardware/gralloc1.h>
#include <hardware/hardware.h>

int main(void) {
    const hw_module_t* module = NULL;
    int result = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
    if (result != 0 || module == NULL) {
        printf("hw_get_module failed: %d\n", result);
        return 1;
    }
    const gralloc_module_t* gralloc = (const gralloc_module_t*)module;
    printf("name=%s author=%s api=0x%x\n",
           module->name ? module->name : "(null)",
           module->author ? module->author : "(null)",
           module->module_api_version);
    printf("register=%d unregister=%d lock=%d lock_ycbcr=%d unlock=%d\n",
           gralloc->registerBuffer != NULL,
           gralloc->unregisterBuffer != NULL,
           gralloc->lock != NULL,
           gralloc->lock_ycbcr != NULL,
           gralloc->unlock != NULL);

    gralloc1_device_t* device = NULL;
    result = gralloc1_open(module, &device);
    if (result != 0 || device == NULL) {
        printf("gralloc1_open failed: %d\n", result);
        return 2;
    }
    printf("gralloc1 getFunction=%d lock=%d lockFlex=%d unlock=%d\n",
           device->getFunction != NULL,
           device->getFunction != NULL && device->getFunction(
                   device, GRALLOC1_FUNCTION_LOCK) != NULL,
           device->getFunction != NULL && device->getFunction(
                   device, GRALLOC1_FUNCTION_LOCK_FLEX) != NULL,
           device->getFunction != NULL && device->getFunction(
                   device, GRALLOC1_FUNCTION_UNLOCK) != NULL);
    gralloc1_close(device);
    return 0;
}
