// gxr_alvr.c - Galaxy XR perception -> ALVR "UniExpr" sender (root).
//
// Output uses the ALVR VRCFT wire format that the VRCFT-ALVR-UE module
// understands:
//   UDP to <pc>:41463, one datagram = one record:
//     "UniExpr\0" + 88 float UE weights + 11-float eye tail   (99 floats = 396 B)
//
// Destination: pass --pc <ip> for a fixed unicast target, or omit it to
// auto-connect. In auto mode the sender computes the active interface's
// directed broadcast address (from the Wi-Fi IPv4 netmask) and sends there.
// The stock VRCFT-ALVR module binds 0.0.0.0:41463, so it receives it with no
// PC-side change and no configuration. The address is re-resolved while idle,
// so a network change (new subnet) recovers on its own. If no broadcast
// address can be resolved yet (Wi-Fi not up at boot), the sender waits and
// retries instead of failing.
//
// The 68->88 mapping (Android-XR blendshapes -> VRCFT Unified Expressions),
// the un-clamped jaw, openness, pupil and true per-eye gaze are all computed
// here, from read-only host uprobes:
//
//   face 72: 0x3589f8   (68 Android-XR blendshapes, x1=src x2=0x110)
//   jaw  :   0xf09aa4   (pre-clamp jaw + tongue_out/up/down)
//   eyedir:  0x4be378   (per-eye raw gaze direction, x0=vec x21=eye)
//   pupil:   0x4bef60   (pupil ellipse, 6 floats/detection)
//
// Build: tools/build_alvr.sh
// Usage: gxr_alvr [--pc <pc-ip>] [--port 41463] [--verbose]
//                 [--gaze-swap 0|1] [--gaze-invert-x 0|1] [--gaze-invert-y 0|1]
//                 [--selftest]
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>

#define TRACE "/sys/kernel/tracing"
#define EYE_PLUGIN "/vendor/lib64/libperception_eye_tracking_host_plugin.so"
#define FACE_PLUGIN "/vendor/lib64/libperception_face_parameter_tracking_host_plugin.so"
#define DEFAULT_PUPIL_OFFSET   0x4bef60UL
#define DEFAULT_EYEDIR_OFFSET  0x4be378UL
#define DEFAULT_FACE_BS_OFFSET 0x3589f8UL
#define DEFAULT_JAW_OFFSET     0xf09aa4UL

#define UE_MAX 88
#define TAIL_N 11
#define PAYLOAD_N (UE_MAX + TAIL_N)   /* 99 */
#define FACE_N 68
#define MAX_DET 16

/* VRCFT UnifiedExpressions indices (order == UnifiedExpressions.cs) */
enum { UE_EyeSquintRight = 0, UE_EyeSquintLeft = 1, UE_EyeWideRight = 2, UE_EyeWideLeft = 3,
       UE_JawOpen = 22, UE_MouthClosed = 29,
       UE_LipSuckUpperRight = 30, UE_LipSuckUpperLeft = 31,
       UE_MouthUpperRight = 52, UE_MouthUpperLeft = 53,
       UE_TongueOut = 72, UE_TongueUp = 73, UE_TongueDown = 74,
       UE_TongueRight = 75, UE_TongueLeft = 76 };

/* Face68 (Android XR) indices used for the eye tail. Openness uses the
   EyeSquint weight (UE_EyeSquint{Left,Right}) as the lid tightener, exactly as
   RootedLinkFTModule.CalculateEyeOpenness does, so the LidTightener face
   indices (28/29) are not read directly here. */
#define F_EYES_CLOSED_L 12
#define F_EYES_CLOSED_R 13

/* EyeTail slot order == UnifiedExprTracking.EyeTail in the module */
enum { T_Version = 0, T_LOpen, T_ROpen, T_LPupil, T_RPupil, T_MinDil, T_MaxDil,
       T_LPitch, T_LYaw, T_RPitch, T_RYaw };

/* 68 -> UE map (66 pairs). Source of truth:
     RootedLinkFTModule.cs -> private static readonly (int Face, int Ue)[] Face68Map
   UE indices are enum values from VRCFaceTracking.Core/Params/Expressions/
   UnifiedExpressions.cs. Keep this table in sync with that map. */
