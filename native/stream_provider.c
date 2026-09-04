/*
 * Copyright 2026 android-vcam contributors
 * Licensed under the Apache License, Version 2.0.
 */
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

#define HEADER_SIZE 24
#define CAMERA_AID 1006
#define INET_AID 3003
#define MAX_SOURCE_DIMENSION 4096
#define MAX_SOURCE_PIXELS (4096ULL * 3072ULL)
#define MAX_PIXEL_RATE (1920ULL * 1080ULL * 60ULL)

static const uint8_t kYuvMagic[8] = {'V', 'C', 'A', 'M', 'Y', 'U', 'V', '1'};
static const char* kProviderPrefix = "/data/vendor/camera/vcam/providers/";
static volatile sig_atomic_t gRunning = 1;

static int drop_to_camera(void) {
    if (getuid() == 0) {
        const gid_t supplementary_groups[] = {INET_AID};
        if (setgroups(1, supplementary_groups) != 0 ||
            setgid(CAMERA_AID) != 0 ||
            setuid(CAMERA_AID) != 0) {
            fprintf(stderr, "vcam-streamer: unable to drop to camera uid: %s\n",
                    strerror(errno));
            return -1;
        }
    }
    if (getuid() != CAMERA_AID || getgid() != CAMERA_AID) {
        fprintf(stderr, "vcam-streamer: must run as root or camera uid\n");
        return -1;
    }
    return 0;
}

static void handle_signal(int signal_number) {
    (void)signal_number;
    gRunning = 0;
}

static void write_le32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static int write_exact(int fd, const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t written = 0;
    while (written < size) {
        ssize_t count = write(fd, bytes + written, size - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        written += (size_t)count;
    }
    return 0;
}

static int publish_frame(const char* output_path, uint8_t* const data[4],
                         const int linesize[4], int width, int height,
                         uint32_t sequence) {
    char temporary[512];
    if (snprintf(temporary, sizeof(temporary), "%s.new", output_path) >=
        (int)sizeof(temporary)) return AVERROR(ENAMETOOLONG);
    const uint64_t pixels = (uint64_t)width * height;
    const uint64_t payload64 = pixels + pixels / 2;
    if (payload64 > UINT32_MAX) return AVERROR(EOVERFLOW);

    uint8_t header[HEADER_SIZE];
    memcpy(header, kYuvMagic, sizeof(kYuvMagic));
    write_le32(header + 8, (uint32_t)width);
    write_le32(header + 12, (uint32_t)height);
    write_le32(header + 16, (uint32_t)payload64);
    write_le32(header + 20, sequence);

    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                  0644);
    if (fd < 0) return AVERROR(errno);
    int result = 0;
    if (fchmod(fd, 0644) != 0 || fchown(fd, CAMERA_AID, CAMERA_AID) != 0 ||
        write_exact(fd, header, sizeof(header)) != 0) {
        result = AVERROR(errno == 0 ? EIO : errno);
    }
    for (int row = 0; result == 0 && row < height; ++row) {
        if (write_exact(fd, data[0] + row * linesize[0], (size_t)width) != 0) {
            result = AVERROR(errno == 0 ? EIO : errno);
        }
    }
    for (int plane = 1; result == 0 && plane <= 2; ++plane) {
        for (int row = 0; result == 0 && row < height / 2; ++row) {
            if (write_exact(fd, data[plane] + row * linesize[plane],
                            (size_t)width / 2) != 0) {
                result = AVERROR(errno == 0 ? EIO : errno);
            }
        }
    }
    if (close(fd) != 0 && result == 0) result = AVERROR(errno);
    if (result == 0 && rename(temporary, output_path) != 0) result = AVERROR(errno);
    if (result != 0) unlink(temporary);
    return result;
}

static void output_dimensions(int input_width, int input_height,
                              int max_width, int max_height,
                              int* output_width, int* output_height) {
    double scale = 1.0;
    if (input_width > max_width) scale = (double)max_width / input_width;
    if (input_height * scale > max_height) scale = (double)max_height / input_height;
    *output_width = (int)(input_width * scale + 0.5);
    *output_height = (int)(input_height * scale + 0.5);
    if (*output_width < 2) *output_width = 2;
    if (*output_height < 2) *output_height = 2;
    *output_width &= ~1;
    *output_height &= ~1;
}

