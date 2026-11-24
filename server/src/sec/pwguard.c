/* pwguard.c — "no microphone", "no camera" and "no screen capture" for Linux apps inside PipeWire (see pwguard.h).
 *
 * A minimal client of PipeWire's native protocol (version 3): each message is a 16-byte header
 * (object id; opcode << 24 | payload size; sequence; number of fds) and a SPA POD Struct.
 * We only use: core.hello, client.update_properties, core.get_registry, core.pong,
 * registry.bind (clients and links), registry.destroy (a restricted app's capture streams),
 * client.update_permissions and core.sync barriers; and we read core.ping/done/error/remove_id,
 * registry.global/global_remove, client.info and Link::Info. Link state is observational and never changes policy.
 * Everything is non-blocking: lord-server never waits for it. */
#include "pwguard.h"
#include "appid.h"
#include "restrict.h"
#include "../fw/rules.h"

#include <dirent.h>
#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/unix_diag.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 256
#define MAX_NODES   1024
#define MAX_PORTS   4096
#define MAX_LINKS   2048
#define MAX_PROXY   (MAX_CLIENTS + MAX_LINKS + 8)
#define PERM_CHUNK  400
#define PERM_MAX_CHUNKS ((MAX_NODES + MAX_PORTS + MAX_CLIENTS + PERM_CHUNK - 1) / PERM_CHUNK)
#define IN_MAX      (1u << 20)
#define OUT_MAX     (1u << 20)

/* PipeWire's permission bits (pw_permission): read, write, execute, link, metadata */
#define PW_R 0400
#define PW_W 0200
#define PW_X 0100
#define PW_L 0020
#define PW_M 0010
#define PW_ALL (PW_R | PW_W | PW_X | PW_L | PW_M)

#define SPA_INT    4
#define SPA_LONG   5
#define SPA_STRING 8
#define SPA_STRUCT 14

typedef struct {
    uint32_t id, proxy;         /* global id; our bound proxy (0 = not bound yet) */
    int pid_sec, pid_app, pulse;/* pipewire.sec.pid (kernel), application.process.id (app-reported), a client of the real pipewire-pulse */
    int unverified;             /* who it is can't be established: treated as restricted while restricted apps run */
    int daemon;                 /* pipewire-pulse's own connection: the streams of modules a pulse client loaded */
    int resolved;               /* app worked out (needs the client's info) */
    char fpid[64];              /* pipewire.access.portal.app_id: the Flatpak app pipewire-pulse says it is (checked) */
    char app[FW_APP_MAX];
    uint32_t want, applied;     /* restrictions requested / confirmed by Core::Done */
    uint32_t pending_sync;
    uint32_t pending_sync_header;
    uint32_t pending_seq[PERM_MAX_CHUNKS];
    uint32_t pending_flags;
    unsigned pending_count;
    double pending_since;
    int pending, pending_failed, pending_dirty;
    int identity_pid;            /* resolved process whose identity this PipeWire connection was bound to */
    uint64_t identity_start;     /* /proc/<pid>/stat starttime: prevents PID reuse from inheriting identity */
    int identity_failed;         /* ambiguous namespace PID: never retry it as a different host process */
    int live;
} PgClient;

typedef struct { uint32_t id, client, flag, stream, port_overflow; int live; } PgNode; /* port_overflow: ports couldn't be tracked; fail closed for matching clients */
typedef struct { uint32_t id, node; int live; } PgPort;
typedef struct { uint32_t id, proxy, out_node, out_port, in_node, in_port; int state, live, info; } PgLink;

static struct {
    int fd;
    uint8_t in[IN_MAX];
    size_t inlen;
    uint8_t *out;
    size_t outlen;
    uint32_t seq, sync_token;
    PgClient cl[MAX_CLIENTS];
    PgNode node[MAX_NODES];
    PgPort port[MAX_PORTS];
    PgLink link[MAX_LINKS];
    uint8_t proxy_used[MAX_PROXY];
    uint32_t port_overflow;
    double next_try, next_tick;
    int up, connecting;
} P = { .fd = -1 };

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

/* ---- SPA POD: reading ---------------------------------------------------------------------- */

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* PipeWire publishes these identifiers as decimal strings. Reject prefixes, signs,
 * whitespace and overflow instead of accepting atoi()/strtoul() partial parses. */
uint32_t pwguard_next_sync_token(uint32_t current, const uint32_t *pending, unsigned count) {
    uint32_t candidate = current;
    for (unsigned attempt = 0; attempt <= count; attempt++) {
        candidate++;
        if (!candidate || candidate > INT32_MAX) candidate = 1;
        int used = 0;
        for (unsigned i = 0; pending && i < count; i++)
            if (pending[i] == candidate) { used = 1; break; }
        if (!used) return candidate;
    }
    return 0;
}

int pwguard_parse_u64(const char *s, uint64_t *value) {
    if (!s || !*s || !value) return -1;
    uint64_t v = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        uint64_t digit = (uint64_t)(*p - '0');
        if (v > (UINT64_MAX - digit) / 10) return -1;
        v = v * 10 + digit;
    }
    *value = v;
    return 0;
}

int pwguard_parse_u32(const char *s, uint32_t *value) {
    if (!s || !*s || !value) return -1;
    uint32_t v = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        uint32_t digit = (uint32_t)(*p - '0');
        if (v > (UINT32_MAX - digit) / 10) return -1;
        v = v * 10 + digit;
    }
    *value = v;
    return 0;
}

int pwguard_parse_pid(const char *s) {
    uint32_t pid;
    return !pwguard_parse_u32(s, &pid) && pid > 0 && pid <= INT_MAX ? (int)pid : 0;
}