static const struct { int face; int ue; } FACE68_TO_UE[] = {
    {0, 5}, {0, 7}, {1, 4}, {1, 6}, {2, 19}, {3, 18}, {4, 17}, {5, 16},
    {6, 21}, {7, 20}, {8, 67}, {9, 66}, {10, 65}, {11, 64},
    {22, 9}, {23, 8}, {24, 22}, {25, 24}, {26, 23}, {27, 25},
    {28, 1}, {29, 0}, {30, 61}, {31, 60}, {32, 57}, {32, 59}, {33, 56}, {33, 58},
    {34, 39}, {35, 37}, {36, 38}, {37, 36}, {38, 69}, {39, 68},
    {40, 43}, {40, 41}, {41, 42}, {41, 40}, {42, 63}, {43, 62},
    {44, 33}, {45, 31}, {46, 32}, {47, 30}, {48, 71}, {49, 70}, {50, 29},
    {51, 51}, {52, 50}, {53, 55}, {53, 53}, {54, 54}, {54, 52},
    {55, 49}, {56, 48}, {57, 11}, {58, 10}, {59, 3}, {60, 2}, {61, 45}, {62, 44},
    {63, 72}, {64, 76}, {65, 75}, {66, 73}, {67, 74},
};
#define NTAB ((int)(sizeof(FACE68_TO_UE)/sizeof(FACE68_TO_UE[0])))

/* --- tuning (match RootedLinkFT defaults; overridable from the CLI) --- */
static float PUPIL_MM_SCALE = 24.0f;      /* ellipse radius -> mm (module default) */
static float PUPIL_DILATION_GAIN = 0.75f; /* widens the min/max window by 1/gain */
#define PUPIL_WINDOW_MIN_MM 0.05f  /* floor for the tracked min/max span */
#define PUPIL_GAIN_MIN 0.05f       /* floor on the dilation gain divisor */

/* A host frame counts as "live" only if it arrived within this window. */
#define FRESH_MS 500
/* poll() timeout == the self-heal / re-resolve cadence when idle. */
#define SOCK_POLL_MS 3000
/* Single-instance lock (flock); released automatically when we exit. */
#define LOCK_PATH "/data/local/tmp/gxr_alvr.lock"

/* Un-clamped jaw constants (ported 1:1 from RootedLinkFTModule.UpdateJaw). */
#define JAW_HIST 1024              /* ring buffer of rest-baseline samples */
#define JAW_MIN_SAMPLES 60         /* don't trust the baseline below this */
#define JAW_NEUTRAL_PERIOD_MS 500  /* re-learn the 5th-percentile baseline at most this often */
#define JAW_CLAMP_ACTIVE 0.15f     /* tongue DOF above this => vendor clamp is driving JawOpen */
#define JAW_NEUTRAL_MIN 0.05f
#define JAW_NEUTRAL_MAX 0.40f
#define JAW_BAND_LO 0.40f          /* shipped JawOpen in this band is likely clamped */
#define JAW_BAND_HI 0.56f
#define JAW_DENOM_FLOOR 0.2f

/* Lip-suck compensation exponent (1/6), from RootedLinkFT.UpdateFaceTracking. */
#define LIP_SUCK_EXP (1.0f / 6.0f)

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }

/* If set, print the 1 Hz gaze trace to stderr (default: quiet). */
static int g_verbose = 0;

/* Gaze mapping knobs. Defaults (0/0/0) are the verified-correct mapping:
   yaw -> Gaze.x, pitch(up+) -> Gaze.y. See gaze_map(). Set from the CLI only;
   the old /data/local/tmp/gxr_alvr.conf is no longer polled. */
static int g_swap = 0, g_inv_x = 0, g_inv_y = 0;

/* config kept for self-heal re-arming */
static unsigned long g_pupil_off, g_eyedir_off, g_face_off, g_jaw_off;
static const char *g_eye_plugin, *g_face_plugin;

