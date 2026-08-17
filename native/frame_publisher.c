/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static void write_le32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static int is_provider_path(const char* path) {
    return path != NULL && strncmp(path, kProviderPrefix, strlen(kProviderPrefix)) == 0 &&
           path[strlen(kProviderPrefix)] != '\0' && strstr(path, "..") == NULL;
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

static int pread_exact(int fd, void* destination, size_t size, off_t offset) {
    uint8_t* output = (uint8_t*)destination;
    size_t consumed = 0;
    while (consumed < size) {
        ssize_t count = pread(fd, output + consumed, size - consumed,
                              offset + (off_t)consumed);
        if (count == 0) return -1;
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        consumed += (size_t)count;
    }
    return 0;
}

static int thumbnail_frame(const char* source_path, const char* destination_path,
                           uint32_t max_width, uint32_t max_height) {
    uint8_t header[HEADER_SIZE];
    char temporary_path[512];
    struct stat source_stat;
    int input = -1;
    int output = -1;
    uint8_t* source_row = NULL;
    uint8_t* output_row = NULL;
    int result = 0;

    if (!is_provider_path(source_path) || !is_provider_path(destination_path) ||
        max_width < 16 || max_height < 16 || max_width > 1024 || max_height > 1024) {
        fprintf(stderr, "vcam-publisher: invalid thumbnail arguments\n");
        return 64;
    }
    if (snprintf(temporary_path, sizeof(temporary_path), "%s.new", destination_path) >=
        (int)sizeof(temporary_path)) {
        fprintf(stderr, "vcam-publisher: thumbnail path is too long\n");
        return 64;
    }
    input = open(source_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0 || fstat(input, &source_stat) != 0 || !S_ISREG(source_stat.st_mode) ||
        pread_exact(input, header, sizeof(header), 0) != 0 ||
        memcmp(header, kMagic, sizeof(kMagic)) != 0) {
        fprintf(stderr, "vcam-publisher: invalid thumbnail source\n");
        result = 2;
        goto cleanup;
    }

    const uint32_t width = read_le32(header + 8);
    const uint32_t height = read_le32(header + 12);
    const uint32_t payload_size = read_le32(header + 16);
    const uint64_t expected = (uint64_t)width * height * 3u;
    if (width == 0 || height == 0 || width > MAX_DIMENSION || height > MAX_DIMENSION ||
        expected != payload_size || source_stat.st_size != (off_t)(HEADER_SIZE + expected)) {
        fprintf(stderr, "vcam-publisher: unsupported thumbnail source dimensions\n");
        result = 3;
        goto cleanup;
    }

    uint32_t output_width = width;
    uint32_t output_height = height;
    if (output_width > max_width) {
        output_height = (uint32_t)(((uint64_t)output_height * max_width) / output_width);
        output_width = max_width;
    }
    if (output_height > max_height) {
        output_width = (uint32_t)(((uint64_t)output_width * max_height) / output_height);
        output_height = max_height;
    }
    if (output_width == 0) output_width = 1;
    if (output_height == 0) output_height = 1;
    const uint32_t output_payload = output_width * output_height * 3u;
    write_le32(header + 8, output_width);
    write_le32(header + 12, output_height);
    write_le32(header + 16, output_payload);

    source_row = (uint8_t*)malloc((size_t)width * 3u);
    output_row = (uint8_t*)malloc((size_t)output_width * 3u);
    if (source_row == NULL || output_row == NULL) {
        fprintf(stderr, "vcam-publisher: thumbnail allocation failed\n");
        result = 4;
        goto cleanup;
    }
    output = open(temporary_path,
                  O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (output < 0 || fchmod(output, 0644) != 0 ||
        fchown(output, CAMERA_AID, CAMERA_AID) != 0 ||
        write_exact(output, header, sizeof(header)) != 0) {
        fprintf(stderr, "vcam-publisher: thumbnail output failed: %s\n", strerror(errno));
        result = 5;
        goto cleanup;
    }

    for (uint32_t y = 0; y < output_height; ++y) {
        const uint32_t source_y = (uint32_t)(((uint64_t)y * height) / output_height);
        const off_t row_offset = (off_t)HEADER_SIZE + (off_t)source_y * width * 3u;
        if (pread_exact(input, source_row, (size_t)width * 3u, row_offset) != 0) {
            fprintf(stderr, "vcam-publisher: thumbnail source changed while reading\n");
            result = 6;
            goto cleanup;
        }
        for (uint32_t x = 0; x < output_width; ++x) {
            const uint32_t source_x = (uint32_t)(((uint64_t)x * width) / output_width);
            memcpy(output_row + (size_t)x * 3u, source_row + (size_t)source_x * 3u, 3u);
        }
        if (write_exact(output, output_row, (size_t)output_width * 3u) != 0) {
            fprintf(stderr, "vcam-publisher: thumbnail write failed\n");
            result = 6;
            goto cleanup;
        }
    }
    if (close(output) != 0) {
        output = -1;
        result = 7;
        goto cleanup;
    }
    output = -1;
    if (rename(temporary_path, destination_path) != 0) {
        fprintf(stderr, "vcam-publisher: thumbnail publish failed: %s\n", strerror(errno));
        result = 8;
        goto cleanup;
    }

cleanup:
    if (input >= 0) close(input);
    if (output >= 0) close(output);
    free(source_row);
    free(output_row);
    if (result != 0) unlink(temporary_path);
    return result;
}

int main(int argc, char** argv) {
    if (argc == 6 && strcmp(argv[1], "--thumbnail") == 0) {
        char* width_end = NULL;
        char* height_end = NULL;
        unsigned long max_width = strtoul(argv[4], &width_end, 10);
        unsigned long max_height = strtoul(argv[5], &height_end, 10);
        if (width_end == argv[4] || *width_end != '\0' || height_end == argv[5] ||
            *height_end != '\0' || max_width > UINT32_MAX || max_height > UINT32_MAX) {
            fprintf(stderr, "vcam-publisher: invalid thumbnail dimensions\n");
            return 64;
        }
        return thumbnail_frame(argv[2], argv[3], (uint32_t)max_width,
                               (uint32_t)max_height);
    }
    if (argc > 2) {
        fprintf(stderr, "usage: vcam-publisher [destination] | "
                        "vcam-publisher --thumbnail source destination max-width max-height\n");
        return 64;
    }
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