/* /proc/<pid>/status: accept exactly one positive PID after the NSpid label. */
int pwguard_parse_nspid(const char *line) {
    if (!line || strncmp(line, "NSpid:", 6)) return 0;
    const char *s = line + 6;
    while (*s == ' ' || *s == '\t') s++;
    const char *end = s;
    while (*end >= '0' && *end <= '9') end++;
    if (end == s || (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')) return 0;
    char token[32];
    size_t n = (size_t)(end - s);
    if (n >= sizeof token) return 0;
    memcpy(token, s, n);
    token[n] = 0;
    int pid = pwguard_parse_pid(token);
    if (!pid) return 0;
    s = end;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return *s ? 0 : pid;
}

int pwguard_parse_socket_inode(const char *link, uint32_t *inode) {
    static const char prefix[] = "socket:[";
    if (!link || !inode || strncmp(link, prefix, sizeof prefix - 1)) return -1;
    const char *digits = link + sizeof prefix - 1;
    const char *end = strchr(digits, ']');
    if (!end || end[1]) return -1;
    char token[32];
    size_t n = (size_t)(end - digits);
    if (!n || n >= sizeof token) return -1;
    memcpy(token, digits, n);
    token[n] = 0;
    return pwguard_parse_u32(token, inode);
}

static int pwpod_long(PwPod p, int64_t *v) {
    if (p.n < 16 || rd32(p.p + 4) != SPA_LONG || rd32(p.p) != 8) return -1;
    memcpy(v, p.p + 8, 8);
    return 0;
}

/* The pod at p (size + type + body), within n bytes. */
static int pod_at(const uint8_t *p, size_t n, PwPod *out, uint32_t *type) {
    if (n < 8) return -1;
    uint32_t size = rd32(p), t = rd32(p + 4);
    if (size > n - 8) return -1;
    out->p = p;
    out->n = 8 + size;
    if (type) *type = t;
    return 0;
}

int pwpod_struct_field(PwPod s, int index, PwPod *out) {
    uint32_t t;
    PwPod whole;
    if (pod_at(s.p, s.n, &whole, &t) || t != SPA_STRUCT) return -1;
    size_t off = 8, end = whole.n;
    for (int i = 0; off < end; i++) {
        PwPod f;
        if (pod_at(s.p + off, end - off, &f, NULL)) return -1;
        size_t adv = (f.n + 7) & ~(size_t)7;
        if (adv > end - off) return -1;
        if (i == index) { *out = f; return 0; }
        off += adv;
    }
    return -1;
}

int pwpod_int(PwPod p, int32_t *v) {
    if (p.n < 12 || rd32(p.p + 4) != SPA_INT || rd32(p.p) != 4) return -1;
    memcpy(v, p.p + 8, 4);
    return 0;
}

int pwpod_string(PwPod p, const char **s) {
    uint32_t size = p.n >= 8 ? rd32(p.p) : 0;
    if (p.n < 9 || rd32(p.p + 4) != SPA_STRING || !size || size > p.n - 8 ||
        p.p[8 + size - 1] || memchr(p.p + 8, 0, size - 1)) return -1;
    *s = (const char *)p.p + 8;
    return 0;
}

/* A dict is a Struct containing Int n followed by exactly n String key/value pairs.
 * Return 1 when found, 0 when absent, and -1 for malformed or duplicate requested keys.
 * Walk the encoded fields once: repeated pwpod_struct_field() calls would make this O(n²). */
int pwguard_dict_lookup(PwPod d, const char *key, const char **value) {
    PwPod whole, field;
    uint32_t type;
    int32_t count;
    if (!d.p || !key || !value || pod_at(d.p, d.n, &whole, &type) || type != SPA_STRUCT) return -1;
    size_t off = 8, end = whole.n;
    if (pod_at(d.p + off, end - off, &field, NULL) || pwpod_int(field, &count) ||
        count < 0 || count > 4096) return -1;
    size_t adv = (field.n + 7) & ~(size_t)7;
    if (adv > end - off) return -1;
    off += adv;

    const char *found = NULL;
    int matches = 0;
    for (int i = 0; i < count; i++) {
        PwPod k, v;
        const char *ks, *vs;
        if (off > end || pod_at(d.p + off, end - off, &k, NULL) || pwpod_string(k, &ks)) return -1;
        adv = (k.n + 7) & ~(size_t)7;
        if (adv > end - off) return -1;
        off += adv;
        if (off > end || pod_at(d.p + off, end - off, &v, NULL) || pwpod_string(v, &vs)) return -1;
        adv = (v.n + 7) & ~(size_t)7;
        if (adv > end - off) return -1;
        off += adv;
        if (!strcmp(ks, key)) {
            found = vs;
            if (++matches > 1) return -1;
        }
    }
    if (off != end) return -1;
    *value = found;
    return matches;
}

/* ---- SPA POD: writing ---------------------------------------------------------------------- */

typedef struct { uint8_t b[16384]; size_t n; int err; } Pb;

static void pb_raw(Pb *b, const void *p, size_t n) {
    if (b->err || n > sizeof b->b - b->n) { b->err = 1; return; }
    memcpy(b->b + b->n, p, n);
    b->n += n;
}
static void pb_pad(Pb *b) { static const uint8_t z[8] = { 0 }; size_t r = b->n % 8; if (r) pb_raw(b, z, 8 - r); }
static void pb_hdr(Pb *b, uint32_t size, uint32_t type) { pb_raw(b, &size, 4); pb_raw(b, &type, 4); }
static void pb_int(Pb *b, int32_t v) { pb_hdr(b, 4, SPA_INT); pb_raw(b, &v, 4); pb_pad(b); }
static void pb_str(Pb *b, const char *s) { size_t l = strlen(s) + 1; pb_hdr(b, (uint32_t)l, SPA_STRING); pb_raw(b, s, l); pb_pad(b); }
static size_t pb_begin(Pb *b) { size_t at = b->n; pb_hdr(b, 0, SPA_STRUCT); return at; }
static void pb_end(Pb *b, size_t at) { if (b->err) return; uint32_t size = (uint32_t)(b->n - at - 8); memcpy(b->b + at, &size, 4); }

/* ---- connection ---------------------------------------------------------------------------- */

static void drop(void) {
    if (P.fd >= 0) close(P.fd);
    P.fd = -1;
    P.up = 0;
    P.connecting = 0;
    P.next_tick = 0;
    P.inlen = P.outlen = 0;
    memset(P.cl, 0, sizeof P.cl);
    memset(P.node, 0, sizeof P.node);
    memset(P.port, 0, sizeof P.port);
    memset(P.link, 0, sizeof P.link);
    memset(P.proxy_used, 0, sizeof P.proxy_used);
    P.port_overflow = 0;
    P.next_try = now_ms() + 2000;
}

static void flush_out(void) {
    while (P.fd >= 0 && P.outlen) {
        ssize_t w = send(P.fd, P.out, P.outlen, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && errno == EAGAIN) return;
        if (w <= 0) { drop(); return; }
        memmove(P.out, P.out + w, P.outlen - (size_t)w);
        P.outlen -= (size_t)w;
    }
}

static int send_msg_seq(uint32_t id, uint32_t opcode, const Pb *b, uint32_t *sent_seq) {
    if (P.fd < 0) return -1;
    if (b->err) { drop(); return -1; }
    if (!P.out && !(P.out = malloc(OUT_MAX))) { drop(); return -1; }
    if (P.outlen + 16 + b->n > OUT_MAX) { drop(); return -1; }   /* PipeWire isn't reading: start over */
    uint32_t seq = P.seq++;
    uint32_t h[4] = { id, opcode << 24 | (uint32_t)(b->n & 0xffffff), seq, 0 };
    memcpy(P.out + P.outlen, h, 16);
    memcpy(P.out + P.outlen + 16, b->b, b->n);
    P.outlen += 16 + b->n;
    if (sent_seq) *sent_seq = seq;
    return 0;
}
static int send_msg(uint32_t id, uint32_t opcode, const Pb *b) {
    return send_msg_seq(id, opcode, b, NULL);
}

static uint32_t proxy_new(void) {
    for (uint32_t i = 3; i < MAX_PROXY; i++) if (!P.proxy_used[i]) { P.proxy_used[i] = 1; return i; }
    return 0;
}

static void start_protocol(void) {
    P.seq = 0;
    P.sync_token = 0;
    P.proxy_used[0] = P.proxy_used[1] = P.proxy_used[2] = 1;
    Pb b = { .n = 0 };
    size_t s = pb_begin(&b); pb_int(&b, 3); pb_end(&b, s);
    send_msg(0, 1, &b);                                         /* core.hello(3) */
    b.n = 0;
    s = pb_begin(&b);
    size_t d = pb_begin(&b);
    pb_int(&b, 2);
    pb_str(&b, "application.name"); pb_str(&b, "LordOS privacy guard");
    pb_str(&b, "media.category"); pb_str(&b, "Manager");
    pb_end(&b, d);
    pb_end(&b, s);
    send_msg(1, 2, &b);                                         /* client.update_properties */
    b.n = 0;
    s = pb_begin(&b); pb_int(&b, 3); pb_int(&b, 2); pb_end(&b, s);
    send_msg(0, 5, &b);                                         /* core.get_registry(3, new id 2) */
    P.up = P.fd >= 0;
}

int pwguard_start(void) {
    if (P.fd >= 0) return 0;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt || rt[0] != '/') { P.next_try = now_ms() + 2000; return -1; }
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    if ((size_t)snprintf(sa.sun_path, sizeof sa.sun_path, "%s/pipewire-0-manager", rt) >= sizeof sa.sun_path) { P.next_try = now_ms() + 2000; return -1; }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) { P.next_try = now_ms() + 2000; return -1; }
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (rc && errno != EINPROGRESS) { close(fd); P.next_try = now_ms() + 2000; return -1; }
    P.fd = fd;
    P.connecting = rc != 0;
    P.up = 0;
    P.next_tick = 0;
    P.inlen = P.outlen = 0;
    memset(P.cl, 0, sizeof P.cl);
    memset(P.node, 0, sizeof P.node);
    memset(P.port, 0, sizeof P.port);
    memset(P.link, 0, sizeof P.link);
    memset(P.proxy_used, 0, sizeof P.proxy_used);
    if (!P.connecting) {
        start_protocol();
        flush_out();
    }
    return 0;
}

int pwguard_timeout_ms(void) {
    /* A pending connect is already watched for POLLOUT by the main loop. */
    if (P.fd >= 0 && P.connecting) return -1;
    /* The main loop must wake both for a reconnect and for the one-second policy/process
     * revalidation while connected; otherwise an idle desktop can sleep past both. */
    double deadline = P.fd >= 0 ? P.next_tick : P.next_try;
    double left = deadline - now_ms();
    if (left <= 0) return 0;
    if (left >= INT_MAX) return INT_MAX;
    return (int)left + 1; /* round up: never wake early and spin before the deadline */
}

int pwguard_fd(void) { return P.fd; }
int pwguard_wants_write(void) { return P.fd >= 0 && (P.connecting || P.outlen > 0); }

/* ---- the policy ---------------------------------------------------------------------------- */

uint32_t pwguard_capture_flag(const char *mc) {
    if (!mc) return 0;
    if (!strncmp(mc, "Audio/Source", 12) || !strcmp(mc, "Audio/Duplex")) return FW_R_NO_MIC;
    if (!strncmp(mc, "Video/Source", 12)) return FW_R_NO_CAMERA;
    return 0;
}

uint32_t pwguard_node_flag(const char *mc, const char *role, const char *name) {
    if (mc && !strncmp(mc, "Video/Source", 12) && ((role && !strcmp(role, "Screen")) || (name && !strncmp(name, "lordos-screencast-", 18))))
        return FW_R_NO_SCREEN;
    return pwguard_capture_flag(mc);
}

int pwguard_link_state_active(int state) { return state == 4; }


uint32_t pwguard_stream_flag(const char *mc) {
    if (!mc) return 0;
    if (!strcmp(mc, "Stream/Input/Audio")) return FW_R_NO_MIC;
    if (!strcmp(mc, "Stream/Input/Video")) return FW_R_NO_CAMERA;
    return 0;
}

static PgClient *client_by_id(uint32_t id) {
    for (int i = 0; i < MAX_CLIENTS; i++) if (P.cl[i].live && P.cl[i].id == id) return &P.cl[i];
    return NULL;
}

static PgNode *node_by_id(uint32_t id) {
    for (int i = 0; i < MAX_NODES; i++) if (P.node[i].live && P.node[i].id == id) return &P.node[i];
    return NULL;
}

/* Current real capture users: only ACTIVE links from a source node to an app input stream.
 * App identity comes from the client resolver above, never from node/application display strings.
 * Caller owns the returned JSON buffer. */