static long long now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
static long long now_ms(void) { return now_us() / 1000; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---------------- tracefs ---------------- */
static int write_file(const char *path, const char *data) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    size_t n = strlen(data);
    ssize_t w = write(fd, data, n);
    close(fd);
    return (w == (ssize_t)n) ? 0 : -1;
}
static void tracefs_cleanup(void) {
    /* O_TRUNC clear + disable: a bare "\n" write does not clear here. */
    write_file(TRACE "/tracing_on", "0\n");
    char dirpath[64];
    snprintf(dirpath, sizeof(dirpath), "%s/events/uprobes", TRACE);
    DIR *d = opendir(dirpath);
    if (d) {
        struct dirent *e; char en[400];
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            snprintf(en, sizeof(en), "%s/%s/enable", dirpath, e->d_name);
            write_file(en, "0\n");
        }
        closedir(d);
    }
    int fd = open(TRACE "/uprobe_events", O_WRONLY | O_TRUNC);
    if (fd >= 0) close(fd);
}
static int remove_event(const char *name) {
    char spec[128];
    snprintf(spec, sizeof(spec), "-:uprobes/%s\n", name);
    int fd = open(TRACE "/uprobe_events", O_WRONLY | O_APPEND);
    if (fd < 0) return -1;
    ssize_t w = write(fd, spec, strlen(spec));
    close(fd);
    return (w == (ssize_t)strlen(spec)) ? 0 : -1;
}
static int create_event(const char *name, const char *spec) {
    if (write_file(TRACE "/uprobe_events", spec) != 0) {
        remove_event(name);
        if (write_file(TRACE "/uprobe_events", spec) != 0) {
            fprintf(stderr, "gxr_alvr: create %s failed: %s\n", name, strerror(errno));
            return -1;
        }
    }
    char en[128];
    snprintf(en, sizeof(en), "%s/events/uprobes/%s/enable", TRACE, name);
    if (write_file(en, "1\n") != 0) {
        fprintf(stderr, "gxr_alvr: enable %s failed: %s\n", name, strerror(errno));
        return -1;
    }
    return 0;
}
static int arm_pupil(const char *plugin, unsigned long off) {
    char spec[512];
    snprintf(spec, sizeof(spec),
             "p:pten %s:0x%lx idx=%%x20 v0=+0x0(%%x10):u32 v1=+0x4(%%x10):u32 "
             "v2=+0x8(%%x10):u32 v3=+0xc(%%x10):u32 v4=+0x10(%%x10):u32 v5=+0x14(%%x10):u32\n",
             plugin, off);
    return create_event("pten", spec);
}
static int arm_eyedir(const char *plugin, unsigned long off) {
    char spec[512];
    snprintf(spec, sizeof(spec),
             "p:dir %s:0x%lx e=%%x21 x=+0x0(%%x0):u32 y=+0x4(%%x0):u32 z=+0x8(%%x0):u32\n",
             plugin, off);
    return create_event("dir", spec);
}
static int arm_face(const char *plugin, unsigned long off) {
    char spec[4096];
    size_t used = (size_t)snprintf(spec, sizeof(spec), "p:bs %s:0x%lx ", plugin, off);
    for (int i = 0; i < FACE_N && used + 32 < sizeof(spec); i++) {
        int w = snprintf(spec + used, sizeof(spec) - used, "f%d=+%d(%%x1):u32 ", i, i * 4);
        if (w < 0) break;
        used += (size_t)w;
    }
    if (used > 0) {
        if (spec[used - 1] == ' ') spec[used - 1] = '\n';
        else if (used + 1 < sizeof(spec)) { spec[used] = '\n'; spec[used + 1] = '\0'; }
    }
    return create_event("bs", spec);
}
static int arm_jaw(const char *plugin, unsigned long off) {
    char spec[512];
    snprintf(spec, sizeof(spec),
             "p:jaw %s:0x%lx jaw=+0x60(%%x21):u32 tout=+0xfc(%%x21):u32 "
             "tup=+0x108(%%x21):u32 tdn=+0x10c(%%x21):u32\n",
             plugin, off);
    return create_event("jaw", spec);
}
static void disarm_all(void) {
    write_file(TRACE "/tracing_on", "0\n");
    write_file(TRACE "/events/uprobes/pten/enable", "0\n");
    write_file(TRACE "/events/uprobes/dir/enable", "0\n");
    write_file(TRACE "/events/uprobes/bs/enable", "0\n");
    write_file(TRACE "/events/uprobes/jaw/enable", "0\n");
    int fd = open(TRACE "/uprobe_events", O_WRONLY | O_TRUNC);
    if (fd >= 0) close(fd);
}

/* Single-instance guard. An flock on LOCK_PATH makes the first start win and a
   concurrent second start exit cleanly, so two relays can never stack up and
   fight over tracefs (the old /proc scan could race and target the wrong pid).
   The lock is released by the kernel when we exit, so a crash never wedges it.
   The module/dev scripts still pkill before starting when a restart is wanted
   (that path frees the lock, then the new process acquires it). Returns 0 on
   success, -1 if another live instance holds the lock. */
static int acquire_lock(void) {
    int fd = open(LOCK_PATH, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        fprintf(stderr, "gxr_alvr: lock %s: %s (continuing unlocked)\n", LOCK_PATH, strerror(errno));
        return 0;   /* fail soft: not being able to lock must not stop the relay */
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "gxr_alvr: another instance is already running (lock %s held)\n", LOCK_PATH);
        close(fd);
        return -1;
    }
    return fd;      /* keep the fd open for the process lifetime */
}

/* Gaze mapping: VRCFT's Gaze.x is HORIZONTAL, Gaze.y VERTICAL. Device frame is
   +x right, +y DOWN, +z forward, so yaw = atan2(x,z) (+ = look right) and
   pitch = atan2(-yDown,z) (+ = look up). Defaults (swap=0, no inversion) send
   yaw->Gaze.x, pitch->Gaze.y, which is the verified-correct mapping. */
