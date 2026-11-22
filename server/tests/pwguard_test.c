/* pwguard_test.c — the PipeWire guard's pieces that don't need PipeWire (src/sec/pwguard.c): the
 * SPA POD reader on well-formed, truncated and lying input, and which nodes and streams each
 * restriction covers. The guard against a real PipeWire: tests/fwlinux/mic.sh. */
#include "sec/pwguard.h"
#include "fw/rules.h"
#include "sec/broker.h"
#include "sec/perms.h"

#include <stdio.h>
#include <string.h>

void brokersrv_revoke(const char *app, const char *perm) { (void)app; (void)perm; }
PermState perm_get(const char *app_id, Perm p) { (void)app_id; (void)p; return PERM_ASK; }

static int fails, total;
#define CHECK(c) do { total++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #c); } } while (0)

static size_t put(uint8_t *b, size_t o, uint32_t v) { memcpy(b + o, &v, 4); return o + 4; }

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

    uint32_t parsed;
    CHECK(!pwguard_parse_u32("0", &parsed) && parsed == 0);
    CHECK(!pwguard_parse_u32("4294967295", &parsed) && parsed == UINT32_MAX);
    CHECK(pwguard_parse_u32("", &parsed) != 0);
    CHECK(pwguard_parse_u32("-1", &parsed) != 0);
    CHECK(pwguard_parse_u32("+1", &parsed) != 0);
    CHECK(pwguard_parse_u32("12x", &parsed) != 0);
    CHECK(pwguard_parse_u32("4294967296", &parsed) != 0);
    CHECK(pwguard_parse_pid("1") == 1);
    CHECK(pwguard_parse_pid("2147483647") == INT32_MAX);
    CHECK(pwguard_parse_pid("0") == 0);
    CHECK(pwguard_parse_pid("2147483648") == 0);
    CHECK(pwguard_parse_pid("123junk") == 0);

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
    printf("pwguard-test: %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
