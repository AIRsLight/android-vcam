/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define HEADER_SIZE 24u
#define MAX_DIMENSION 4096u
#define CAMERA_AID 1006

static const char kMagic[8] = {'V', 'C', 'A', 'M', 'R', 'G', 'B', '1'};
static const char* kDefaultDestinationPath = "/data/vendor/camera/vcam/source.rgb";
static const char* kProviderPrefix = "/data/vendor/camera/vcam/providers/";

static uint32_t read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int read_exact(int fd, void* destination, size_t size) {
    uint8_t* output = (uint8_t*)destination;
    size_t consumed = 0;
    while (consumed < size) {
        ssize_t count = read(fd, output + consumed, size - consumed);
        if (count == 0) return consumed == 0 ? 0 : -1;
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (count == 0) return -1;
        consumed += (size_t)count;
    }
    return 1;
}

static int write_exact(int fd, const void* source, size_t size) {
    const uint8_t* input = (const uint8_t*)source;
    size_t consumed = 0;
    while (consumed < size) {
        ssize_t count = write(fd, input + consumed, size - consumed);
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        consumed += (size_t)count;
    }
    return 0;
}

int main(int argc, char** argv) {
    uint8_t header[HEADER_SIZE];
    uint8_t buffer[256 * 1024];
    const char* destination_path = argc > 1 ? argv[1] : kDefaultDestinationPath;
    char temporary_path[512];
    if (strcmp(destination_path, kDefaultDestinationPath) != 0 &&
        strncmp(destination_path, kProviderPrefix, strlen(kProviderPrefix)) != 0) {
        fprintf(stderr, "vcam-publisher: output path is outside provider storage\n");
        return 64;
    }
    if (snprintf(temporary_path, sizeof(temporary_path), "%s.new",
                 destination_path) >= (int)sizeof(temporary_path)) {
        fprintf(stderr, "vcam-publisher: output path is too long\n");
        return 64;
    }

    for (;;) {
        int header_result = read_exact(STDIN_FILENO, header, sizeof(header));
        if (header_result == 0) return 0;
        if (header_result < 0 || memcmp(header, kMagic, sizeof(kMagic)) != 0) {
            fprintf(stderr, "vcam-publisher: invalid frame header\n");
            return 2;
        }

        const uint32_t width = read_le32(header + 8);
        const uint32_t height = read_le32(header + 12);
        const uint32_t payload_size = read_le32(header + 16);
        const uint64_t expected = (uint64_t)width * height * 3u;
        if (width == 0 || height == 0 || width > MAX_DIMENSION ||
            height > MAX_DIMENSION || expected != payload_size) {
            fprintf(stderr, "vcam-publisher: unsupported frame dimensions\n");
            return 3;
        }

        int output = open(temporary_path,
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                          0644);
        if (output < 0) {
            fprintf(stderr, "vcam-publisher: open failed: %s\n", strerror(errno));
            return 4;
        }
        if (fchmod(output, 0644) != 0 || fchown(output, CAMERA_AID, CAMERA_AID) != 0 ||
            write_exact(output, header, sizeof(header)) != 0) {
            fprintf(stderr, "vcam-publisher: header write failed: %s\n", strerror(errno));
            close(output);
            unlink(temporary_path);
            return 5;
        }

        uint32_t remaining = payload_size;
        while (remaining > 0) {
            size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            if (read_exact(STDIN_FILENO, buffer, chunk) != 1 ||
                write_exact(output, buffer, chunk) != 0) {
                fprintf(stderr, "vcam-publisher: payload transfer failed\n");
                close(output);
                unlink(temporary_path);
                return 6;
            }
            remaining -= (uint32_t)chunk;
        }

        if (close(output) != 0) {
            fprintf(stderr, "vcam-publisher: close failed: %s\n", strerror(errno));
            unlink(temporary_path);
            return 7;
        }
        if (rename(temporary_path, destination_path) != 0) {
            fprintf(stderr, "vcam-publisher: publish failed: %s\n", strerror(errno));
            unlink(temporary_path);
            return 8;
        }
    }
}