static void gaze_map(const float d[3], float *gx, float *gy) {
    float yaw = atan2f(d[0], d[2]);      /* + = look right */
    float pitch = atan2f(-d[1], d[2]);   /* + = look up (device +y is down) */
    float x = g_swap ? pitch : yaw;
    float y = g_swap ? yaw : pitch;
    if (g_inv_x) x = -x;
    if (g_inv_y) y = -y;
    *gx = x; *gy = y;
}

/* ---- self-heal: something else (a capture script / another tool) may clear
   the tracefs event set or drop tracing_on. Re-arm so the live link survives. */
static int event_present(const char *name) {
    int fd = open(TRACE "/uprobe_events", O_RDONLY);
    if (fd < 0) return 0;
    static char b[16384];
    ssize_t n = read(fd, b, sizeof(b) - 1);
    close(fd);
    if (n <= 0) return 0;
    b[n] = 0;
    char key[64];
    snprintf(key, sizeof(key), "uprobes/%s ", name);
    return strstr(b, key) != NULL;
}
static void re_arm(void) {
    tracefs_cleanup();
    write_file(TRACE "/buffer_size_kb", "16384\n");
    arm_pupil(g_eye_plugin, g_pupil_off);
    arm_eyedir(g_eye_plugin, g_eyedir_off);
    arm_face(g_face_plugin, g_face_off);
    arm_jaw(g_face_plugin, g_jaw_off);
    write_file(TRACE "/tracing_on", "1\n");
    fprintf(stderr, "gxr_alvr: re-armed (tracefs had been changed under us)\n");
}
static int tracing_enabled(void) {
    char c = 0;
    int f = open(TRACE "/tracing_on", O_RDONLY);
    if (f >= 0) { if (read(f, &c, 1) < 0) c = 0; close(f); }
    return c == '1';
}
static void health_check(void) {
    /* If any of our events vanished (something truncated uprobe_events),
       re-create the whole set. Otherwise, if only tracing_on was toggled,
       just turn it back on -- this avoids destroying a capture tool's own
       probes when ours are still intact. */
    if (!event_present("pten") || !event_present("dir") ||
        !event_present("bs") || !event_present("jaw")) {
        re_arm();
    } else if (!tracing_enabled()) {
        write_file(TRACE "/tracing_on", "1\n");
        fprintf(stderr, "gxr_alvr: tracing_on had been cleared; re-enabled\n");
    }
}

/* ---------------- parsing ---------------- */
static uint32_t bits_to_u32(const char *p) { return (uint32_t)strtoul(p, NULL, 10); }
static float u32_as_float(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static int parse_v(const char *p, float *out, int n) {
    for (int i = 0; i < n; i++) {
        char key[8]; snprintf(key, sizeof(key), "v%d=", i);
        const char *q = strstr(p, key);
        if (!q) return -1;
        out[i] = u32_as_float(bits_to_u32(q + strlen(key)));
    }
    return 0;
}
static int parse_pten(const char *line, int *idx, float v[6]) {
    const char *p = strstr(line, "pten:"); if (!p) return -1;
    const char *k = strstr(p, "idx="); if (!k) return -1;
    *idx = (int)strtol(k + 4, NULL, 16);
    return parse_v(p, v, 6);
}
static int parse_dir(const char *line, int *eye, float dir[3]) {
    const char *p = strstr(line, "dir:"); if (!p) return -1;
    const char *e = strstr(p, "e="); const char *x = strstr(p, "x=");
    const char *y = strstr(p, "y="); const char *z = strstr(p, "z=");
    if (!e || !x || !y || !z) return -1;
    *eye = (int)strtol(e + 2, NULL, 16);
    dir[0] = u32_as_float(bits_to_u32(x + 2));
    dir[1] = u32_as_float(bits_to_u32(y + 2));
    dir[2] = u32_as_float(bits_to_u32(z + 2));
    return 0;
}
static int parse_bs(const char *line, float v[FACE_N]) {
    const char *p = strstr(line, "bs:"); if (!p) return -1;
    for (int i = 0; i < FACE_N; i++) {
        char key[8]; snprintf(key, sizeof(key), "f%d=", i);
        const char *q = strstr(p, key);
        if (!q) return -1;
        v[i] = u32_as_float(bits_to_u32(q + strlen(key)));
    }
    return 0;
}
static int parse_jaw(const char *line, float v[4]) {
    const char *p = strstr(line, "jaw:"); if (!p) return -1;
    static const char *keys[4] = { "jaw=", "tout=", "tup=", "tdn=" };
    for (int i = 0; i < 4; i++) {
        const char *q = strstr(p, keys[i]);
        if (!q) return -1;
        v[i] = u32_as_float(bits_to_u32(q + strlen(keys[i])));
    }
    return 0;
}

/* ---------------- sender ---------------- */
static int g_sock = -1;
static struct sockaddr_in g_sa;
static int g_auto_target = 0;    /* 1 = resolve the Wi-Fi directed broadcast */
static int g_have_target = 0;    /* a valid destination has been set */

static void put_f32(unsigned char *p, float f) { memcpy(p, &f, 4); }

/* Directed broadcast of the active IPv4 interface. Prefer a Wi-Fi interface
   (wlan*), else the first up, non-loopback, broadcast-capable one. Returns 0
   and fills *out on success. */
static int resolve_broadcast(struct in_addr *out) {
    struct ifaddrs *ifs = NULL;
    if (getifaddrs(&ifs) != 0) return -1;
    int rc = -1;
    for (int pass = 0; pass < 2 && rc != 0; pass++) {
        for (struct ifaddrs *ifa = ifs; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_BROADCAST)) continue;
            if (!ifa->ifa_broadaddr || (ifa->ifa_flags & IFF_LOOPBACK)) continue;
            if (pass == 0 && strncmp(ifa->ifa_name, "wlan", 4) != 0) continue;
            *out = ((struct sockaddr_in *)ifa->ifa_broadaddr)->sin_addr;
            rc = 0;
            break;
        }
    }
    freeifaddrs(ifs);
    return rc;
}

