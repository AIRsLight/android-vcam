#pragma once

#include <android/log.h>

#ifndef LOG_TAG
#define LOG_TAG "android-vcam"
#endif

#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
