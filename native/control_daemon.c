#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define SOCKET_NAME "android_vcam_control"
#define MANAGER_PACKAGE "io.github.androidvcam.manager"
#define REQUEST_MAGIC "VCAMD001"
#define RESPONSE_MAGIC "VCAMR001"
#define MAX_ARGUMENTS 8U
#define MAX_COMMAND 64U
#define MAX_ARGUMENT 4096U
#define MAX_OUTPUT (4U * 1024U * 1024U)
#define MAX_IMAGE_PAYLOAD (32ULL * 1024ULL * 1024ULL)
#define MAX_MEDIA_PAYLOAD (32ULL * 1024ULL * 1024ULL * 1024ULL)

struct command_rule {
    const char *name;
    uint32_t arguments;
    uint64_t maximum_payload;
};

static void reap_handlers(int signal_number) {
    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
}

static const struct command_rule k_rules[] = {
    {"status", 0, 0},
    {"providers", 0, 0},
    {"provider-add", 4, 0},
    {"provider-remove", 1, 0},
    {"provider-start", 1, 0},
    {"provider-stop", 1, 0},
    {"source-preview", 2, 0},
    {"provider-frame", 1, 0},
    {"provider-update", 8, 0},
    {"provider-publish-stdin", 1, MAX_IMAGE_PAYLOAD},
    {"provider-import-media", 1, MAX_MEDIA_PAYLOAD},
    {"provider-config", 1, 0},
    {"provider-config-set", 6, 0},
    {"routes", 0, 0},
    {"route-set", 3, 0},
    {"route-remove", 2, 0},
    {"route-save", 3, 0},
};

static bool read_exact(int fd, void *buffer, size_t length) {
    uint8_t *cursor = buffer;
    while (length > 0) {
        ssize_t count = read(fd, cursor, length);
        if (count == 0) return false;
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        cursor += count;
        length -= (size_t)count;
    }
    return true;
}

static bool write_exact(int fd, const void *buffer, size_t length) {
    const uint8_t *cursor = buffer;
    while (length > 0) {
        ssize_t count = write(fd, cursor, length);
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        cursor += count;
        length -= (size_t)count;
    }
    return true;
}

static uint64_t host_to_be64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(value >> 32))) |
           ((uint64_t)htonl((uint32_t)value) << 32);
#else
    return value;
#endif
}

static uint64_t be64_to_host(uint64_t value) {
    return host_to_be64(value);
}

static bool manager_uid(uid_t uid) {
    FILE *packages = fopen("/data/system/packages.list", "re");
    if (packages == NULL) return false;
    char *line = NULL;
    size_t capacity = 0;
    bool matched = false;
    while (getline(&line, &capacity, packages) >= 0) {
        char package[256];
        unsigned long package_uid;
        if (sscanf(line, "%255s %lu", package, &package_uid) == 2 &&
            strcmp(package, MANAGER_PACKAGE) == 0 && package_uid == (unsigned long)uid) {
            matched = true;
            break;
        }
    }
    free(line);
    fclose(packages);
    return matched;
}

static const struct command_rule *find_rule(const char *command) {
    for (size_t index = 0; index < sizeof(k_rules) / sizeof(k_rules[0]); ++index) {
        if (strcmp(command, k_rules[index].name) == 0) return &k_rules[index];
    }
    return NULL;
}

static void send_response(int fd, int32_t code, const void *output, uint32_t length) {
    uint32_t code_be = htonl((uint32_t)code);
    uint32_t length_be = htonl(length);
    if (!write_exact(fd, RESPONSE_MAGIC, 8) ||
        !write_exact(fd, &code_be, sizeof(code_be)) ||
        !write_exact(fd, &length_be, sizeof(length_be))) return;
    if (length > 0) write_exact(fd, output, length);
}

static int run_controller(int client, const char *controller, char *const argv[], uint64_t payload,
                          char **output, uint32_t *output_length) {
    int input_pipe[2];
    int output_pipe[2];
    if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) return 70;
    pid_t child = fork();
    if (child < 0) return 70;
    if (child == 0) {
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(input_pipe[0]); close(input_pipe[1]);
        close(output_pipe[0]); close(output_pipe[1]);
        execv(controller, argv);
        dprintf(STDERR_FILENO, "cannot execute controller: %s\n", strerror(errno));
        _exit(127);
    }
    close(input_pipe[0]);
    close(output_pipe[1]);
    uint8_t transfer[64 * 1024];
    bool transfer_ok = true;
    while (payload > 0) {
        size_t wanted = payload < sizeof(transfer) ? (size_t)payload : sizeof(transfer);
        if (!read_exact(client, transfer, wanted) || !write_exact(input_pipe[1], transfer, wanted)) {
            transfer_ok = false;
            break;
        }
        payload -= wanted;
    }
    close(input_pipe[1]);

    size_t used = 0;
    char *bytes = malloc(MAX_OUTPUT + 1U);
    if (bytes == NULL) transfer_ok = false;
    if (bytes != NULL) {
        for (;;) {
            ssize_t count = read(output_pipe[0], bytes + used, MAX_OUTPUT - used);
            if (count == 0) break;
            if (count < 0) {
                if (errno == EINTR) continue;
                transfer_ok = false;
                break;
            }
            used += (size_t)count;
            if (used == MAX_OUTPUT) {
                transfer_ok = false;
                break;
            }
        }
        bytes[used] = '\0';
    }
    close(output_pipe[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) { }
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 70;
    if (!transfer_ok && code == 0) code = 74;
    *output = bytes;
    *output_length = (uint32_t)used;
    return code;
}