/* Set/refresh g_sa for auto mode; logs only when the address changes. If no
   interface is resolvable yet (Wi-Fi still coming up) we keep the last good
   address, or stay silent and retry on the next idle tick. */
static void refresh_target(void) {
    if (!g_auto_target) return;
    struct in_addr b;
    if (resolve_broadcast(&b) != 0) {
        if (!g_have_target) {
            static int warned = 0;
            if (!warned) { warned = 1; fprintf(stderr, "gxr_alvr: no broadcast interface yet; will retry\n"); }
        }
        return;
    }
    if (!g_have_target || b.s_addr != g_sa.sin_addr.s_addr) {
        g_sa.sin_addr = b;
        g_have_target = 1;
        char s[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &b, s, sizeof(s));
        fprintf(stderr, "gxr_alvr: auto target %s:%u (broadcast)\n", s, ntohs(g_sa.sin_port));
    }
}

static void send_uniexpr(const float *ue, const float *tail) {
    if (!g_have_target) return;   /* fail soft until a destination is known */
    unsigned char buf[8 + PAYLOAD_N * 4];
    memcpy(buf, "UniExpr\0", 8);
    for (int i = 0; i < UE_MAX; i++) put_f32(buf + 8 + i * 4, ue[i]);
    for (int i = 0; i < TAIL_N; i++) put_f32(buf + 8 + (UE_MAX + i) * 4, tail[i]);
    sendto(g_sock, buf, sizeof(buf), 0, (struct sockaddr *)&g_sa, sizeof(g_sa));
}

/* ---------------- latest state ---------------- */
static float g_face[FACE_N]; static long long g_face_ms;
static int g_eyedir_mask; static float g_Ldir[3], g_Rdir[3]; static long long g_eyedir_ms;
static float g_det[MAX_DET][6]; static int g_ndet; static long long g_pupil_ms;
static float g_jaw[4]; static long long g_jaw_ms;

/* pupil dilation window */
static float g_minMm = 1e30f, g_maxMm = -1e30f;
/* jaw neutral learning (see RootedLinkFTModule.UpdateJaw) */
static float g_jawHist[JAW_HIST]; static int g_jawN, g_jawIdx; static float g_jawNeutral = 0.2f;
static long long g_jawNeutralMs;