static int decode_source(const char* source, const char* output_path,
                         int output_fps, int max_width, int max_height,
                         uint32_t* sequence, int single_frame) {
    AVFormatContext* format = NULL;
    AVCodecContext* codec = NULL;
    AVPacket* packet = NULL;
    AVFrame* frame = NULL;
    struct SwsContext* scaler = NULL;
    uint8_t* yuv_data[4] = {NULL};
    int yuv_linesize[4] = {0};
    AVDictionary* options = NULL;
    int video_stream = -1;
    int result = 0;
    int output_width = 0;
    int output_height = 0;
    int64_t next_frame_time = 0;
    const int live_source = strstr(source, "://") != NULL;
    int awaiting_keyframe = live_source;
    unsigned live_keyframes_seen = 0;

    av_dict_set(&options, "rw_timeout", "10000000", 0);
    // FFmpeg 4.2 interprets the deprecated RTSP "timeout" option as a
    // listen timeout and silently switches the demuxer into server mode.
    // stimeout is the client-side TCP I/O timeout on this pinned build.
    av_dict_set(&options, "stimeout", "10000000", 0);
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "user_agent", "android-vcam/0.3", 0);
    const char* ca_file = getenv("ANDROID_VCAM_CA_FILE");
    if (ca_file != NULL && ca_file[0] != '\0') {
        av_dict_set(&options, "tls_verify", "1", 0);
        av_dict_set(&options, "ca_file", ca_file, 0);
    } else if (strncmp(source, "https://", 8) == 0) {
        fprintf(stderr, "vcam-streamer: HTTPS requires an Android CA bundle\n");
        result = AVERROR(EACCES);
        av_dict_free(&options);
        goto cleanup;
    }
    result = avformat_open_input(&format, source, NULL, &options);
    av_dict_free(&options);
    if (result < 0) goto cleanup;
    result = avformat_find_stream_info(format, NULL);
    if (result < 0) goto cleanup;

    AVCodec* decoder = NULL;
    video_stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                       &decoder, 0);
    if (video_stream < 0 || decoder == NULL) {
        result = video_stream < 0 ? video_stream : AVERROR_DECODER_NOT_FOUND;
        goto cleanup;
    }
    codec = avcodec_alloc_context3(decoder);
    if (codec == NULL) { result = AVERROR(ENOMEM); goto cleanup; }
    result = avcodec_parameters_to_context(
            codec, format->streams[video_stream]->codecpar);
    if (result < 0) goto cleanup;
    codec->thread_count = 0;
    codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    result = avcodec_open2(codec, decoder, NULL);
    if (result < 0) goto cleanup;

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (packet == NULL || frame == NULL) { result = AVERROR(ENOMEM); goto cleanup; }

    int draining = 0;
    while (gRunning) {
        if (!draining) {
            result = av_read_frame(format, packet);
            if (result == AVERROR_EOF) {
                result = avcodec_send_packet(codec, NULL);
                if (result < 0 && result != AVERROR_EOF) goto cleanup;
                draining = 1;
            } else if (result < 0) {
                goto cleanup;
            } else if (packet->stream_index != video_stream) {
                av_packet_unref(packet);
                continue;
            } else {
                result = avcodec_send_packet(codec, packet);
                av_packet_unref(packet);
                if (result < 0 && result != AVERROR(EAGAIN)) goto cleanup;
            }
        }

        while (gRunning && (result = avcodec_receive_frame(codec, frame)) >= 0) {
            // RTSP sessions can begin in the middle of a GOP. Some encoders
            // also mark early recovery pictures as keyframes before all H.264
            // reference state is usable. If one is published, text and QR
            // codes remain visibly fragmented. Let three decoder-reported
            // keyframes establish reference state and never publish frames
            // that FFmpeg explicitly marks corrupt.
            if (live_source && frame->key_frame && live_keyframes_seen < 3) {
                ++live_keyframes_seen;
            }
            if ((awaiting_keyframe && live_keyframes_seen < 3) ||
                frame->decode_error_flags != 0 ||
                (frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) {
                av_frame_unref(frame);
                continue;
            }
            awaiting_keyframe = 0;
            if (scaler == NULL || output_width == 0 || output_height == 0) {
                output_dimensions(frame->width, frame->height,
                                  max_width, max_height,
                                  &output_width, &output_height);
                scaler = sws_getContext(frame->width, frame->height,
                        (enum AVPixelFormat)frame->format,
                        output_width, output_height, AV_PIX_FMT_YUV420P,
                        SWS_BILINEAR, NULL, NULL, NULL);
                if (scaler == NULL) { result = AVERROR(EINVAL); goto cleanup; }
                result = av_image_alloc(yuv_data, yuv_linesize, output_width,
                                        output_height, AV_PIX_FMT_YUV420P, 1);
                if (result < 0) goto cleanup;
            }
            const int64_t now = av_gettime_relative();
            if (!single_frame && next_frame_time > now) {
                if (live_source) {
                    av_frame_unref(frame);
                    continue;
                }
                av_usleep((unsigned)(next_frame_time - now));
            }
            sws_scale(scaler, (const uint8_t* const*)frame->data,
                      frame->linesize, 0, frame->height, yuv_data, yuv_linesize);

            result = publish_frame(output_path, yuv_data, yuv_linesize,
                                   output_width, output_height, ++*sequence);
            if (result < 0) goto cleanup;
            if (single_frame) {
                result = 0;
                goto cleanup;
            }
            next_frame_time = av_gettime_relative() + 1000000 / output_fps;
            av_frame_unref(frame);
        }
        if (result == AVERROR_EOF) {
            result = 0;
            break;
        }
        if (result == AVERROR(EAGAIN)) {
            result = 0;
            if (draining) break;
            continue;
        }
        if (result < 0) goto cleanup;
    }
    if (!gRunning) result = 0;

cleanup:
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, message, sizeof(message));
        fprintf(stderr, "vcam-streamer: %s: %s\n", source, message);
    }
    if (yuv_data[0] != NULL) av_freep(&yuv_data[0]);
    if (scaler != NULL) sws_freeContext(scaler);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
    return result;
}

