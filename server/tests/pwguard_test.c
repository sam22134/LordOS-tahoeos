#define _GNU_SOURCE
/* pwguard_test.c — the PipeWire guard's pieces that don't need PipeWire (src/sec/pwguard.c): the
 * SPA POD reader on well-formed, truncated and lying input, and which nodes and streams each
 * restriction covers. The guard against a real PipeWire: tests/fwlinux/mic.sh. */
#include "sec/pwguard.h"
#include "fw/rules.h"
#include "sec/broker.h"
#include "sec/perms.h"

#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>

void brokersrv_revoke(const char *app, const char *perm) { (void)app; (void)perm; }
PermState perm_get(const char *app_id, Perm p) { (void)app_id; (void)p; return PERM_ASK; }

static int fails, total;
#define CHECK(c) do { total++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #c); } } while (0)

static size_t put(uint8_t *b, size_t o, uint32_t v) { memcpy(b + o, &v, 4); return o + 4; }



static size_t put_pod_int(uint8_t *b, size_t o, int32_t v) {
    o = put(b, o, 4); o = put(b, o, 4); o = put(b, o, (uint32_t)v);
    return o + 4; /* SPA POD alignment */
}

static size_t put_pod_string(uint8_t *b, size_t o, const char *v) {
    size_t n = strlen(v) + 1;
    o = put(b, o, (uint32_t)n); o = put(b, o, 8);
    memcpy(b + o, v, n);
    o += n;
    return o + ((8 - (n & 7)) & 7);
}

static size_t put_dict(uint8_t *b, size_t o, int count, const char **keys, const char **values) {
    size_t start = o;
    o = put(b, o, 0); o = put(b, o, 14);
    o = put_pod_int(b, o, count);
    for (int i = 0; i < count; i++) {
        o = put_pod_string(b, o, keys[i]);
        o = put_pod_string(b, o, values[i]);
    }
    put(b, start, (uint32_t)(o - start - 8));
    return o;
}