static int cmpf(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static void compute_and_send(void) {
    long long now = now_ms();
    float ue[UE_MAX]; memset(ue, 0, sizeof(ue));

    /* 1) 68 -> UE */
    int haveFace = (now - g_face_ms) <= FRESH_MS;
    if (haveFace) for (int i = 0; i < NTAB; i++) ue[FACE68_TO_UE[i].ue] = g_face[FACE68_TO_UE[i].face];

    /* 2) lip-suck compensation (as in RootedLinkFT.UpdateFaceTracking) */
    ue[UE_LipSuckUpperLeft]  = fminf(1.0f - powf(fmaxf(ue[UE_MouthUpperLeft], 0.0f), LIP_SUCK_EXP), ue[UE_LipSuckUpperLeft]);
    ue[UE_LipSuckUpperRight] = fminf(1.0f - powf(fmaxf(ue[UE_MouthUpperRight], 0.0f), LIP_SUCK_EXP), ue[UE_LipSuckUpperRight]);

    /* 3) un-clamped jaw override (only while the vendor clamp drives it) */
    int haveJaw = (now - g_jaw_ms) <= FRESH_MS;
    if (haveJaw) {
        float raw = g_jaw[0], to = g_jaw[1], tu = g_jaw[2], td = g_jaw[3];
        int clampActive = (to > JAW_CLAMP_ACTIVE || tu > JAW_CLAMP_ACTIVE || td > JAW_CLAMP_ACTIVE);
        if (!clampActive) {
            g_jawHist[g_jawIdx] = raw; g_jawIdx = (g_jawIdx + 1) % JAW_HIST;
            if (g_jawN < JAW_HIST) g_jawN++;
        }
        if (g_jawN >= JAW_MIN_SAMPLES && now - g_jawNeutralMs > JAW_NEUTRAL_PERIOD_MS) {
            g_jawNeutralMs = now;
            float tmp[JAW_HIST]; memcpy(tmp, g_jawHist, g_jawN * sizeof(float));
            qsort(tmp, g_jawN, sizeof(float), cmpf);
            g_jawNeutral = tmp[(int)(0.05f * (g_jawN - 1))];
            if (g_jawNeutral > JAW_NEUTRAL_MAX) g_jawNeutral = JAW_NEUTRAL_MAX;
            if (g_jawNeutral < JAW_NEUTRAL_MIN) g_jawNeutral = JAW_NEUTRAL_MIN;
        }
        float shipped = ue[UE_JawOpen];
        int inBand = (shipped > JAW_BAND_LO && shipped < JAW_BAND_HI);
        if (clampActive || inBand) {
            float denom = fmaxf(1.0f - g_jawNeutral, JAW_DENOM_FLOOR);
            ue[UE_JawOpen] = clampf((raw - g_jawNeutral) / denom, 0.0f, 1.0f);
        }
    }

    /* 4) tail */
    float tail[TAIL_N]; memset(tail, 0, sizeof(tail));
    tail[T_Version] = 1.0f;

    if (haveFace) {
        float closedL = g_face[F_EYES_CLOSED_L], closedR = g_face[F_EYES_CLOSED_R];
        tail[T_LOpen] = 1.0f - clampf(closedL + closedL * ue[UE_EyeSquintLeft], 0.0f, 1.0f);
        tail[T_ROpen] = 1.0f - clampf(closedR + closedR * ue[UE_EyeSquintRight], 0.0f, 1.0f);
    }

    int havePupil = (now - g_pupil_ms) <= FRESH_MS && g_ndet > 0;
    if (havePupil) {
        float sum = 0.0f;
        for (int i = 0; i < g_ndet; i++) sum += 2.0f * fmaxf(g_det[i][2], g_det[i][3]);
        float mm = (sum / (float)g_ndet) * PUPIL_MM_SCALE;
        tail[T_LPupil] = mm; tail[T_RPupil] = mm;
        if (mm > 0.0f) {
            if (mm < g_minMm) g_minMm = mm;
            if (mm > g_maxMm) g_maxMm = mm;
            float center = 0.5f * (g_minMm + g_maxMm);
            float half = 0.5f * fmaxf(g_maxMm - g_minMm, PUPIL_WINDOW_MIN_MM);
            float wideHalf = half / fmaxf(PUPIL_DILATION_GAIN, PUPIL_GAIN_MIN);
            tail[T_MinDil] = center - wideHalf;
            tail[T_MaxDil] = center + wideHalf;
        }
    }

    int haveEye = (now - g_eyedir_ms) <= FRESH_MS;
    if (haveEye) {
        // The module maps Gaze.x <- the T_*Pitch slot and Gaze.y <- the T_*Yaw
        // slot, and VRCFT's Gaze.x is HORIZONTAL / Gaze.y VERTICAL. So yaw must
        // go into the *Pitch* slot and pitch into the *Yaw* slot; the straight
        // assignment came out rotated 90 deg (up->right, right->up).
        float gx, gy;
        if (g_eyedir_mask & 1) {
            gaze_map(g_Ldir, &gx, &gy);
            tail[T_LPitch] = gx;   // module: Gaze.x <- the T_LPitch slot
            tail[T_LYaw]   = gy;   // module: Gaze.y <- the T_LYaw slot
        }
        if (g_eyedir_mask & 2) {
            gaze_map(g_Rdir, &gx, &gy);
            tail[T_RPitch] = gx;
            tail[T_RYaw]   = gy;
        }
        static long long lastlog = 0;
        if (g_verbose && now - lastlog > 1000) {
            lastlog = now;
            float lx, ly, rx, ry;
            gaze_map(g_Ldir, &lx, &ly); gaze_map(g_Rdir, &rx, &ry);
            fprintf(stderr, "gxr_alvr: gaze Ldir=(%+.2f,%+.2f,%+.2f) Gaze.x=%+.3f Gaze.y=%+.3f "
                    "| Rdir=(%+.2f,%+.2f,%+.2f) Gaze.x=%+.3f Gaze.y=%+.3f | swap=%d inv_x=%d inv_y=%d\n",
                    g_Ldir[0], g_Ldir[1], g_Ldir[2], lx, ly,
                    g_Rdir[0], g_Rdir[1], g_Rdir[2], rx, ry, g_swap, g_inv_x, g_inv_y);
        }
    }

    send_uniexpr(ue, tail);
}

static void feed_line(const char *line) {
    int idx; float v[6];
    int eye; float d[3];
    float fv[FACE_N];
    float jv[4];

    if (parse_dir(line, &eye, d) == 0) {
        if (eye == 0) { memcpy(g_Ldir, d, 12); g_eyedir_mask |= 1; }
        else          { memcpy(g_Rdir, d, 12); g_eyedir_mask |= 2; }
        g_eyedir_ms = now_ms();
        return;
    }
    if (parse_bs(line, fv) == 0) {
        memcpy(g_face, fv, sizeof(fv)); g_face_ms = now_ms();
        compute_and_send();
        return;
    }
    if (parse_jaw(line, jv) == 0) {
        memcpy(g_jaw, jv, sizeof(jv)); g_jaw_ms = now_ms();
        return;
    }
    if (parse_pten(line, &idx, v) == 0) {
        if (idx >= 0 && idx < MAX_DET) { memcpy(g_det[idx], v, sizeof(v)); if (idx + 1 > g_ndet) g_ndet = idx + 1; }
        g_pupil_ms = now_ms();
        return;
    }
}

static void usage(FILE *f, const char *argv0) {
    fprintf(f,
        "Usage: %s [--pc <ip>] [--port <n>] [options]\n"
        "  --pc <ip>            fixed unicast target (default: auto broadcast)\n"
        "  --port <n>           UDP target port (default 41463)\n"
        "  --verbose            log the 1 Hz gaze trace for tuning\n"
        "  --gaze-swap 0|1      swap gaze axes (default 0)\n"
        "  --gaze-invert-x 0|1  invert Gaze.x (default 0)\n"
        "  --gaze-invert-y 0|1  invert Gaze.y (default 0)\n"
        "  --selftest           send one synthetic UniExpr and exit (no root)\n"
        "  --pupil-mm-scale <f> --pupil-dilation-gain <f>\n"
        "  --pupil-offset <hex> --eye-dir-offset <hex>\n"
        "  --face-bs-offset <hex> --jaw-offset <hex>\n"
        "  --eye-plugin <path> --face-plugin <path>\n",
        argv0);
}

int main(int argc, char **argv) {
    const char *pc = NULL;
    int port = 41463;
    unsigned long pupil_off = DEFAULT_PUPIL_OFFSET, eyedir_off = DEFAULT_EYEDIR_OFFSET;
    unsigned long face_off = DEFAULT_FACE_BS_OFFSET, jaw_off = DEFAULT_JAW_OFFSET;
    const char *eye_plugin = EYE_PLUGIN, *face_plugin = FACE_PLUGIN;
    int selftest = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pc") && i + 1 < argc) pc = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (!strcmp(argv[i], "--verbose")) g_verbose = 1;
        else if (!strcmp(argv[i], "--gaze-swap") && i + 1 < argc) g_swap = atoi(argv[++i]) ? 1 : 0;
        else if (!strcmp(argv[i], "--gaze-invert-x") && i + 1 < argc) g_inv_x = atoi(argv[++i]) ? 1 : 0;
        else if (!strcmp(argv[i], "--gaze-invert-y") && i + 1 < argc) g_inv_y = atoi(argv[++i]) ? 1 : 0;
        else if (!strcmp(argv[i], "--pupil-mm-scale") && i + 1 < argc) PUPIL_MM_SCALE = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--pupil-dilation-gain") && i + 1 < argc) PUPIL_DILATION_GAIN = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--pupil-offset") && i + 1 < argc) pupil_off = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--eye-dir-offset") && i + 1 < argc) eyedir_off = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--face-bs-offset") && i + 1 < argc) face_off = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--jaw-offset") && i + 1 < argc) jaw_off = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--eye-plugin") && i + 1 < argc) eye_plugin = argv[++i];
        else if (!strcmp(argv[i], "--face-plugin") && i + 1 < argc) face_plugin = argv[++i];
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(stdout, argv[0]); return 0; }
        else { fprintf(stderr, "gxr_alvr: unknown arg: %s\n", argv[i]); usage(stderr, argv[0]); return 2; }
    }

    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sig; sigemptyset(&sa.sa_mask); sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);

    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) { perror("socket"); return 2; }
    /* SO_BROADCAST is required for auto mode and harmless for unicast. */
    int bcast = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));
    memset(&g_sa, 0, sizeof(g_sa));
    g_sa.sin_family = AF_INET;
    g_sa.sin_port = htons((uint16_t)port);

    char target[INET_ADDRSTRLEN + 16];
    if (pc) {
        if (inet_pton(AF_INET, pc, &g_sa.sin_addr) != 1) {
            fprintf(stderr, "gxr_alvr: bad --pc ip '%s'\n", pc);
            return 2;
        }
        g_have_target = 1;
        snprintf(target, sizeof(target), "%s", pc);
    } else {
        g_auto_target = 1;
        refresh_target();   /* may stay unresolved if Wi-Fi is not up yet */
        if (g_have_target) {
            char s[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &g_sa.sin_addr, s, sizeof(s));
            snprintf(target, sizeof(target), "%s", s);
        } else {
            snprintf(target, sizeof(target), "auto (unresolved)");
        }
    }

    if (selftest) {
        if (!g_have_target) {
            fprintf(stderr, "gxr_alvr: selftest: no target resolved (no broadcast interface)\n");
            return 1;
        }
        float ue[UE_MAX], tail[TAIL_N];
        for (int i = 0; i < UE_MAX; i++) ue[i] = (float)i * 0.01f;
        memset(tail, 0, sizeof(tail));
        tail[T_Version] = 1.0f;
        tail[T_LOpen] = 0.9f; tail[T_ROpen] = 0.8f;
        tail[T_LPupil] = 4.5f; tail[T_RPupil] = 4.6f;
        tail[T_MinDil] = 3.0f; tail[T_MaxDil] = 6.0f;
        tail[T_LPitch] = 0.11f; tail[T_LYaw] = 0.21f;
        tail[T_RPitch] = 0.12f; tail[T_RYaw] = 0.22f;
        send_uniexpr(ue, tail);
        fprintf(stderr, "gxr_alvr: selftest sent 1 UniExpr (%d floats) to %s:%d\n", PAYLOAD_N, target, port);
        return 0;
    }

    /* Single instance: exit if another live relay holds the lock. */
    if (acquire_lock() < 0) return 5;

    tracefs_cleanup();
    write_file(TRACE "/buffer_size_kb", "16384\n");
    g_pupil_off = pupil_off; g_eyedir_off = eyedir_off;
    g_face_off = face_off; g_jaw_off = jaw_off;
    g_eye_plugin = eye_plugin; g_face_plugin = face_plugin;
    usleep(200000);   /* let any just-pkill'd peer release tracefs */
    if (arm_pupil(eye_plugin, pupil_off) != 0) return 3;
    if (arm_eyedir(eye_plugin, eyedir_off) != 0) fprintf(stderr, "gxr_alvr: eyedir probe not armed (continuing)\n");
    if (arm_face(face_plugin, face_off) != 0) fprintf(stderr, "gxr_alvr: face probe not armed (continuing)\n");
    if (arm_jaw(face_plugin, jaw_off) != 0) fprintf(stderr, "gxr_alvr: jaw probe not armed (continuing)\n");
    write_file(TRACE "/tracing_on", "1\n");
    fprintf(stderr, "gxr_alvr: armed pten@0x%lx + eyedir@0x%lx + face68@0x%lx + jaw@0x%lx -> %s:%d (UniExpr)\n",
            pupil_off, eyedir_off, face_off, jaw_off, target, port);

    int fd = open(TRACE "/trace_pipe", O_RDONLY);
    if (fd < 0) { fprintf(stderr, "gxr_alvr: open trace_pipe: %s\n", strerror(errno)); disarm_all(); return 4; }

    char buf[65536]; static char line[8192]; size_t carry = 0;
    struct pollfd pfd;
    pfd.fd = fd; pfd.events = POLLIN;
    while (!g_stop) {
        int pr = poll(&pfd, 1, SOCK_POLL_MS);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) { health_check(); refresh_target(); continue; }
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) continue;
        size_t start = 0;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] != '\n') continue;
            size_t seg = (size_t)i - start;
            if (carry + seg < sizeof(line)) {
                memcpy(line + carry, buf + start, seg);
                line[carry + seg] = '\0';
                feed_line(line);
            }
            carry = 0; start = (size_t)i + 1;
        }
        size_t rem = (size_t)n - start;
        if (rem >= sizeof(line)) rem = sizeof(line) - 1;
        memcpy(line, buf + start, rem); carry = rem;
    }

    close(fd);
    disarm_all();
    close(g_sock);
    return 0;
}