int main(int argc, char** argv) {
    const int single_frame = argc == 7 && strcmp(argv[6], "--once") == 0;
    if ((argc != 6 && !single_frame) || strncmp(argv[2], kProviderPrefix,
                             strlen(kProviderPrefix)) != 0) {
        fprintf(stderr, "usage: vcam-streamer <url-or-path> <provider-frame.rgb> <fps> <max-width> <max-height> [--once]\n");
        return 64;
    }
    const int output_fps = atoi(argv[3]);
    const int max_width = atoi(argv[4]);
    const int max_height = atoi(argv[5]);
    const uint64_t configured_pixels = (uint64_t)max_width * max_height;
    if (output_fps < 1 || output_fps > 60 || max_width < 160 ||
        max_width > MAX_SOURCE_DIMENSION || max_height < 120 ||
        max_height > MAX_SOURCE_DIMENSION || configured_pixels > MAX_SOURCE_PIXELS ||
        configured_pixels * (uint64_t)output_fps > MAX_PIXEL_RATE) {
        fprintf(stderr, "vcam-streamer: invalid output configuration\n");
        return 64;
    }
    // The APatch magisk SELinux domain has Android's default-network access,
    // while the isolated controller domain does not. Drop DAC privileges
    // before parsing any untrusted media or opening a network socket.
    if (drop_to_camera() != 0) return 77;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);
    av_log_set_level(AV_LOG_WARNING);
    avformat_network_init();

    uint32_t sequence = (uint32_t)(av_gettime_relative() / 1000000);
    while (gRunning) {
        const int result = decode_source(argv[1], argv[2], output_fps,
                                         max_width, max_height, &sequence,
                                         single_frame);
        if (single_frame) {
            avformat_network_deinit();
            return result < 0 ? 69 : 0;
        }
        if (gRunning) av_usleep(2000000);
    }
    avformat_network_deinit();
    return 0;
}
