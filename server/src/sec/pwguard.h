/* pwguard.h — the firewall's "no microphone" and "no camera" for Linux apps inside PipeWire
 * (docs/FIREWALL.md, "Linux apps").
 *
 * Linux apps talk to PipeWire directly (natively, or through pipewire-pulse), so the kernel's
 * device rules don't cover the microphone (PipeWire opens the sound card, not the app) nor a
 * camera that PipeWire serves. lord-server keeps one connection of its own to the session's
 * PipeWire, on the manager socket ($XDG_RUNTIME_DIR/pipewire-0-manager), with our own
 * implementation of the native protocol (no libpipewire). For every PipeWire client it works out
 * the app (the PID's cgroup → the launcher's scope → desktop:<id>) and, if that app is
 * restricted (sec/restrict.h):
 *   - hides from that client every capture node: Audio/Source* and Audio/Duplex for "no
 *     microphone", Video/Source for "no camera", a shared screen or window (lord-screencast's
 *     Video/Source, media.role Screen) for "no screen capture", and their ports (permission 0, so it
 *     can neither see nor link them), and every other client (so it can't change anyone's permissions);
 *   - destroys every capture stream that client creates (Stream/Input/Audio, Stream/Input/Video),
 *     as soon as it appears.
 * The rules are lord.fw's store (the one Settings edits), read every second: a restriction set or
 * lifted on a running app applies within a second, without restarting it. Nothing is written to a
 * file the apps could change.
 *
 * Who a client is: PipeWire's own pipewire.sec.pid (from the socket's credentials) for native
 * clients. Apps using the PulseAudio API all reach PipeWire through pipewire-pulse's process, so
 * for those the PID is the application.process.id the app itself reported to pipewire-pulse (see
 * the documentation for what that means). */
#ifndef LORD_SEC_PWGUARD_H
#define LORD_SEC_PWGUARD_H

#include <stddef.h>
#include <stdint.h>

int  pwguard_start(void);       /* 0 started/connecting; retried by pwguard_tick if PipeWire isn't up */
int  pwguard_fd(void);          /* -1 if not connected */
int  pwguard_timeout_ms(void); /* -1 connected; otherwise milliseconds until the next retry */
int  pwguard_wants_write(void);
void pwguard_dispatch(void);
void pwguard_tick(void);        /* every pass of the main loop; does work once a second */

/* ---- for tests (tests/pwguard_test.c) ---- */
/* A SPA POD reader over buf: the n-th field of a Struct and its kind. */
typedef struct { const uint8_t *p; size_t n; } PwPod;
int  pwpod_struct_field(PwPod s, int index, PwPod *out);     /* 0 ok */
int  pwpod_int(PwPod p, int32_t *v);
int  pwpod_string(PwPod p, const char **s);
/* Dict lookup: 1 found, 0 absent, -1 malformed or duplicate key. */
int  pwguard_dict_lookup(PwPod d, const char *key, const char **value);
/* Complete a correlated permission batch; -1 means disconnect, 0 applied, 1 reapply. */
int  pwguard_sync_complete(uint32_t pending_flags, uint32_t want,
                           int pending_failed, int pending_dirty,
                           uint32_t *applied);
/* Match Core::Error to a permission update or its Core::Sync barrier (sequence is bit-preserving). */
int  pwguard_error_matches(uint32_t proxy, uint32_t sync_header,
                           const uint32_t *permission_seqs, unsigned count,
                           int32_t resource_id, int32_t request_seq);
/* Strict decimal parsing for PipeWire's string-valued IDs and process IDs (0 = invalid PID). */
int  pwguard_parse_u32(const char *s, uint32_t *value);
int  pwguard_parse_u64(const char *s, uint64_t *value);
uint32_t pwguard_next_sync_token(uint32_t current, const uint32_t *pending, unsigned count);
int  pwguard_parse_pid(const char *s);
/* Parse exactly one positive PID from a /proc/<pid>/status NSpid line. */
int  pwguard_parse_nspid(const char *line);
/* Parse the exact socket:[inode] format from a /proc/<pid>/fd symlink. */
int  pwguard_parse_socket_inode(const char *link, uint32_t *inode);
/* Which restriction makes a node a capture node to hide ("Audio/Source" → FW_R_NO_MIC...), 0 none. */
uint32_t pwguard_capture_flag(const char *media_class);
/* The same for a stream an app creates to capture ("Stream/Input/Audio" → FW_R_NO_MIC). */
uint32_t pwguard_stream_flag(const char *media_class);
/* PipeWire pw_link_state: only ACTIVE (4) means the graph connection is running. */
int pwguard_link_state_active(int state);
/* malloc-owned JSON array of verified applications on active capture links. */
char *pwguard_device_users_json(void);
/* A node's flag from its global's properties: a screen shared with an app (the ScreenCast portal's lord-screencast: a
 * Video/Source whose media.role is Screen, or named lordos-screencast-*) is FW_R_NO_SCREEN, not a camera; else as
 * pwguard_capture_flag. Any app's node that says so is hidden from no-screen apps, which only hides it more. */
uint32_t pwguard_node_flag(const char *media_class, const char *media_role, const char *node_name);

#endif