static void pw_json_string(char *out, size_t cap, size_t *at, const char *s) {
    if (*at + 1 >= cap) return;
    out[(*at)++] = '"';
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p && *at + 7 < cap; p++) {
        if (*p == '"' || *p == '\\') { out[(*at)++] = '\\'; out[(*at)++] = (char)*p; }
        else if (*p < 0x20) { int n = snprintf(out + *at, cap - *at, "\\u%04x", *p); if (n > 0) *at += (size_t)n; }
        else out[(*at)++] = (char)*p;
    }
    if (*at + 1 < cap) out[(*at)++] = '"';
}
char *pwguard_device_users_json(void) {
    size_t cap = (size_t)MAX_LINKS * (FW_APP_MAX * 6u + 128u) + 2u, at = 0;
    char *out = malloc(cap);
    if (!out) return NULL;
    out[at++] = '[';
    int first = 1;
    for (int i = 0; i < MAX_LINKS; i++) {
        PgLink *ln = &P.link[i];
        if (!ln->live || !ln->info || !pwguard_link_state_active(ln->state)) continue;
        PgNode *src = node_by_id(ln->out_node), *dst = node_by_id(ln->in_node);
        if (!src || !dst || !src->flag || !dst->stream) continue;
        if ((src->flag & FW_R_NO_SCREEN) || (dst->stream & FW_R_NO_SCREEN)) continue;
        uint32_t kind = src->flag & (FW_R_NO_MIC | FW_R_NO_CAMERA);
        if (!kind || !(dst->stream & kind)) continue;
        PgClient *c = client_by_id(dst->client);
        if (!c || !c->resolved || c->unverified || c->daemon || !c->app[0]) continue;
        /* One app/device pair is enough even when PipeWire fans a stream across channels. */
        int duplicate = 0;
        for (int k = 0; k < i; k++) {
            PgLink *old = &P.link[k];
            if (!old->live || !old->info || !pwguard_link_state_active(old->state)) continue;
            PgNode *os = node_by_id(old->out_node), *od = node_by_id(old->in_node);
            PgClient *oc = od ? client_by_id(od->client) : NULL;
            if (os && od && (os->flag & kind) && (od->stream & kind) && oc && oc->resolved &&
                !oc->unverified && !oc->daemon && !strcmp(oc->app, c->app)) { duplicate = 1; break; }
        }
        if (duplicate) continue;
        if (!first) out[at++] = ',';
        first = 0;
        out[at++] = '{';
        out[at++] = '"'; memcpy(out + at, "kind", 4); at += 4; out[at++] = '"'; out[at++] = ':';
        pw_json_string(out, cap, &at, kind == FW_R_NO_MIC ? "microphone" : "camera");
        out[at++] = ',';
        out[at++] = '"'; memcpy(out + at, "app", 3); at += 3; out[at++] = '"'; out[at++] = ':';
        pw_json_string(out, cap, &at, c->app);
        out[at++] = ',';
        memcpy(out + at, "\"pid\":", 6); at += 6;
        at += (size_t)snprintf(out + at, cap - at, "%d", c->pulse && c->pid_app > 0 ? c->pid_app : (c->pid_sec > 0 ? c->pid_sec : c->pid_app));
        out[at++] = '}';
    }
    out[at++] = ']';
    out[at] = 0;
    return out;
}

#define GUARDED (FW_R_NO_MIC | FW_R_NO_CAMERA | FW_R_NO_SCREEN)

/* Restricted apps are running (an app scope of this session whose app has no-mic, no-camera or no-screen):
 * only then are clients we can't identify treated as restricted, so normal use without
 * restrictions never pays for it. */
static int guard_armed;

/* What the client's app is restricted to, among what PipeWire hands out. */
static uint32_t client_restrictions(PgClient *c) {
    if (!c->resolved) return 0;
    /* pipewire-pulse's own connection carries the capture streams of modules any pulse client
     * loaded (module-loopback from the microphone into a pipe or a TCP server): while restricted
     * apps run they can't be told apart, so they are closed */
    if (c->daemon) return guard_armed ? GUARDED : 0;
    if (c->unverified) return guard_armed ? GUARDED : 0;
    if (!c->app[0]) return 0;
    uint32_t f = 0;
    if (restrict_blocks(c->app, FW_R_NO_MIC)) f |= FW_R_NO_MIC;
    if (restrict_blocks(c->app, FW_R_NO_CAMERA)) f |= FW_R_NO_CAMERA;
    if (restrict_blocks(c->app, FW_R_NO_SCREEN)) f |= FW_R_NO_SCREEN;   /* the screens Linux apps are shown (ScreenCast) */
    return f;
}

/* Sends the whole permission list of a restricted client (or the restore list when its
 * restriction is lifted): hidden = the capture nodes and ports of what it is restricted to, and
 * every other client. In chunks: PipeWire merges successive update_permissions, so a list longer
 * than one message is never dropped (that would leave the client with every permission). */
static int send_perm_chunk(PgClient *c, const int32_t *pairs, int32_t n, uint32_t *sent_seq) {
    Pb b = { .n = 0 };
    size_t s = pb_begin(&b);
    pb_int(&b, n);
    for (int32_t i = 0; i < n; i++) { pb_int(&b, pairs[2 * i]); pb_int(&b, pairs[2 * i + 1]); }
    pb_end(&b, s);
    if (b.err) { fprintf(stderr, "pwguard: permission chunk too long for %s\n", c->app); drop(); return -1; }
    return send_msg_seq(c->proxy, 4, &b, sent_seq);             /* client.update_permissions */
}

/* A permission update is not considered applied until Core::Done confirms that every
 * preceding UpdatePermissions method has been processed. Core::Error is correlated by the
 * target proxy and the request header sequence. */
static void apply_permissions(PgClient *c, uint32_t flags) {
    if (!c->proxy || !c->live || P.fd < 0) return;
    if (c->pending) { c->pending_dirty = 1; return; }

    int32_t pairs[2 * PERM_CHUNK];
    int32_t n = 0;
    c->pending_count = 0;
    c->pending_failed = 0;
    c->pending_dirty = 0;
    c->pending_flags = flags;
    c->pending = 1;
    c->pending_since = now_ms();
#define PERM_ADD(id_, perm_) do { \
        pairs[2 * n] = (int32_t)(id_); pairs[2 * n + 1] = (int32_t)(perm_); \
        if (++n == PERM_CHUNK) { \
            uint32_t sent = 0; \
            if (send_perm_chunk(c, pairs, n, &sent)) { c->pending = 0; return; } \
            if (c->pending_count >= PERM_MAX_CHUNKS) { drop(); return; } \
            c->pending_seq[c->pending_count++] = sent; n = 0; \
            if (P.fd < 0) return; \
        } \
    } while (0)
    uint32_t perm_clients = flags ? 0 : PW_ALL;
    for (int i = 0; i < MAX_NODES; i++) {
        PgNode *nd = &P.node[i];
        if (!nd->live || !nd->flag) continue;
        uint32_t want = (flags & nd->flag) ? 0 : PW_ALL;
        PERM_ADD(nd->id, want);
        for (int k = 0; k < MAX_PORTS; k++)
            if (P.port[k].live && P.port[k].node == nd->id) PERM_ADD(P.port[k].id, want);
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PgClient *o = &P.cl[i];
        if (!o->live || o == c) continue;
        PERM_ADD(o->id, perm_clients);
    }
#undef PERM_ADD
    if (n) {
        uint32_t sent = 0;
        if (send_perm_chunk(c, pairs, n, &sent)) { c->pending = 0; return; }
        if (c->pending_count >= PERM_MAX_CHUNKS) { drop(); return; }
        c->pending_seq[c->pending_count++] = sent;
    }

    /* Core::Sync is a barrier: Done echoes this unique token after earlier methods are handled. */
    Pb sync = { .n = 0 };
    size_t at = pb_begin(&sync);
    uint32_t active_tokens[MAX_CLIENTS];
    unsigned active_count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (&P.cl[i] != c && P.cl[i].live && P.cl[i].pending)
            active_tokens[active_count++] = P.cl[i].pending_sync;
    uint32_t token = pwguard_next_sync_token(P.sync_token, active_tokens, active_count);
    if (!token) { drop(); return; }
    P.sync_token = token;
    pb_int(&sync, 0);
    pb_int(&sync, (int32_t)token);
    pb_end(&sync, at);
    c->pending_sync = token;
    if (send_msg_seq(0, 2, &sync, &c->pending_sync_header)) {
        c->pending = 0;
        return;
    }
}

/* registry.destroy of a global (a stream, a node or a client we must not leave running) */
static void destroy_global(uint32_t id) {
    Pb b = { .n = 0 };
    size_t s = pb_begin(&b); pb_int(&b, (int32_t)id); pb_end(&b, s);
    send_msg(2, 2, &b);
}

/* Destroys the capture streams of a restricted client (the app gets its stream closed). */
static void kill_streams(void) {
    for (int i = 0; i < MAX_NODES; i++) {
        PgNode *nd = &P.node[i];
        if (!nd->live || !nd->stream) continue;
        PgClient *c = client_by_id(nd->client);
        /* an owner we don't track (no client slot) counts as restricted while restricted apps run */
        if (c ? !(c->want & nd->stream) : !guard_armed) continue;
        destroy_global(nd->id);
        fprintf(stderr, "pwguard: %s is restricted (%s): its capture stream %u was closed\n", c ? (c->daemon ? "pipewire-pulse (a loaded module)" : c->app) : "an untracked client",
                nd->stream == FW_R_NO_MIC ? "no-mic" : "no-camera", nd->id);
        nd->stream = 0;                                         /* once; the global_remove follows */
    }
}

static void evaluate(PgClient *c) {
    c->want = client_restrictions(c);
    /* If a capture node has more ports than we can track, don't leave a matching client with
     * partial visibility. Disconnect it rather than risk a port bypass. */
    if (c->want & P.port_overflow) {
        fprintf(stderr, "pwguard: capture-port tracking overflow: disconnecting restricted client %s\n", c->app[0] ? c->app : "(unresolved)");
        destroy_global(c->id);
        c->live = 0;
        return;
    }
    if (c->daemon) return;              /* only its module streams are closed: it serves every pulse client */
    if (c->want != c->applied) apply_permissions(c, c->want);
}

/* ---- who a client really is ---------------------------------------------------------------- */

