/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 *
 * Minimal LD_PRELOAD diagnostics for a manually launched camera provider.
 * This is intentionally not linked into production artifacts. It mirrors
 * Android log writes to stderr and reports process-local dlopen attempts so a
 * probe can be diagnosed even when its temporary SELinux domain cannot write
 * to logd.
 */

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char kDefaultTracePath[] =
    "/data/vendor/android_vcam_probe/trace.log";

static void trace_write(const char* message, size_t length) {
  (void)write(STDERR_FILENO, message, length);
  const char* configured_path = getenv("ANDROID_VCAM_PROBE_TRACE_PATH");
  const char* trace_path =
      configured_path != NULL && configured_path[0] == '/'
          ? configured_path
          : kDefaultTracePath;
  const int fd =
      open(trace_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (fd < 0) return;
  (void)write(fd, message, length);
  (void)close(fd);
}

static int emit_log(int priority, const char* tag, const char* format,
                    va_list args) {
  char message[2048];
  const int prefix = snprintf(message, sizeof(message),
                              "[android-log p=%d tag=%s] ", priority,
                              tag != NULL ? tag : "");
  if (prefix < 0) return prefix;

  const size_t offset = (size_t)prefix < sizeof(message)
                            ? (size_t)prefix
                            : sizeof(message) - 1;
  const int body = vsnprintf(message + offset, sizeof(message) - offset,
                             format != NULL ? format : "", args);
  size_t length = strnlen(message, sizeof(message));
  if (length == 0 || message[length - 1] != '\n') {
    if (length + 1 < sizeof(message)) {
      message[length++] = '\n';
      message[length] = '\0';
    }
  }
  trace_write(message, length);
  return body;
}

int __android_log_vprint(int priority, const char* tag, const char* format,
                         va_list args) {
  return emit_log(priority, tag, format, args);
}

int __android_log_print(int priority, const char* tag, const char* format,
                        ...) {
  va_list args;
  va_start(args, format);
  const int result = emit_log(priority, tag, format, args);
  va_end(args);
  return result;
}

int __android_log_buf_print(int buffer_id, int priority, const char* tag,
                            const char* format, ...) {
  (void)buffer_id;
  va_list args;
  va_start(args, format);
  const int result = emit_log(priority, tag, format, args);
  va_end(args);
  return result;
}

int __android_log_write(int priority, const char* tag, const char* text) {
  char message[2048];
  const int length = snprintf(message, sizeof(message),
                              "[android-log p=%d tag=%s] %s\n", priority,
                              tag != NULL ? tag : "", text != NULL ? text : "");
  if (length > 0) {
    const size_t write_length = (size_t)length < sizeof(message)
                                    ? (size_t)length
                                    : sizeof(message) - 1;
    trace_write(message, write_length);
  }
  return length;
}

int __android_log_buf_write(int buffer_id, int priority, const char* tag,
                            const char* text) {
  (void)buffer_id;
  return __android_log_write(priority, tag, text);
}

void* dlopen(const char* filename, int flags) {
  typedef void* (*DlopenFunction)(const char*, int);
  static DlopenFunction next_dlopen;
  if (next_dlopen == NULL) {
    next_dlopen = (DlopenFunction)dlsym(RTLD_NEXT, "dlopen");
  }
  if (next_dlopen == NULL) return NULL;

  char before[1024];
  const int before_length = snprintf(before, sizeof(before),
                                     "[provider-probe] dlopen(%s, 0x%x)\n",
                                     filename != NULL ? filename : "<null>",
                                     flags);
  if (before_length > 0) {
    trace_write(before, (size_t)before_length < sizeof(before)
                            ? (size_t)before_length
                            : sizeof(before) - 1);
  }

  void* handle = next_dlopen(filename, flags);
  char after[128];
  const int after_length = snprintf(after, sizeof(after),
                                    "[provider-probe] dlopen result=%p\n",
                                    handle);
  if (after_length > 0) {
    trace_write(after, (size_t)after_length < sizeof(after)
                           ? (size_t)after_length
                           : sizeof(after) - 1);
  }
  return handle;
}

int32_t AServiceManager_addService(void* service, const char* instance) {
  typedef int32_t (*AddServiceFunction)(void*, const char*);
  static AddServiceFunction next_add_service;
  if (next_add_service == NULL) {
    next_add_service =
        (AddServiceFunction)dlsym(RTLD_NEXT, "AServiceManager_addService");
  }
  if (next_add_service == NULL) return -1;

  const int32_t result = next_add_service(service, instance);
  char message[512];
  const int length = snprintf(
      message, sizeof(message),
      "[provider-probe] AServiceManager_addService(%s)=%d\n",
      instance != NULL ? instance : "<null>", result);
  if (length > 0) {
    trace_write(message, (size_t)length < sizeof(message)
                             ? (size_t)length
                             : sizeof(message) - 1);
  }
  return result;
}