static void test_well_formed_events_keep_connection(void) {
    char dir[] = "/tmp/lordos-pwguard-valid-XXXXXX";
    if (!mkdtemp(dir)) { CHECK(0); return; }
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    snprintf(path, sizeof path, "%s/pipewire-0-manager", dir);
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(server >= 0);
    if (server < 0) { rmdir(dir); return; }
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (bind(server, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        listen(server, 1) != 0 || setenv("XDG_RUNTIME_DIR", dir, 1) != 0 ||
        pwguard_start() != 0) {
        CHECK(0); close(server); unlink(path); rmdir(dir); return;
    }
    CHECK(1);
    int peer = accept(server, NULL, NULL);
    CHECK(peer >= 0);
    if (peer >= 0) {
        uint8_t packet[160] = { 0 }, body[96] = { 0 };
        size_t n = 8;
        n = put_pod_int(body, n, 0);
        n = put_pod_int(body, n, 0);
        put(body, 0, (uint32_t)(n - 8)); put(body, 4, 14);
        uint32_t header[4] = { 0, (1u << 24) | (uint32_t)n, 41, 0 };
        memcpy(packet, header, sizeof header);
        memcpy(packet + sizeof header, body, n);
        CHECK(send(peer, packet, 16 + n, 0) == (ssize_t)(16 + n));
        pwguard_dispatch();
        CHECK(pwguard_fd() >= 0); /* valid Core::Done with no pending request is harmless */

        memset(packet, 0, sizeof packet); memset(body, 0, sizeof body);
        n = 8;
        n = put_pod_int(body, n, 0);
        n = put_pod_int(body, n, 42);
        n = put_pod_int(body, n, -1);
        n = put_pod_string(body, n, "mock error");
        put(body, 0, (uint32_t)(n - 8)); put(body, 4, 14);
        header[1] = (3u << 24) | (uint32_t)n;
        header[2] = 42;
        memcpy(packet, header, sizeof header);
        memcpy(packet + sizeof header, body, n);
        CHECK(send(peer, packet, 16 + n, 0) == (ssize_t)(16 + n));
        pwguard_dispatch();
        CHECK(pwguard_fd() >= 0); /* well-formed Core::Error is parsed, not treated as framing failure */
        shutdown(peer, SHUT_RDWR);
        pwguard_dispatch();
        close(peer);
    }
    close(server);
    unlink(path);
    rmdir(dir);
}

static void test_unexpected_fd_count_resets_connection(void) {
    char dir[] = "/tmp/lordos-pwguard-fdcount-XXXXXX";
    if (!mkdtemp(dir)) { CHECK(0); return; }
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    snprintf(path, sizeof path, "%s/pipewire-0-manager", dir);
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(server >= 0);
    if (server < 0) { rmdir(dir); return; }
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (bind(server, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        listen(server, 1) != 0 || setenv("XDG_RUNTIME_DIR", dir, 1) != 0 ||
        pwguard_start() != 0) {
        CHECK(0); close(server); unlink(path); rmdir(dir); return;
    }
    int peer = accept(server, NULL, NULL);
    CHECK(peer >= 0);
    if (peer >= 0) {
        uint8_t packet[64] = { 0 }, body[48] = { 0 };
        size_t n = 8;
        n = put_pod_int(body, n, 0);
        n = put_pod_int(body, n, 41);
        put(body, 0, (uint32_t)(n - 8)); put(body, 4, 14);
        uint32_t header[4] = { 0, (1u << 24) | (uint32_t)n, 41, 1 };
        memcpy(packet, header, sizeof header);
        memcpy(packet + sizeof header, body, n);
        CHECK(send(peer, packet, 16 + n, 0) == (ssize_t)(16 + n));
        pwguard_dispatch();
        CHECK(pwguard_fd() == -1); /* valid payload, but header claims an unsupported FD */
        close(peer);
    }
    close(server);
    unlink(path);
    rmdir(dir);
}

static void test_permission_sync_completion(void) {
    uint32_t applied = 0xdeadbeefu;
    CHECK(pwguard_sync_complete(1, 1, 0, 0, &applied) == 0);
    CHECK(applied == 1);
    CHECK(pwguard_sync_complete(2, 4, 0, 0, &applied) == 1);
    CHECK(applied == 2); /* confirmed state advances before the newer desired state is resent */
    CHECK(pwguard_sync_complete(8, 8, 0, 1, &applied) == 1);
    CHECK(applied == 8); /* a policy change during the pending batch forces another batch */
    applied = 0xdeadbeefu;
    CHECK(pwguard_sync_complete(16, 16, 1, 0, &applied) == -1);
    CHECK(applied == 0xdeadbeefu); /* failed update never becomes applied */
    CHECK(pwguard_sync_complete(16, 16, 1, 1, NULL) == -1);
    CHECK(pwguard_sync_complete(16, 16, 0, 0, NULL) == -1);
}

static void test_permission_error_sequence_matching(void) {
    const uint32_t seqs[] = { 7u, 0x80000001u, UINT32_MAX };
    CHECK(pwguard_error_matches(23, 91, seqs, 3, 23, 7));
    CHECK(pwguard_error_matches(23, 91, seqs, 3, 23, (int32_t)0x80000001u));
    CHECK(pwguard_error_matches(23, 91, seqs, 3, 23, -1));
    CHECK(pwguard_error_matches(23, 91, seqs, 3, 0, 91));
    CHECK(!pwguard_error_matches(23, 91, seqs, 3, 23, 8));
    CHECK(!pwguard_error_matches(23, 91, seqs, 3, 24, 7));
    CHECK(!pwguard_error_matches(23, 91, seqs, 3, 0, 90));
    CHECK(!pwguard_error_matches(23, 91, NULL, 3, 23, 7));
    CHECK(!pwguard_error_matches(23, 91, seqs, 0, 23, 7));
}

static void test_malformed_event_resets_connection(uint32_t object_id, uint32_t opcode) {
    char dir[] = "/tmp/lordos-pwguard-XXXXXX";
    if (!mkdtemp(dir)) { CHECK(0); return; }
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    snprintf(path, sizeof path, "%s/pipewire-0-manager", dir);

    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(server >= 0);
    if (server < 0) { rmdir(dir); return; }

    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (bind(server, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        listen(server, 1) != 0 || setenv("XDG_RUNTIME_DIR", dir, 1) != 0 ||
        pwguard_start() != 0) {
        CHECK(0);
        close(server);
        unlink(path);
        rmdir(dir);
        return;
    }
    CHECK(1);
    int peer = accept(server, NULL, NULL);
    CHECK(peer >= 0);
    if (peer >= 0) {
        /* An empty Core::Done/Core::Error body is malformed: drop the socket. */
        uint32_t header[4] = { object_id, opcode << 24, 0, 0 };
        CHECK(send(peer, header, sizeof header, 0) == (ssize_t)sizeof header);
        pwguard_dispatch();
        CHECK(pwguard_fd() == -1);
        close(peer);
    } else if (pwguard_fd() >= 0) {
        /* Avoid leaving a live test connection if accept unexpectedly failed. */
        pwguard_dispatch();
    }
    close(server);
    unlink(path);
    rmdir(dir);
}

static void test_registry_global_trailing_bytes_reset_connection(void) {
    char dir[] = "/tmp/lordos-pwguard-trailing-XXXXXX";
    if (!mkdtemp(dir)) { CHECK(0); return; }
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    snprintf(path, sizeof path, "%s/pipewire-0-manager", dir);
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(server >= 0);
    if (server < 0) { rmdir(dir); return; }
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (bind(server, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        listen(server, 1) != 0 || setenv("XDG_RUNTIME_DIR", dir, 1) != 0 ||
        pwguard_start() != 0) {
        CHECK(0); close(server); unlink(path); rmdir(dir); return;
    }
    int peer = accept(server, NULL, NULL);
    CHECK(peer >= 0);
    if (peer >= 0) {
        uint8_t packet[160] = { 0 }, body[128] = { 0 };
        size_t n = 8;
        n = put_pod_int(body, n, 7);          /* global id */
        n = put_pod_int(body, n, 0);          /* permissions */
        n = put_pod_string(body, n, "PipeWire:Interface:Client");
        n = put_pod_int(body, n, 3);          /* interface version */
        put(body, 0, (uint32_t)(n - 8)); put(body, 4, 14);
        body[n++] = 0xaa;                     /* bytes outside the declared Struct */
        uint32_t header[4] = { 2, (uint32_t)n, 51, 0 };
        header[1] |= 0u << 24;
        memcpy(packet, header, sizeof header);
        memcpy(packet + sizeof header, body, n);
        CHECK(send(peer, packet, 16 + n, 0) == (ssize_t)(16 + n));
        pwguard_dispatch();
        CHECK(pwguard_fd() == -1);            /* trailing bytes invalidate the event */
        close(peer);
    }
    close(server);
    unlink(path);
    rmdir(dir);
}

static int test_pwguard_socket(char *dir, size_t dir_size, char *path, size_t path_size,
                               int *server, int *peer) {
    snprintf(dir, dir_size, "/tmp/lordos-pwguard-life-XXXXXX");
    if (!mkdtemp(dir)) return -1;
    snprintf(path, path_size, "%s/pipewire-0-manager", dir);
    *server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (*server < 0) return -1;
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (bind(*server, (struct sockaddr *)&sa, sizeof sa) ||
        listen(*server, 1) || setenv("XDG_RUNTIME_DIR", dir, 1) ||
        pwguard_start() != 0) return -1;
    *peer = accept(*server, NULL, NULL);
    if (*peer >= 0) {\n        pwguard_tick();\n        int tick_ms = pwguard_timeout_ms();\n        CHECK(tick_ms > 0 && tick_ms <= 1000);\n    }
    return *peer < 0 ? -1 : 0;
}

static void test_unknown_info_proxy_resets_connection(void) {
    char dir[64] = { 0 }, path[sizeof(((struct sockaddr_un *)0)->sun_path)] = { 0 };
    int server = -1, peer = -1;
    if (test_pwguard_socket(dir, sizeof dir, path, sizeof path, &server, &peer)) {
        CHECK(0);
        if (peer >= 0) close(peer);
        if (server >= 0) close(server);
        if (path[0]) unlink(path);
        if (dir[0]) rmdir(dir);
        return;
    }
    uint32_t header[4] = { 99, 0, 71, 0 }; /* Info event on a proxy never bound by us */
    CHECK(send(peer, header, sizeof header, 0) == (ssize_t)sizeof header);
    pwguard_dispatch();
    CHECK(pwguard_fd() == -1);
    close(peer); close(server); unlink(path); rmdir(dir);
}

static void test_duplicate_tracked_global_resets_connection(void) {
    char dir[64], path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    int server = -1, peer = -1;
    if (test_pwguard_socket(dir, sizeof dir, path, sizeof path, &server, &peer)) {
        CHECK(0);
        if (peer >= 0) close(peer);
        if (server >= 0) close(server);
        if (path[0]) unlink(path);
        if (dir[0]) rmdir(dir);
        return;
    }
    uint8_t packet[160] = { 0 }, body[128] = { 0 };
    size_t n = 8;
    n = put_pod_int(body, n, 7);
    n = put_pod_int(body, n, 0);
    n = put_pod_string(body, n, "PipeWire:Interface:Client");
    n = put_pod_int(body, n, 3);
    put(body, 0, (uint32_t)(n - 8)); put(body, 4, 14);
    uint32_t header[4] = { 2, (uint32_t)n, 72, 0 };
    memcpy(packet, header, sizeof header);
    memcpy(packet + sizeof header, body, n);
    CHECK(send(peer, packet, 16 + n, 0) == (ssize_t)(16 + n));
    pwguard_dispatch();
    CHECK(pwguard_fd() >= 0); /* first announcement allocates exactly one client */
    CHECK(send(peer, packet, 16 + n, 0) == (ssize_t)(16 + n));
    pwguard_dispatch();
    CHECK(pwguard_fd() == -1); /* duplicate global ID invalidates this registry snapshot */
    close(peer); close(server); unlink(path); rmdir(dir);
}

static void test_core_event_extra_fields_reset_connection(uint32_t opcode) {
    char dir[64] = { 0 }, path[sizeof(((struct sockaddr_un *)0)->sun_path)] = { 0 };
    int server = -1, peer = -1;
    if (test_pwguard_socket(dir, sizeof dir, path, sizeof path, &server, &peer)) {
        CHECK(0);
        if (peer >= 0) close(peer);
        if (server >= 0) close(server);
        if (path[0]) unlink(path);
        if (dir[0]) rmdir(dir);
        return;
    }
    uint8_t packet[192] = { 0 }, body[160] = { 0 };
    size_t n = 8;
    if (opcode == 3) { /* Core::Error: four fields, followed by an illegal fifth */
        n = put_pod_int(body, n, 0);
        n = put_pod_int(body, n, 9);
        n = put_pod_int(body, n, -1);
        n = put_pod_string(body, n, "mock error");
    } else {
        n = put_pod_int(body, n, 0);
        n = put_pod_int(body, n, 9);
    }
    n = put_pod_int(body, n, 123); /* extra field must not be silently accepted */
    put(body, 0, (uint32_t)(n - 8)); put(body, 4, 14);
    uint32_t header[4] = { 0, (opcode << 24) | (uint32_t)n, 81, 0 };
    memcpy(packet, header, sizeof header);
    memcpy(packet + sizeof header, body, n);
    CHECK(send(peer, packet, 16 + n, 0) == (ssize_t)(16 + n));
    pwguard_dispatch();
    CHECK(pwguard_fd() == -1);
    close(peer); close(server); unlink(path); rmdir(dir);
}

static void test_disconnect_reconnects_and_restarts_protocol(void) {
    char dir[] = "/tmp/lordos-pwguard-reconnect-XXXXXX";
    if (!mkdtemp(dir)) { CHECK(0); return; }
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    snprintf(path, sizeof path, "%s/pipewire-0-manager", dir);
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(server >= 0);
    if (server < 0) { rmdir(dir); return; }
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (bind(server, (struct sockaddr *)&sa, sizeof sa) ||
        listen(server, 2) || setenv("XDG_RUNTIME_DIR", dir, 1) ||
        pwguard_start() != 0) {
        CHECK(0); close(server); unlink(path); rmdir(dir); return;
    }

    int first = accept(server, NULL, NULL);
    CHECK(first >= 0);
    if (first < 0) { close(server); unlink(path); rmdir(dir); return; }
    close(first);
    pwguard_dispatch();
    CHECK(pwguard_fd() == -1); /* EOF drops the old registry snapshot */
    int retry_ms = pwguard_timeout_ms();
    CHECK(retry_ms > 0 && retry_ms <= 2000);

    /* Reconnect is intentionally rate-limited to two seconds after a transport failure. */
    struct timespec pause = { .tv_sec = 2, .tv_nsec = 200000000 };
    while (nanosleep(&pause, &pause) && errno == EINTR) {}
    CHECK(pwguard_timeout_ms() == 0); /* the event loop can wake exactly when retry becomes due */
    pwguard_tick();
    CHECK(pwguard_fd() >= 0);

    struct pollfd ready = { .fd = server, .events = POLLIN };
    int ready_n = poll(&ready, 1, 3000);
    CHECK(ready_n == 1 && (ready.revents & POLLIN));
    int second = ready_n == 1 && (ready.revents & POLLIN) ? accept(server, NULL, NULL) : -1;
    CHECK(second >= 0);
    if (second >= 0) {
        struct pollfd incoming = { .fd = second, .events = POLLIN };
        uint8_t hello[64];
        int incoming_n = poll(&incoming, 1, 1000);
        ssize_t n = incoming_n == 1 && (incoming.revents & POLLIN) ? recv(second, hello, sizeof hello, 0) : -1;
        CHECK(n >= 16); /* a fresh core.hello/client.update_properties/get_registry sequence */
        close(second);
        pwguard_dispatch();
    }
    close(server);
    unlink(path);
    rmdir(dir);
}

int main(void) {
    /* Struct { Int 7, String "Audio/Source", Int -1 } */
    uint8_t b[128] = { 0 };
    size_t o = 8;
    o = put(b, o, 4); o = put(b, o, 4); o = put(b, o, 7); o += 4;
    o = put(b, o, 13); o = put(b, o, 8); memcpy(b + o, "Audio/Source", 13); o += 16;
    o = put(b, o, 4); o = put(b, o, 4); o = put(b, o, 0xffffffffu); o += 4;
    put(b, 0, (uint32_t)(o - 8)); put(b, 4, 14);
    PwPod s = { b, o }, f;
    int32_t v;
    const char *str;
    CHECK(!pwpod_struct_field(s, 0, &f) && !pwpod_int(f, &v) && v == 7);
    CHECK(!pwpod_struct_field(s, 1, &f) && !pwpod_string(f, &str) && !strcmp(str, "Audio/Source"));
    CHECK(!pwpod_struct_field(s, 2, &f) && !pwpod_int(f, &v) && v == -1);
    CHECK(pwpod_struct_field(s, 3, &f) != 0);                          /* past the end */
    CHECK(!pwpod_struct_field(s, 1, &f) && pwpod_int(f, &v) != 0);     /* a string is not an int */
    PwPod cut = { b, o - 10 };
    CHECK(pwpod_struct_field(cut, 0, &f) != 0);                        /* the struct says more than there is */
    uint8_t unpadded[20] = { 0 };
    put(unpadded, 0, 12); put(unpadded, 4, 14);
    put(unpadded, 8, 4); put(unpadded, 12, 4); put(unpadded, 16, 7);
    PwPod up = { unpadded, sizeof unpadded };
    CHECK(pwpod_struct_field(up, 0, &f) != 0);                         /* a child missing its 8-byte padding */
    uint8_t lie[64];
    memcpy(lie, b, sizeof lie);
    put(lie, 8, 400);                                                  /* a field longer than the struct */
    PwPod l = { lie, o };
    CHECK(pwpod_struct_field(l, 0, &f) != 0);
    uint8_t nonul[40] = { 0 };
    put(nonul, 0, 24); put(nonul, 4, 14); put(nonul, 8, 4); put(nonul, 12, 8); memcpy(nonul + 16, "abcd", 4);
    PwPod nn = { nonul, 32 };
    CHECK(!pwpod_struct_field(nn, 0, &f) && pwpod_string(f, &str) != 0); /* string without its 0 */
    uint8_t oversized[16] = { 0 };
    put(oversized, 0, 64); put(oversized, 4, 8);
    PwPod os = { oversized, sizeof oversized };
    CHECK(pwpod_string(os, &str) != 0); /* claimed string length exceeds POD buffer */
    uint8_t embedded[16] = { 0 };
    put(embedded, 0, 4); put(embedded, 4, 8); memcpy(embedded + 8, "a\0b", 4);
    PwPod es = { embedded, sizeof embedded };
    CHECK(pwpod_string(es, &str) != 0); /* embedded NUL makes the value ambiguous */

    /* Dictionary lookup validates the complete pair sequence and rejects duplicate identity keys. */
    uint8_t dbuf[512] = { 0 };
    const char *dkeys[] = { "application.process.id", "media.class" };
    const char *dvals[] = { "123", "Audio/Source" };
    size_t dn = put_dict(dbuf, 0, 2, dkeys, dvals);
    PwPod dict = { dbuf, dn };
    CHECK(pwguard_dict_lookup(dict, "application.process.id", &str) == 1 && !strcmp(str, "123"));
    CHECK(pwguard_dict_lookup(dict, "missing", &str) == 0 && str == NULL);
    const char *dupkeys[] = { "application.process.id", "application.process.id" };
    const char *samevals[] = { "123", "123" };
    dn = put_dict(dbuf, 0, 2, dupkeys, samevals);
    dict = (PwPod){ dbuf, dn };
    CHECK(pwguard_dict_lookup(dict, "application.process.id", &str) == -1);
    const char *diffvals[] = { "123", "456" };
    dn = put_dict(dbuf, 0, 2, dupkeys, diffvals);
    dict = (PwPod){ dbuf, dn };
    CHECK(pwguard_dict_lookup(dict, "application.process.id", &str) == -1);
    dict.n--;
    CHECK(pwguard_dict_lookup(dict, "application.process.id", &str) == -1);

    CHECK(pwguard_next_sync_token(0, NULL, 0) == 1);
    CHECK(pwguard_next_sync_token(1, NULL, 0) == 2);
    CHECK(pwguard_next_sync_token(INT32_MAX, NULL, 0) == 1);
    const uint32_t pending_tokens[] = { 1, 2, 4 };
    CHECK(pwguard_next_sync_token(INT32_MAX, pending_tokens, 3) == 3);
    CHECK(pwguard_next_sync_token(1, pending_tokens, 3) == 3);

    uint64_t parsed64;
    CHECK(!pwguard_parse_u64("0", &parsed64) && parsed64 == 0);
    CHECK(!pwguard_parse_u64("18446744073709551615", &parsed64) && parsed64 == UINT64_MAX);
    CHECK(pwguard_parse_u64("", &parsed64) != 0);
    CHECK(pwguard_parse_u64("-1", &parsed64) != 0);
    CHECK(pwguard_parse_u64("+1", &parsed64) != 0);
    CHECK(pwguard_parse_u64(" 1", &parsed64) != 0);
    CHECK(pwguard_parse_u64("1 ", &parsed64) != 0);
    CHECK(pwguard_parse_u64("12x", &parsed64) != 0);
    CHECK(pwguard_parse_u64("18446744073709551616", &parsed64) != 0);

    uint32_t parsed;
    CHECK(!pwguard_parse_u32("0", &parsed) && parsed == 0);
    CHECK(!pwguard_parse_u32("4294967295", &parsed) && parsed == UINT32_MAX);
    CHECK(pwguard_parse_u32("", &parsed) != 0);
    CHECK(pwguard_parse_u32("-1", &parsed) != 0);
    CHECK(pwguard_parse_u32("+1", &parsed) != 0);
    CHECK(pwguard_parse_u32(" 1", &parsed) != 0);
    CHECK(pwguard_parse_u32("1 ", &parsed) != 0);
    CHECK(pwguard_parse_u32("\t1", &parsed) != 0);
    CHECK(pwguard_parse_u32("1\n", &parsed) != 0);
    CHECK(pwguard_parse_u32("1\r", &parsed) != 0);
    CHECK(pwguard_parse_u32("1\v", &parsed) != 0);
    CHECK(pwguard_parse_u32("1\f", &parsed) != 0);
    CHECK(pwguard_parse_u32("12x", &parsed) != 0);
    CHECK(pwguard_parse_u32("4294967296", &parsed) != 0);
    CHECK(pwguard_parse_pid("") == 0);
    CHECK(pwguard_parse_pid("1") == 1);
    CHECK(pwguard_parse_pid("2147483647") == INT32_MAX);
    CHECK(pwguard_parse_pid("0") == 0);
    CHECK(pwguard_parse_pid("2147483648") == 0);
    CHECK(pwguard_parse_pid("123junk") == 0);
    CHECK(pwguard_parse_nspid("NSpid:\t123\n") == 123);
    CHECK(pwguard_parse_nspid("NSpid: 2147483647\n") == INT32_MAX);
    CHECK(pwguard_parse_nspid("NSpid: 123 45\n") == 0); /* nested PID namespace */
    CHECK(pwguard_parse_nspid("NSpid: 123junk\n") == 0);
    CHECK(pwguard_parse_nspid("NSpid: 123 45junk\n") == 0);
    CHECK(pwguard_parse_nspid("NSpid: 0\n") == 0);
    CHECK(pwguard_parse_nspid("NSpid: 2147483648\n") == 0);
    CHECK(pwguard_parse_nspid("Pid: 123\n") == 0);

    uint32_t socket_inode = 0;
    CHECK(!pwguard_parse_socket_inode("socket:[12345]", &socket_inode) && socket_inode == 12345);
    CHECK(!pwguard_parse_socket_inode("socket:[4294967295]", &socket_inode) && socket_inode == UINT32_MAX);
    CHECK(pwguard_parse_socket_inode("socket:[]", &socket_inode) != 0);
    CHECK(pwguard_parse_socket_inode("socket:[123]junk", &socket_inode) != 0);
    CHECK(pwguard_parse_socket_inode("socket:[123junk]", &socket_inode) != 0);
    CHECK(pwguard_parse_socket_inode("socket:[4294967296]", &socket_inode) != 0);
    CHECK(pwguard_parse_socket_inode("pipe:[123]", &socket_inode) != 0);
    CHECK(pwguard_parse_socket_inode("socket:[123]", NULL) != 0);

    CHECK(pwguard_link_state_active(-2) == 0);       /* ERROR */
    CHECK(pwguard_link_state_active(-1) == 0);       /* UNLINKED */
    CHECK(pwguard_link_state_active(0) == 0);        /* INIT */
    CHECK(pwguard_link_state_active(1) == 0);        /* NEGOTIATING */
    CHECK(pwguard_link_state_active(2) == 0);        /* ALLOCATING */
    CHECK(pwguard_link_state_active(3) == 0);        /* PAUSED */
    CHECK(pwguard_link_state_active(4) == 1);        /* ACTIVE */
    CHECK(pwguard_capture_flag("Audio/Source") == FW_R_NO_MIC);
    CHECK(pwguard_capture_flag("Audio/Source/Virtual") == FW_R_NO_MIC);
    CHECK(pwguard_capture_flag("Audio/Duplex") == FW_R_NO_MIC);
    CHECK(pwguard_capture_flag("Video/Source") == FW_R_NO_CAMERA);
    CHECK(pwguard_capture_flag("Audio/Sink") == 0);                    /* playback stays */
    CHECK(pwguard_capture_flag(NULL) == 0);
    CHECK(pwguard_stream_flag("Stream/Input/Audio") == FW_R_NO_MIC);
    CHECK(pwguard_stream_flag("Stream/Input/Video") == FW_R_NO_CAMERA);
    CHECK(pwguard_stream_flag("Stream/Output/Audio") == 0);
    /* a shared screen or window (lord-screencast) is hidden from no-screen apps, not from no-camera ones */
    CHECK(pwguard_node_flag("Video/Source", "Screen", "lordos-screencast-0") == FW_R_NO_SCREEN);
    CHECK(pwguard_node_flag("Video/Source", NULL, "lordos-screencast-1") == FW_R_NO_SCREEN);
    CHECK(pwguard_node_flag("Video/Source", "Screen", NULL) == FW_R_NO_SCREEN);
    CHECK(pwguard_node_flag("Video/Source", "Camera", "v4l2_input.pci") == FW_R_NO_CAMERA);
    CHECK(pwguard_node_flag("Video/Source", NULL, NULL) == FW_R_NO_CAMERA);
    CHECK(pwguard_node_flag("Audio/Source", "Screen", "lordos-screencast-0") == FW_R_NO_MIC);   /* only a video source is a screen */
    CHECK(pwguard_node_flag("Audio/Sink", NULL, NULL) == 0);
    test_unexpected_fd_count_resets_connection();
    test_permission_sync_completion();
    test_permission_error_sequence_matching();
    test_well_formed_events_keep_connection();
    test_malformed_event_resets_connection(0, 1); /* Core::Done */
    test_malformed_event_resets_connection(0, 3); /* Core::Error */
    test_malformed_event_resets_connection(2, 0); /* Registry::Global */
    test_malformed_event_resets_connection(2, 1); /* Registry::GlobalRemove */
    test_malformed_event_resets_connection(0, 4); /* Core::RemoveId */
    test_registry_global_trailing_bytes_reset_connection();
    test_unknown_info_proxy_resets_connection();
    test_duplicate_tracked_global_resets_connection();
    test_core_event_extra_fields_reset_connection(1); /* Core::Done */
    test_core_event_extra_fields_reset_connection(2); /* Core::Ping */
    test_core_event_extra_fields_reset_connection(3); /* Core::Error */
    test_disconnect_reconnects_and_restarts_protocol();
    printf("pwguard-test: %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