/* Is pid running the root-owned executable at path (same inode, not a copy)? */
static uint64_t process_start_time(int pid);

static int runs_exe(int pid, const char *path) {
    char p[64];
    struct stat st, want;
    uint64_t start = process_start_time(pid);
    if (!start) return 0;
    snprintf(p, sizeof p, "/proc/%d/exe", pid);
    if (stat(p, &st) || stat(path, &want) || want.st_uid != 0 || (want.st_mode & 022)) return 0;
    if (st.st_dev != want.st_dev || st.st_ino != want.st_ino) return 0;
    snprintf(p, sizeof p, "/proc/%d", pid);
    if (stat(p, &st) || st.st_uid != getuid()) return 0;
    /* Do not let a PID reused during the executable/owner checks inherit this identity. */
    return process_start_time(pid) == start;
}

/* Read a cgroup.procs file only when it contains exactly one complete valid PID. */
static int cgroup_single_pid(const char *path) {
    FILE *f = fopen(path, "re");
    if (!f) return 0;
    char line[64];
    int pid = 0, lines = 0, invalid = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n') { invalid = 1; break; }
        line[strcspn(line, "\r\n")] = 0;
        int value = pwguard_parse_pid(line);
        if (!value || ++lines != 1) { invalid = 1; break; }
        pid = value;
    }
    if (ferror(f)) invalid = 1;
    fclose(f);
    return !invalid && lines == 1 ? pid : 0;
}

/* Is pid exactly in the user manager's session.slice/<unit>, alone? The whole path is compared:
 * the user owns the user@ subtree and can make a nested cgroup that merely ends in the same names. */
static int in_user_unit(int pid, const char *unit) {
    char p[64], line[1024], want[512];
    unsigned u = (unsigned)getuid();
    snprintf(want, sizeof want, "0::/user.slice/user-%u.slice/user@%u.service/session.slice/%s\n", u, u, unit);
    snprintf(p, sizeof p, "/proc/%d/cgroup", pid);
    FILE *f = fopen(p, "re");
    int ok = 0;
    while (f && fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n') break;
        if (!strncmp(line, "0::", 3)) { ok = !strcmp(line, want); break; }
    }
    if (f) { if (ferror(f)) ok = 0; fclose(f); }
    if (!ok) return 0;
    /* A second process moved into the unit makes neither process a trusted daemon. */
    snprintf(want, sizeof want, "/sys/fs/cgroup/user.slice/user-%u.slice/user@%u.service/session.slice/%s/cgroup.procs", u, u, unit);
    return cgroup_single_pid(want) == pid;
}

/* Is pid the session's real pipewire-pulse? Never from a property: the executable is the root-owned
 * /usr/bin/pipewire (pipewire-pulse is a link to it), the process is ours, and it runs in the user
 * manager's pipewire-pulse.service. */
static int is_pulse_daemon(int pid) {
    return runs_exe(pid, "/usr/bin/pipewire") && in_user_unit(pid, "pipewire-pulse.service");
}

/* The unix sockets of the system and their peers (sock_diag), to see who is really connected to
 * pipewire-pulse. Returns the number of pairs written into ino/peer. */
static int unix_peers(uint32_t *ino, uint32_t *peer, int max) {
    if (!ino || !peer || max <= 0) return -1;
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
    if (fd < 0) return -1;
    struct sockaddr_nl local = { .nl_family = AF_NETLINK };
    if (bind(fd, (struct sockaddr *)&local, sizeof local) != 0) { close(fd); return -1; }
    struct { struct nlmsghdr nh; struct unix_diag_req r; } req;
    memset(&req, 0, sizeof req);
    req.nh.nlmsg_len = sizeof req;
    req.nh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nh.nlmsg_seq = 1;
    req.r.sdiag_family = AF_UNIX;
    req.r.udiag_states = ~0u;
    req.r.udiag_show = UDIAG_SHOW_PEER;
    if (send(fd, &req, sizeof req, 0) != (ssize_t)sizeof req) { close(fd); return -1; }
    static uint8_t buf[32768];
    int n = 0, done = 0, failed = 0;
    while (!done && !failed) {
        struct sockaddr_nl source;
        struct iovec iov = { .iov_base = buf, .iov_len = sizeof buf };
        struct msghdr msg;
        memset(&source, 0, sizeof source);
        memset(&msg, 0, sizeof msg);
        msg.msg_name = &source;
        msg.msg_namelen = sizeof source;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        ssize_t r;
        do { r = recvmsg(fd, &msg, MSG_TRUNC); } while (r < 0 && errno == EINTR);
        if (r <= 0 || (size_t)r > sizeof buf ||
            (msg.msg_flags & MSG_TRUNC) || msg.msg_namelen < sizeof source ||
            source.nl_family != AF_NETLINK || source.nl_pid != 0 || source.nl_groups != 0) {
            failed = 1;
            break;
        }
        int rem = (int)r;
        for (struct nlmsghdr *h = (struct nlmsghdr *)buf; NLMSG_OK(h, rem); h = NLMSG_NEXT(h, rem)) {
            /* Accept only replies to this request from the kernel's Netlink endpoint. */
            if (h->nlmsg_seq != req.nh.nlmsg_seq || h->nlmsg_pid != 0) { failed = 1; break; }
            /* An interrupted kernel dump is not a complete snapshot, even if it ends in DONE. */
            if (h->nlmsg_flags & NLM_F_DUMP_INTR) { failed = 1; break; }
            if (h->nlmsg_type == NLMSG_DONE) {
                size_t payload = NLMSG_PAYLOAD(h, 0);
                if (payload != 0 && payload != sizeof(int)) { failed = 1; break; }
                if (payload == sizeof(int)) {
                    int dump_error;
                    memcpy(&dump_error, NLMSG_DATA(h), sizeof dump_error);
                    if (dump_error) { failed = 1; break; }
                }
                done = 1;
                /* The current message must be the final aligned message in this datagram. */
                if (rem != (int)NLMSG_ALIGN(h->nlmsg_len)) failed = 1;
                break;
            }
            if (h->nlmsg_type == NLMSG_ERROR) {
                if (h->nlmsg_len != NLMSG_LENGTH(sizeof(struct nlmsgerr))) { failed = 1; break; }
                const struct nlmsgerr *e = NLMSG_DATA(h);
                if (e->error) { failed = 1; break; }
                continue; /* a successful ACK is not the end of a multipart dump */
            }
            if (h->nlmsg_type != SOCK_DIAG_BY_FAMILY ||
                h->nlmsg_len < NLMSG_LENGTH(sizeof(struct unix_diag_msg))) { failed = 1; break; }
            struct unix_diag_msg *m = NLMSG_DATA(h);
            if (m->udiag_family != AF_UNIX) { failed = 1; break; }
            int len = (int)h->nlmsg_len - (int)NLMSG_LENGTH(sizeof *m);
            int peer_seen = 0;
            for (struct rtattr *a = (struct rtattr *)(m + 1); RTA_OK(a, len); a = RTA_NEXT(a, len)) {
                if (a->rta_type != UNIX_DIAG_PEER) continue;
                if (peer_seen++ || RTA_PAYLOAD(a) != sizeof(uint32_t)) { failed = 1; break; }
                uint32_t peer_ino;
                memcpy(&peer_ino, RTA_DATA(a), sizeof peer_ino);
                /* A socket inode must have one unambiguous peer in this snapshot. */
                int duplicate = 0;
                for (int i = 0; i < n; i++) {
                    if (ino[i] != m->udiag_ino) continue;
                    if (peer[i] != peer_ino) { failed = 1; break; }
                    duplicate = 1;
                }
                if (failed) break;
                if (duplicate) continue;
                if (n >= max) { failed = 1; break; }
                ino[n] = m->udiag_ino;
                peer[n] = peer_ino;
                n++;
            }
            if (failed || len != 0) { failed = 1; break; }
        }
        if (rem != 0 && !done) failed = 1; /* malformed/truncated netlink datagram */
    }
    close(fd);
    return done && !failed ? n : -1;
}

static uint64_t process_start_time(int pid);

/* How many distinct connections to pipewire-pulse (process pp) process pid holds; -1 if unknown.
 * The server ends are pipewire-pulse's (always in our network namespace); their peers are the client
 * ends, which pid holds even when it lives in a network namespace of its own (a Flatpak app with no
 * network), where sock_diag doesn't list them. */
