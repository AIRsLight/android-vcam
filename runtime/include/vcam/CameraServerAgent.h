#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int vcam_cameraserver_agent_validate(
        const char* recipePath, char* message, size_t messageCapacity);

// Performs ABI validation, produces an internal pass-through patch plan and
// requires an exact precompiled trampoline. It never binds the trampoline,
// changes page permissions or writes the returned plan to memory.
int vcam_cameraserver_agent_plan(
        const char* recipePath, char* message, size_t messageCapacity);

// Repeats validation and planning, then inspects this process's maps, target
// bytes and thread inventory. It remains read-only and never commits a patch.
int vcam_cameraserver_agent_preflight(
        const char* recipePath, char* message, size_t messageCapacity);

#ifdef __cplusplus
}
#endif