static void handle_client(int client, const char *controller) {
    struct ucred credential;
    socklen_t credential_length = sizeof(credential);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credential, &credential_length) != 0 ||
        !manager_uid(credential.uid)) {
        send_response(client, 77, "unauthorized client\n", 20);
        return;
    }

    char magic[8];
    uint32_t command_length_be, argument_count_be;
    uint64_t payload_length_be;
    if (!read_exact(client, magic, sizeof(magic)) || memcmp(magic, REQUEST_MAGIC, 8) != 0 ||
        !read_exact(client, &command_length_be, sizeof(command_length_be)) ||
        !read_exact(client, &argument_count_be, sizeof(argument_count_be)) ||
        !read_exact(client, &payload_length_be, sizeof(payload_length_be))) return;
    uint32_t command_length = ntohl(command_length_be);
    uint32_t argument_count = ntohl(argument_count_be);
    uint64_t payload_length = be64_to_host(payload_length_be);
    if (command_length == 0 || command_length > MAX_COMMAND || argument_count > MAX_ARGUMENTS) {
        send_response(client, 64, "invalid request\n", 16);
        return;
    }

    char command[MAX_COMMAND + 1];
    if (!read_exact(client, command, command_length)) return;
    command[command_length] = '\0';
    const struct command_rule *rule = find_rule(command);
    if (rule == NULL || rule->arguments != argument_count ||
        payload_length > rule->maximum_payload ||
        (rule->maximum_payload == 0 && payload_length != 0)) {
        send_response(client, 64, "command rejected\n", 17);
        return;
    }

    char *arguments[MAX_ARGUMENTS + 2] = {0};
    arguments[0] = (char *)controller;
    arguments[1] = command;
    for (uint32_t index = 0; index < argument_count; ++index) {
        uint32_t length_be;
        if (!read_exact(client, &length_be, sizeof(length_be))) goto cleanup;
        uint32_t length = ntohl(length_be);
        if (length > MAX_ARGUMENT) {
            send_response(client, 64, "argument too long\n", 18);
            goto cleanup;
        }
        arguments[index + 2] = calloc((size_t)length + 1U, 1U);
        if (arguments[index + 2] == NULL ||
            !read_exact(client, arguments[index + 2], length)) goto cleanup;
    }

    char *output = NULL;
    uint32_t output_length = 0;
    int code = run_controller(client, controller, arguments, payload_length,
                              &output, &output_length);
    if (output == NULL) {
        output = strdup("controller transport failed\n");
        output_length = output == NULL ? 0 : 28;
    }
    send_response(client, code, output, output_length);
    free(output);

cleanup:
    for (uint32_t index = 0; index < argument_count; ++index) free(arguments[index + 2]);
}

int main(int argc, char **argv) {
    if (argc != 2 || access(argv[1], X_OK) != 0) {
        fprintf(stderr, "usage: vcamd /absolute/path/to/vcamctl\n");
        return 64;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, reap_handlers);
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) return 70;
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    address.sun_path[0] = '\0';
    memcpy(address.sun_path + 1, SOCKET_NAME, sizeof(SOCKET_NAME) - 1);
    socklen_t address_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                                           1 + sizeof(SOCKET_NAME) - 1);
    if (bind(server, (struct sockaddr *)&address, address_length) != 0 ||
        listen(server, 4) != 0) {
        fprintf(stderr, "socket setup failed: %s\n", strerror(errno));
        close(server);
        return 70;
    }
    fprintf(stderr, "vcamd: ready on @%s\n", SOCKET_NAME);
    for (;;) {
        int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        pid_t handler = fork();
        if (handler == 0) {
            signal(SIGCHLD, SIG_DFL);
            close(server);
            handle_client(client, argv[1]);
            close(client);
            _exit(0);
        }
        close(client);
    }
    close(server);
    return 70;
}