static int pulse_connections(int pp, int pid) {
    /* Pin both processes across the /proc and netlink snapshot: PID reuse or exit during
     * inspection must not turn a partial snapshot into evidence of identity. */
    uint64_t pp_start = process_start_time(pp), pid_start = process_start_time(pid);
    if (!pp_start || !pid_start) return -1;
    static uint32_t ino[65536], peer[65536];
    int np = unix_peers(ino, peer, 65536);
    if (np < 0) return -1;
    char d[64], l[320], t[64];
    static uint32_t clients[4096], seen[4096];
    int nc = 0;
    for (int pass = 0; pass < 2; pass++) {
        int owner = pass ? pid : pp;
        int pn = snprintf(d, sizeof d, "/proc/%d/fd", owner);
        if (pn < 0 || (size_t)pn >= sizeof d) return -1;
        DIR *dir = opendir(d);
        if (!dir) return -1;
        struct dirent *e;
        int count = 0, nseen = 0, failed = 0;
        for (;;) {
            /* Other syscalls in the loop may leave errno set; isolate readdir's EOF
             * from an actual directory-read error instead of inspecting stale errno. */
            errno = 0;
            e = readdir(dir);
            if (!e) {
                if (errno) failed = 1;
                break;
            }
            if (e->d_name[0] == '.') continue;
            int ln = snprintf(l, sizeof l, "%s/%s", d, e->d_name);
            if (ln < 0 || (size_t)ln >= sizeof l) { failed = 1; break; }
            ssize_t k = readlink(l, t, sizeof t - 1);
            if (k < 0 || (size_t)k >= sizeof t - 1) { failed = 1; break; }
            uint32_t v;
            t[k] = 0;
            if (pwguard_parse_socket_inode(t, &v)) continue;
            if (!pass) {
                /* a socket of pipewire-pulse: its peer is a client's end.
                 * Count socket inodes, not FDs: dup()/fork() may expose one connection
                 * through several descriptors, but it is still only one connection. */
                for (int i = 0; i < np; i++) if (ino[i] == v) {
                    uint32_t p = peer[i];
                    if (!p) break;
                    int duplicate = 0;
                    for (int j = 0; j < nc; j++) if (clients[j] == p) { duplicate = 1; break; }
                    if (!duplicate) {
                        if (nc == 4096) { closedir(dir); return -1; }
                        clients[nc++] = p;
                    }
                    break;
                }
                continue;
            }
            for (int i = 0; i < nc; i++) if (clients[i] == v) {
                int duplicate = 0;
                for (int j = 0; j < nseen; j++) if (seen[j] == v) { duplicate = 1; break; }
                if (!duplicate) {
                    seen[nseen++] = v;
                    count++;
                }
                break;
            }
        }
        if (closedir(dir)) failed = 1;
        if (failed) return -1;
        if (process_start_time(owner) != (pass ? pid_start : pp_start)) return -1;
        if (pass) {
            if (process_start_time(pp) != pp_start || process_start_time(pid) != pid_start) return -1;
            return count;
        }
    }
    return -1;
}

/* Parse the whitespace-separated NSpid values without accepting numeric prefixes or overflow. */
static int inner_pid(int pid) {
    char p[64], line[256];
    snprintf(p, sizeof p, "/proc/%d/status", pid);
    FILE *f = fopen(p, "re");
    if (!f) return -1;
    int in = -1, found = 0, invalid = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n') { invalid = 1; break; }
        if (strncmp(line, "NSpid:", 6)) continue;
        if (found++) { invalid = 1; break; }
        char *s = line + 6;
        int last = -1, count = 0;
        while (*s) {
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
            if (!*s) break;
            char *end = s;
            while (*end >= '0' && *end <= '9') end++;
            if (end == s || (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')) {
                last = -1; count = 0; break;
            }
            char saved = *end;
            *end = 0;
            int value = pwguard_parse_pid(s);
            *end = saved;
            if (!value) { last = -1; count = 0; break; }
            last = value;
            count++;
            s = end;
        }
        in = count ? last : -1;
        if (in <= 0) invalid = 1;
    }
    if (ferror(f)) invalid = 1;
    fclose(f);
    return !invalid && found == 1 ? in : -1;
}

/* Re-read the authoritative scope membership after process/socket inspection. */
static int cgroup_has_pid(const char *path, int pid) {
    FILE *f = fopen(path, "re");
    if (!f) return 0;
    char line[64];
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n') {
            found = 0;
            break;
        }
        line[strcspn(line, "\r\n")] = 0;
        if (pwguard_parse_pid(line) == pid) { found = 1; break; }
    }
    if (ferror(f)) found = 0;
    fclose(f);
    return found;
}

/* A Flatpak app's pulse client reports its pid inside the app's sandbox, and pipewire-pulse says which app
 * it is (pipewire.access.portal.app_id, from the connecting process's .flatpak-info). Neither is taken
 * on its word: the pid outside is a process of that app's own root-owned scope whose innermost pid is
 * the one reported and which holds at least as many connections to pipewire-pulse as there are clients
 * claiming that app and pid. Returns it, or 0. */
static int flatpak_host_pid(const PgClient *c, int claims) {
    size_t n = strnlen(c->fpid, sizeof c->fpid);
    if (!n || n > 59 || c->pid_app <= 0) return 0;
    for (size_t i = 0; i < n; i++) {
        char ch = c->fpid[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')) return 0;
    }
    if (c->fpid[0] == '.' || strstr(c->fpid, "..")) return 0;
    char path[192];
    snprintf(path, sizeof path, "/sys/fs/cgroup/system.slice/lordos.service/lordos-session-%u/app-lordos-fp-%s.scope/cgroup.procs",
             (unsigned)getuid(), c->fpid);
    FILE *f = fopen(path, "re");
    if (!f) return 0;
    int found = 0;
    char line[64];
    while (!found && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        int pid = pwguard_parse_pid(line);
        if (pid <= 0) continue;
        /* Keep the candidate's identity stable while namespace and socket evidence
         * are gathered: a cgroup PID may disappear and be reused mid-validation. */
        uint64_t start = process_start_time(pid);
        if (!start || inner_pid(pid) != c->pid_app) continue;
        int connections = pulse_connections(c->pid_sec, pid);
        if (connections < claims || process_start_time(pid) != start ||
            !cgroup_has_pid(path, pid)) continue;
        found = pid;
    }
    fclose(f);
    return found;
}

/* The pid as the process itself sees it is pid: it lives in our PID namespace. A process in another
 * one (a no-files app, a container) reports its inner pid, so a claim of its outer pid can only be
 * someone else's. */
static int same_pidns(int pid) {
    char p[64], line[256];
    snprintf(p, sizeof p, "/proc/%d/status", pid);
    FILE *f = fopen(p, "re");
    if (!f) return 0;
    int ok = 0, found = 0, invalid = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n') { invalid = 1; break; }
        if (strncmp(line, "NSpid:", 6)) continue;
        if (found++) { invalid = 1; break; }
        ok = pwguard_parse_nspid(line) == pid;
    }
    if (ferror(f)) invalid = 1;
    fclose(f);
    return !invalid && found == 1 && ok;
}

/* A process the user's service manager runs (systemd-run --user, D-Bus activation): outside every
 * app scope, so an app could escape its restriction there. The sound services themselves don't count. */
static int in_user_manager(int pid) {
    char p[64], line[1024], prefix[128];
    snprintf(p, sizeof p, "/proc/%d/cgroup", pid);
    FILE *f = fopen(p, "re");
    if (!f) return 1; /* Cannot prove the process is outside the user manager. */
    unsigned u = (unsigned)getuid();
    snprintf(prefix, sizeof prefix, "0::/user.slice/user-%u.slice/user@%u.service/", u, u);
    int found = 0, match = 0, invalid = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n') { invalid = 1; break; }
        if (strncmp(line, "0::", 3)) continue;
        if (found++) { invalid = 1; break; }
        /* Require the canonical user-manager subtree, not a substring in an unrelated path. */
        match = !strncmp(line, prefix, strlen(prefix)) && n > strlen(prefix) + 1 && line[n - 1] == '\n';
    }
    if (ferror(f)) invalid = 1;
    fclose(f);
    if (invalid || found != 1) return 1; /* Incomplete cgroup evidence is untrusted. */
    if (!match) return 0;
    /* the sound services: their exact unit AND their root-owned executable */
    if (runs_exe(pid, "/usr/bin/pipewire") && (in_user_unit(pid, "pipewire.service") || in_user_unit(pid, "pipewire-pulse.service"))) return 0;
    if (runs_exe(pid, "/usr/bin/wireplumber") && in_user_unit(pid, "wireplumber.service")) return 0;
    return 1;
}

/* Linux /proc/<pid>/stat field 22. The command name is parenthesized and may itself
 * contain spaces or ')', so parse from the final ')' rather than splitting the whole line. */
static uint64_t process_start_time(int pid) {
    if (pid <= 0) return 0;
    char path[64], line[4096];
    snprintf(path, sizeof path, "/proc/%d/stat", pid);
    FILE *f = fopen(path, "re");
    if (!f) return 0;
    int ok = fgets(line, sizeof line, f) != NULL;
    size_t n = ok ? strlen(line) : 0;
    if (!ok || ferror(f) || (n == sizeof line - 1 && line[n - 1] != '\n')) ok = 0;
    fclose(f);
    if (!ok) return 0;
    char *p = strrchr(line, ')');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    /* field 3 (state) */
    while (*p && *p != ' ' && *p != '\t') p++;
    for (int field = 4; field <= 22; field++) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) return 0;
        char *end = p;
        while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') end++;
        if (end == p) return 0;
        if (field == 22) {
            char saved = *end;
            *end = 0;
            uint64_t value = 0;
            int valid = pwguard_parse_u64(p, &value) == 0;
            *end = saved;
            return valid ? value : 0;
        }
        p = end;
    }
    return 0;
}

static void resolve(PgClient *c) {
    c->app[0] = 0;
    c->unverified = 0;
    c->resolved = 1;
    if (c->identity_failed) { c->unverified = 1; return; }
    /* A PID is not a durable process identity. If the process that originally established this
     * client has exited, never let a later process reusing its PID inherit the connection's app. */
    if (c->identity_pid > 0 && (!c->identity_start || process_start_time(c->identity_pid) != c->identity_start)) {
        c->unverified = 1;
        return;
    }
    int pid = c->pid_sec;                           /* the socket's credentials: always the start */
    /* Verify PulseAudio clients even when no restriction is armed: this identity also feeds the
     * microphone privacy indicator, which must never trust application.process.id on its own. */
    if (c->pulse) {
        /* a pulse client of the real pipewire-pulse: the app's own pid is what it reported, so it
         * counts only if that process really holds as many connections to pipewire-pulse as
         * there are clients claiming it */
        int claims = 0;
        int fp = 0;
        if (c->fpid[0] && c->pid_app > 0) {
            /* a Flatpak app's: its pid inside the sandbox, found again in that app's scope. Tried first: that small
             * pid is usually also some other process's out here, so "same PID namespace" can't tell them apart */
            for (int i = 0; i < MAX_CLIENTS; i++)
                if (P.cl[i].live && P.cl[i].pulse && P.cl[i].pid_app == c->pid_app && !strcmp(P.cl[i].fpid, c->fpid)) claims++;
            fp = flatpak_host_pid(c, claims);
            if (fp) pid = fp;
            else if (!same_pidns(c->pid_app)) {
                /* No host PID could be tied to this sandbox PID. Do not retry it later after PID reuse. */
                c->identity_failed = 1;
                c->unverified = 1;
                return;
            }
            claims = 0;
        }
        if (!fp) {
            for (int i = 0; i < MAX_CLIENTS; i++) if (P.cl[i].live && P.cl[i].pulse && P.cl[i].pid_app == c->pid_app) claims++;
            if (c->pid_app > 0 && !same_pidns(c->pid_app)) {
                c->identity_failed = 1;
                c->unverified = 1;
                return;
            }
            int real = c->pid_app > 0 ? pulse_connections(c->pid_sec, c->pid_app) : -1;
            if (real < claims) { c->unverified = 1; return; }
            pid = c->pid_app;
        }
    }
    if (pid <= 0) { c->unverified = 1; return; }
    uint64_t start = process_start_time(pid);
    if (!start) {
        /* Latch an unreadable/dead PID too: it must not become a different process on a later retry. */
        if (!c->identity_pid) c->identity_pid = pid;
        c->identity_start = 0;
        c->unverified = 1;
        return;
    }
    if (c->identity_pid > 0) {
        if (c->identity_pid != pid || c->identity_start != start) { c->unverified = 1; return; }
    } else {
        c->identity_pid = pid;
        c->identity_start = start;
    }
    int r = appid_of_pid((unsigned)pid, c->app, sizeof c->app);
    if (r < 0) { c->unverified = 1; c->app[0] = 0; }  /* gone or unreadable */

    else if (r > 0) {                                /* not in any app's scope */
        c->app[0] = 0;
        /* escaped to the user's service manager while restricted apps run: treated as restricted
         * (like lord-netperm's per-user union) */
        if (guard_armed && in_user_manager(pid)) c->unverified = 1;
    }
    /* appid_of_pid() and the manager/cgroup checks above perform additional /proc reads.
     * Do not publish their result if the PID stopped referring to the pinned process meanwhile. */
    if (process_start_time(pid) != start) {
        c->app[0] = 0;
        c->unverified = 1;
    }
}

static int guarded(const char *app, void *u) {
    (void)u;
    return restrict_blocks(app, FW_R_NO_MIC) || restrict_blocks(app, FW_R_NO_CAMERA) || restrict_blocks(app, FW_R_NO_SCREEN);
}

/* Re-arms the guard: is any restricted app of this session running? (its scope is there) */
static void arm(void) { guard_armed = appid_running(guarded, NULL); }

/* ---- events -------------------------------------------------------------------------------- */

static void on_global(PwPod body) {
    PwPod f;
    int32_t id;
    const char *type;
    if (pwpod_struct_field(body, 0, &f) || pwpod_int(f, &id) || pwpod_struct_field(body, 2, &f) || pwpod_string(f, &type)) return;
    PwPod props;
    int has_props = !pwpod_struct_field(body, 4, &props);
    if (!strcmp(type, "PipeWire:Interface:Client")) {
        int32_t version = 1;
        if (pwpod_struct_field(body, 3, &f) || pwpod_int(f, &version) || version < 1) {
            fprintf(stderr, "pwguard: invalid Client interface version; disconnecting client %d\n", (int)id);
            destroy_global((uint32_t)id);
            return;
        }
        if (version > 3) version = 3;
        PgClient *c = NULL;
        for (int i = 0; i < MAX_CLIENTS && !c; i++) if (!P.cl[i].live) c = &P.cl[i];
        uint32_t px = c ? proxy_new() : 0;
        if (!c || !px) {
            /* fail closed: a client we can't follow could record without its restriction */
            fprintf(stderr, "pwguard: too many PipeWire clients: client %d was disconnected\n", (int)id);
            destroy_global((uint32_t)id);
            return;
        }
        memset(c, 0, sizeof *c);
        c->live = 1;
        c->id = (uint32_t)id;
        {
            Pb b = { .n = 0 };
            size_t s = pb_begin(&b); pb_int(&b, id); pb_str(&b, "PipeWire:Interface:Client"); pb_int(&b, version); pb_int(&b, (int32_t)px); pb_end(&b, s);
            send_msg(2, 1, &b);                                 /* registry.bind → client.info follows */
            c->proxy = px;
        }
        /* Resolve identity asynchronously. Until then, fail closed while any app is restricted:
         * the client must not get a brief unrestricted window before client.info arrives. */
        arm();
        if (guard_armed) {
            c->want = GUARDED;
            if (c->want & P.port_overflow) {
                fprintf(stderr, "pwguard: capture-port tracking overflow: disconnecting unresolved client\n");
                destroy_global(c->id);
                c->live = 0;
                return;
            }
            apply_permissions(c, c->want);
            if (P.fd < 0) return;
        }
        /* the others already restricted must not see this one either */
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (P.cl[i].live && &P.cl[i] != c && P.cl[i].want) apply_permissions(&P.cl[i], P.cl[i].want);
    } else if (!strcmp(type, "PipeWire:Interface:Node")) {
        const char *mc = NULL, *cid = NULL, *role = NULL, *name = NULL;
        if (!has_props ||
            pwguard_dict_lookup(props, "media.class", &mc) < 0 ||
            pwguard_dict_lookup(props, "client.id", &cid) < 0 ||
            pwguard_dict_lookup(props, "media.role", &role) < 0 ||
            pwguard_dict_lookup(props, "node.name", &name) < 0) {
            /* An ambiguous class/owner could conceal a capture node. */
            if (guard_armed) {
                fprintf(stderr, "pwguard: node %d has invalid or ambiguous properties; destroying it\n", (int)id);
                destroy_global((uint32_t)id);
            }
            return;
        }
        uint32_t flag = pwguard_node_flag(mc, role, name), stream = pwguard_stream_flag(mc);
        if (!flag && !stream) return;
        PgNode *nd = NULL;
        for (int i = 0; i < MAX_NODES && !nd; i++) if (!P.node[i].live) nd = &P.node[i];
        if (!nd) {
            /* fail closed: a capture stream, or a capture node of a client that is restricted or
             * not tracked, that we can't follow is destroyed */
            uint32_t client_id = 0;
            PgClient *oc = !pwguard_parse_u32(cid, &client_id) ? client_by_id(client_id) : NULL;
            if (guard_armed && (stream || !oc || oc->want)) {
                fprintf(stderr, "pwguard: too many capture nodes: node %d was destroyed\n", (int)id);
                destroy_global((uint32_t)id);
            }
            return;
        }
        nd->live = 1;
        nd->id = (uint32_t)id;
        nd->flag = flag;
        nd->stream = stream;
        nd->client = 0;
        (void)pwguard_parse_u32(cid, &nd->client);
        /* A stream can be announced before its owner's client.info response. Recheck the
         * running restrictions and temporarily restrict that unresolved client before kill_streams. */
        if (stream) {
            arm();
            PgClient *owner = client_by_id(nd->client);
            if (guard_armed && owner && !owner->resolved && !(owner->want & stream)) {
                owner->want = GUARDED;
                apply_permissions(owner, owner->want);
                if (P.fd < 0) return;
            }
        }
        if (flag)                                               /* a new microphone or camera: hidden from the restricted at once */
            for (int i = 0; i < MAX_CLIENTS; i++) if (P.cl[i].live && (P.cl[i].want & flag)) apply_permissions(&P.cl[i], P.cl[i].want);
        if (stream) kill_streams();
    } else if (!strcmp(type, "PipeWire:Interface:Link")) {
        PgLink *ln = NULL;
        for (int i = 0; i < MAX_LINKS && !ln; i++) if (!P.link[i].live) ln = &P.link[i];
        uint32_t px = ln ? proxy_new() : 0;
        if (!ln || !px) {
            /* Link tracking is observational: never destroy a user's media connection because
             * the privacy guard ran out of bookkeeping slots. */
            fprintf(stderr, "pwguard: link tracking capacity reached for link %d\n", (int)id);
            return;
        }
        memset(ln, 0, sizeof *ln);
        ln->live = 1;
        ln->id = (uint32_t)id;
        ln->proxy = px;
        int32_t version = 1;
        if (!pwpod_struct_field(body, 3, &f)) pwpod_int(f, &version);
        if (version < 1) version = 1;
        if (version > 3) version = 3;
        Pb b = { .n = 0 };
        size_t z = pb_begin(&b);
        pb_int(&b, id); pb_str(&b, "PipeWire:Interface:Link"); pb_int(&b, version); pb_int(&b, (int32_t)px);
        pb_end(&b, z);
        send_msg(2, 1, &b);                                 /* registry.bind → Link::Info */
    } else if (!strcmp(type, "PipeWire:Interface:Port")) {
        const char *nid = NULL;
        if (!has_props || pwguard_dict_lookup(props, "node.id", &nid) < 0 || !nid) {
            /* Without an unambiguous parent, this port might belong to a restricted capture node. */
            if (guard_armed) {
                fprintf(stderr, "pwguard: port %d has invalid or ambiguous parent; destroying it\n", (int)id);
                destroy_global((uint32_t)id);
            }
            return;
        }
        uint32_t node;
        if (pwguard_parse_u32(nid, &node)) {
            if (guard_armed) destroy_global((uint32_t)id);
            return;
        }
        PgNode *nd = node_by_id(node);
        if (!nd || !nd->flag) return;
        PgPort *pt = NULL;
        for (int i = 0; i < MAX_PORTS && !pt; i++) if (!P.port[i].live) pt = &P.port[i];
        if (!pt) {
            /* A missing port permission could bypass the node's restriction. Mark the affected
             * restriction and disconnect current/future matching clients until this node vanishes. */
            nd->port_overflow = nd->flag;
            P.port_overflow |= nd->flag;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                PgClient *cl = &P.cl[i];
                if (!cl->live || !(cl->want & nd->flag)) continue;
                fprintf(stderr, "pwguard: capture-port tracking overflow: disconnecting restricted client %s\n", cl->app[0] ? cl->app : "(unresolved)");
                destroy_global(cl->id);
                cl->live = 0;
            }
            return;
        }
        pt->live = 1;
        pt->id = (uint32_t)id;
        pt->node = node;
        for (int i = 0; i < MAX_CLIENTS; i++) if (P.cl[i].live && (P.cl[i].want & nd->flag)) apply_permissions(&P.cl[i], P.cl[i].want);
    }
}

static void on_global_remove(uint32_t id) {
    for (int i = 0; i < MAX_CLIENTS; i++) if (P.cl[i].live && P.cl[i].id == id) P.cl[i].live = 0;   /* its proxy: remove_id */
    int removed_node = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (P.node[i].live && P.node[i].id == id) {
            P.node[i].live = 0;
            removed_node = 1;
        }
    }
    for (int i = 0; i < MAX_PORTS; i++) if (P.port[i].live && P.port[i].id == id) P.port[i].live = 0;
    if (removed_node) {
        P.port_overflow = 0;
        for (int i = 0; i < MAX_NODES; i++)
            if (P.node[i].live) P.port_overflow |= P.node[i].port_overflow;
    }
    for (int i = 0; i < MAX_LINKS; i++) {
        PgLink *ln = &P.link[i];
        if (!ln->live) continue;
        if (ln->id == id || (removed_node && (ln->out_node == id || ln->in_node == id)))
            ln->live = 0;                 /* don't let a stale link bind to a later reused node ID */
    }
}

/* A malformed Client::Info must never leave a previously trusted identity active.
 * Re-resolve only after a later complete properties update; until then this client is
 * treated as unverified whenever the privacy guard is armed. */
static void client_info_unverified(PgClient *c) {
    if (!c) return;
    c->pid_sec = c->pid_app = c->pulse = c->daemon = 0;
    c->fpid[0] = c->app[0] = 0;
    c->unverified = c->resolved = 1;
    arm();
    evaluate(c);
    kill_streams();
}

/* Used by the Info handlers to validate the complete incoming Struct. */
static int pod_struct_count(PwPod body);

/* client.info(id, change_mask, props): who it is */
static void on_client_info(uint32_t proxy, PwPod body) {
    PgClient *c = NULL;
    for (int i = 0; i < MAX_CLIENTS && !c; i++) if (P.cl[i].live && P.cl[i].proxy == proxy) c = &P.cl[i];
    if (!c) return;
    PwPod f, props;
    int32_t id;
    int64_t change_mask;
    int count = pod_struct_count(body);
    if ((count != 2 && count != 3) ||
        pwpod_struct_field(body, 0, &f) || pwpod_int(f, &id) ||
        (uint32_t)id != c->id || pwpod_struct_field(body, 1, &f) ||
        pwpod_long(f, &change_mask) ||
        (!!(change_mask & 1) != (count == 3))) {
        client_info_unverified(c);
        return;
    }
    /* Client::Info carries the complete property dictionary only when bit 0 is set.
     * Validate the whole Struct before retaining the previous identity or reading props. */
    if (!(change_mask & 1)) return;
    const char *sp = NULL, *ap = NULL, *fp = NULL, *pulse_type = NULL;
    if (pwpod_struct_field(body, 2, &props) ||
        pwguard_dict_lookup(props, "pipewire.sec.pid", &sp) < 0 ||
        pwguard_dict_lookup(props, "application.process.id", &ap) < 0 ||
        pwguard_dict_lookup(props, "pipewire.access.portal.app_id", &fp) < 0 ||
        pwguard_dict_lookup(props, "pulse.server.type", &pulse_type) < 0) {
        /* Never retain or fall back from an ambiguous property set: it could relabel a live client. */
        client_info_unverified(c);
        return;
    }
    c->pid_sec = pwguard_parse_pid(sp);
    c->pid_app = pwguard_parse_pid(ap);
    snprintf(c->fpid, sizeof c->fpid, "%s", fp && strlen(fp) < sizeof c->fpid ? fp : "");
    /* a pulse client only if the connection really comes from the session's pipewire-pulse; the
     * pulse.server.type property alone is the client's word */
    int pulse_conn = c->pid_sec > 0 && is_pulse_daemon(c->pid_sec);
    c->pulse = pulse_type != NULL && pulse_conn;
    c->daemon = pulse_conn && !c->pulse;
    arm();
    resolve(c);
    evaluate(c);
    kill_streams();
}

static void on_link_info(uint32_t proxy, PwPod body) {
    PgLink *ln = NULL;
    for (int i = 0; i < MAX_LINKS && !ln; i++) if (P.link[i].live && P.link[i].proxy == proxy) ln = &P.link[i];
    if (!ln) return;

    /* Parse into temporaries: never publish a partly updated graph edge. */
    PwPod f;
    int32_t id, out_node, out_port, in_node, in_port, state = ln->state;
    int64_t mask;
    int count = pod_struct_count(body);
    if (count < 6 ||
        pwpod_struct_field(body, 0, &f) || pwpod_int(f, &id) ||
        id < 0 || (uint32_t)id != ln->id ||
        pwpod_struct_field(body, 1, &f) || pwpod_int(f, &out_node) || out_node < 0 ||
        pwpod_struct_field(body, 2, &f) || pwpod_int(f, &out_port) || out_port < 0 ||
        pwpod_struct_field(body, 3, &f) || pwpod_int(f, &in_node) || in_node < 0 ||
        pwpod_struct_field(body, 4, &f) || pwpod_int(f, &in_port) || in_port < 0 ||
        pwpod_struct_field(body, 5, &f) || pwpod_long(f, &mask) ||
        ((mask & 1) && count < 7)) {
        fprintf(stderr, "pwguard: malformed PipeWire Link::Info; resetting connection\n");
        drop();
        return;
    }
    if (mask & 1) {
        if (pwpod_struct_field(body, 6, &f) || pwpod_int(f, &state)) {
            fprintf(stderr, "pwguard: malformed PipeWire Link::Info state; resetting connection\n");
            drop();
            return;
        }
    }
    ln->out_node = (uint32_t)out_node;
    ln->out_port = (uint32_t)out_port;
    ln->in_node = (uint32_t)in_node;
    ln->in_port = (uint32_t)in_port;
    if (mask & 1) ln->state = state;
    ln->info = 1;
}

/* Complete one permission batch only after its matching Core::Done barrier. */
int pwguard_sync_complete(uint32_t pending_flags, uint32_t want,
                          int pending_failed, int pending_dirty,
                          uint32_t *applied) {
    if (pending_failed) return -1;
    if (!applied) return -1;
    *applied = pending_flags;
    return pending_dirty || want != pending_flags;
}

int pwguard_error_matches(uint32_t proxy, uint32_t sync_header,
                           const uint32_t *permission_seqs, unsigned count,
                           int32_t resource_id, int32_t request_seq) {
    if (resource_id == 0 && (uint32_t)request_seq == sync_header) return 1;
    if (resource_id != (int32_t)proxy || !permission_seqs) return 0;
    for (unsigned i = 0; i < count; i++)
        if (permission_seqs[i] == (uint32_t)request_seq) return 1;
    return 0;
}

/* Count a complete Struct without accepting truncated children or trailing bytes. */
static int pod_struct_count(PwPod body) {
    PwPod whole, field;
    uint32_t type;
    if (!body.p || pod_at(body.p, body.n, &whole, &type) || whole.n != body.n || type != SPA_STRUCT) return -1;
    size_t off = 8;
    int count = 0;
    while (off < whole.n) {
        if (pod_at(body.p + off, whole.n - off, &field, NULL)) return -1;
        size_t adv = (field.n + 7) & ~(size_t)7;
        if (adv > whole.n - off) return -1;
        off += adv;
        if (++count > 16) return -1;
    }
    return off == whole.n ? count : -1;
}

/* Registry::Global has four mandatory fields and an optional properties Struct.
 * A malformed event invalidates the registry snapshot: reconnect and enumerate again. */
static int registry_global_valid(PwPod body) {
    PwPod f, props;
    int32_t id, permissions, version;
    const char *type, *unused = NULL;
    int count = pod_struct_count(body);
    if ((count != 4 && count != 5) ||
        pwpod_struct_field(body, 0, &f) || pwpod_int(f, &id) || id < 0 ||
        pwpod_struct_field(body, 1, &f) || pwpod_int(f, &permissions) ||
        pwpod_struct_field(body, 2, &f) || pwpod_string(f, &type) ||
        pwpod_struct_field(body, 3, &f) || pwpod_int(f, &version) || version < 0)
        return 0;
    (void)permissions;
    (void)type;
    if (count == 5) {
        if (pwpod_struct_field(body, 4, &props) ||
            pwguard_dict_lookup(props, "__lordos_validation_only__", &unused) < 0)
            return 0;
    }
    return 1;
}

static int registry_global_remove_valid(PwPod body) {
    PwPod f;
    int32_t id;
    return pod_struct_count(body) == 1 &&
           !pwpod_struct_field(body, 0, &f) && !pwpod_int(f, &id) && id >= 0;
}

/* Registry global IDs are unique for the lifetime of one registry snapshot. */
static int tracked_global_id(uint32_t id) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (P.cl[i].live && P.cl[i].id == id) return 1;
    for (int i = 0; i < MAX_NODES; i++)
        if (P.node[i].live && P.node[i].id == id) return 1;
    for (int i = 0; i < MAX_PORTS; i++)
        if (P.port[i].live && P.port[i].id == id) return 1;
    for (int i = 0; i < MAX_LINKS; i++)
        if (P.link[i].live && P.link[i].id == id) return 1;
    return 0;
}

static void on_message(uint32_t id, uint32_t opcode, PwPod body) {
    PwPod f;
    int32_t a, b;
    if (id == 0 && opcode == 2) {                               /* core.ping(id, seq) → pong */
        if (pod_struct_count(body) != 2 ||
            pwpod_struct_field(body, 0, &f) || pwpod_int(f, &a) ||
            pwpod_struct_field(body, 1, &f) || pwpod_int(f, &b)) {
            fprintf(stderr, "pwguard: malformed PipeWire Core::Ping; resetting connection\n");
            drop();
            return;
        }
        Pb p = { .n = 0 };
        size_t s = pb_begin(&p); pb_int(&p, a); pb_int(&p, b); pb_end(&p, s);
        send_msg(0, 3, &p);
    } else if (id == 0 && opcode == 1) {                        /* core.done(id, seq) */
        int32_t done_id, done_seq;
        if (pod_struct_count(body) != 2 ||
            pwpod_struct_field(body, 0, &f) || pwpod_int(f, &done_id) ||
            pwpod_struct_field(body, 1, &f) || pwpod_int(f, &done_seq)) {
            fprintf(stderr, "pwguard: malformed PipeWire Core::Done; resetting connection\n");
            drop();
            return;
        }
        if (done_id != 0) return;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            PgClient *c = &P.cl[i];
            if (!c->live || !c->pending || c->pending_sync != (uint32_t)done_seq) continue;
            int reapply = pwguard_sync_complete(c->pending_flags, c->want,
                                                   c->pending_failed, c->pending_dirty,
                                                   &c->applied);
            c->pending = 0;
            c->pending_count = 0;
            if (reapply < 0) {
                fprintf(stderr, "pwguard: permission update failed; disconnecting client %u (%s)\n",
                        c->id, c->app[0] ? c->app : "unresolved");
                destroy_global(c->id);
                c->live = 0;
                break;
            }
            if (reapply) apply_permissions(c, c->want);
            break;
        }
    } else if (id == 0 && opcode == 3) {                        /* core.error */
        int32_t resource_id = -1, request_seq = 0, result = 0;
        int have_request_seq = 0;
        const char *msg = "";
        if (pod_struct_count(body) != 4 ||
            pwpod_struct_field(body, 0, &f) || pwpod_int(f, &resource_id) ||
            pwpod_struct_field(body, 1, &f) || pwpod_int(f, &request_seq) ||
            pwpod_struct_field(body, 2, &f) || pwpod_int(f, &result) ||
            pwpod_struct_field(body, 3, &f) || pwpod_string(f, &msg)) {
            fprintf(stderr, "pwguard: malformed PipeWire Core::Error; resetting connection\n");
            drop();
            return;
        }
        have_request_seq = 1;
        fprintf(stderr, "pwguard: PipeWire error (object %d, seq %d, result %d): %s\n",
                resource_id, request_seq, result, msg);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            PgClient *c = &P.cl[i];
            if (!c->live || !c->pending) continue;
            if (have_request_seq &&
                pwguard_error_matches(c->proxy, c->pending_sync_header,
                                      c->pending_seq, c->pending_count,
                                      resource_id, request_seq))
                c->pending_failed = 1;
        }
    } else if (id == 0 && opcode == 4) {                        /* core.remove_id */
        if (pod_struct_count(body) != 1 ||
            pwpod_struct_field(body, 0, &f) || pwpod_int(f, &a) ||
            a <= 2 || a >= MAX_PROXY || !P.proxy_used[a]) {
            fprintf(stderr, "pwguard: malformed or unknown PipeWire Core::RemoveId; resetting connection\n");
            drop();
            return;
        }
        P.proxy_used[a] = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (P.cl[i].live && P.cl[i].proxy == (uint32_t)a) P.cl[i].live = 0;
        for (int i = 0; i < MAX_LINKS; i++)
            if (P.link[i].live && P.link[i].proxy == (uint32_t)a) P.link[i].live = 0;
    } else if (id == 2 && opcode == 0) {
        if (!registry_global_valid(body)) {
            fprintf(stderr, "pwguard: malformed PipeWire Registry::Global; resetting connection\n");
            drop();
            return;
        }
        if (pwpod_struct_field(body, 0, &f) || pwpod_int(f, &a) || a < 0 ||
            tracked_global_id((uint32_t)a)) {
            fprintf(stderr, "pwguard: duplicate or invalid tracked PipeWire global; resetting connection\n");
            drop();
            return;
        }
        on_global(body);
    } else if (id == 2 && opcode == 1) {
        if (!registry_global_remove_valid(body)) {
            fprintf(stderr, "pwguard: malformed PipeWire Registry::GlobalRemove; resetting connection\n");
            drop();
            return;
        }
        if (!pwpod_struct_field(body, 0, &f) && !pwpod_int(f, &a)) on_global_remove((uint32_t)a);
    } else if (id > 2 && opcode == 0) {
        int client = 0, link = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (P.cl[i].live && P.cl[i].proxy == id) { client = 1; break; }
        for (int i = 0; i < MAX_LINKS && !link; i++)
            if (P.link[i].live && P.link[i].proxy == id) link = 1;
        if (client == link) {
            /* Exactly one tracked interface must own every bound proxy ID. */
            fprintf(stderr, "pwguard: PipeWire Info arrived for an unknown or ambiguous proxy %u; resetting connection\\n", id);
            drop();
            return;
        }
        if (client) on_client_info(id, body);
        else on_link_info(id, body);
    }
}

void pwguard_dispatch(void) {
    if (P.fd < 0) return;
    if (P.connecting) {
        int error = 0;
        socklen_t n = sizeof error;
        if (getsockopt(P.fd, SOL_SOCKET, SO_ERROR, &error, &n) || error) { drop(); return; }
        P.connecting = 0;
        P.next_tick = now_ms(); /* start policy reconciliation as soon as the protocol is up */
        start_protocol();
        if (P.fd < 0) return;
    }
    flush_out();
    for (;;) {
        if (P.fd < 0) return;
        char cbuf[CMSG_SPACE(28 * sizeof(int))];
        struct iovec io = { P.in + P.inlen, sizeof P.in - P.inlen };
        struct msghdr mh = { .msg_iov = &io, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof cbuf };
        ssize_t n = recvmsg(P.fd, &mh, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN) break;
        if (n <= 0) { drop(); return; }
        int received_fds = 0;
        for (struct cmsghdr *cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm))
            if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS && cm->cmsg_len >= CMSG_LEN(0)) {
                int *fds = (int *)CMSG_DATA(cm);
                size_t k = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                received_fds |= k != 0;
                for (size_t i = 0; i < k; i++) close(fds[i]);
            }
        /* This client never consumes FDs. Reject both unexpected descriptors and a
         * truncated ancillary buffer instead of silently desynchronizing protocol state. */
        if ((mh.msg_flags & MSG_CTRUNC) || received_fds) { drop(); return; }
        P.inlen += (size_t)n;
        size_t off = 0;
        while (P.inlen - off >= 16) {
            uint32_t id = rd32(P.in + off), op = rd32(P.in + off + 4);
            uint32_t size = op & 0xffffff, opcode = op >> 24;
            uint32_t n_fds = rd32(P.in + off + 12);
            if (size > IN_MAX - 16 || n_fds != 0) { drop(); return; }
            if (P.inlen - off < 16 + (size_t)size) break;
            PwPod body = { P.in + off + 16, size };
            on_message(id, opcode, body);
            if (P.fd < 0) return;
            off += 16 + size;
        }
        memmove(P.in, P.in + off, P.inlen - off);
        P.inlen -= off;
        if (P.inlen == sizeof P.in) { drop(); return; }
    }
    flush_out();
}

void pwguard_tick(void) {
    double t = now_ms();
    if (P.fd >= 0 && P.connecting) return; /* POLLOUT completes the connect in dispatch() */
    if (P.fd < 0) {
        if (t >= P.next_try) pwguard_start();
        return;
    }
    if (t < P.next_tick) return;
    P.next_tick = t + 1000;
    /* the rules or the running apps may have changed: apply or lift; who the pulse clients are is
     * checked again (their claims depend on who else is connected) */
    arm();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PgClient *c = &P.cl[i];
        if (!c->live) continue;
        if (c->pending && t - c->pending_since > 5000.0) {
            fprintf(stderr, "pwguard: permission confirmation timed out; disconnecting client %u (%s)\n",
                    c->id, c->app[0] ? c->app : "unresolved");
            destroy_global(c->id);
            c->live = 0;
            continue;
        }
        if (!c->resolved) continue;
        /* Re-read every client's cgroup, not just PulseAudio/unverified clients: a native app may
         * migrate into user@ after its first identification. resolve() also pins PID starttime. */
        resolve(c);
        evaluate(c);
    }
    kill_streams();
    flush_out();
}
