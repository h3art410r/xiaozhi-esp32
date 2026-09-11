// Box0 Voice Link - MicYou native plugin.
// Listens on UDP 9125 for BOX0 wireless-mic button signals and translates
// them into desktop hotkeys (WeChat Input voice typing):
//   VOICE_START      -> tap Ctrl+Win+Shift (start voice input)
//   VOICE_STOP       -> tap Ctrl+Win+Shift again (any key exits)
//   VOICE_HOLD_START -> hold Left Alt+Left Win down (hold-to-talk while M is held)
//   VOICE_HOLD_STOP  -> release Left Alt+Left Win
//   VOICE_ENTER      -> Enter (send recognized text)
//   VOICE_DELETE     -> Backspace (delete left)
// Hotkeys and port are editable on the plugin card in MicYou (config.read).

#include "micyou_plugin_abi.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const mpl_plugin_info_t g_info = { MPL_ABI_VERSION, 1u, "dev.box0.voicelink", "1.0.0" };

extern "C" MPL_EXPORT const mpl_plugin_info_t* micyou_plugin_info(void) {
    return &g_info;
}

// Host API: only retain the frozen-prefix fields we actually call, so the
// plugin stays compatible regardless of the host's table size.
static void (*g_log)(void*, mpl_log_level_t, const char*) = NULL;
static mpl_result_t (*g_get_config)(void*, const char*, char*, uint32_t*) = NULL;
static void* g_ctx = NULL;

static void plog(mpl_log_level_t level, const char* msg) {
    if (g_log) g_log(g_ctx, level, msg);
}

// ---- key combo helpers ----
static WORD key_vk(const char* name, size_t len) {
    char buf[16];
    if (len == 0 || len >= sizeof(buf)) return 0;
    for (size_t i = 0; i < len; i++) buf[i] = (char)toupper((unsigned char)name[i]);
    buf[len] = 0;
    if (!strcmp(buf, "CTRL") || !strcmp(buf, "CONTROL")) return VK_LCONTROL;
    if (!strcmp(buf, "WIN")) return VK_LWIN;
    if (!strcmp(buf, "ALT")) return VK_LMENU;
    if (!strcmp(buf, "SHIFT")) return VK_LSHIFT;
    if (!strcmp(buf, "RALT")) return VK_RMENU;
    if (!strcmp(buf, "RCTRL") || !strcmp(buf, "RCONTROL")) return VK_RCONTROL;
    if (!strcmp(buf, "ESC")) return VK_ESCAPE;
    if (!strcmp(buf, "SPACE")) return VK_SPACE;
    if (!strcmp(buf, "ENTER")) return VK_RETURN;
    if (!strcmp(buf, "TAB")) return VK_TAB;
    if (!strcmp(buf, "BACKSPACE")) return VK_BACK;
    if (!strcmp(buf, "DELETE")) return VK_DELETE;
    if (len == 1 && buf[0] >= 'A' && buf[0] <= 'Z') return (WORD)buf[0];
    if (len == 1 && buf[0] >= '0' && buf[0] <= '9') return (WORD)buf[0];
    if (len >= 2 && buf[0] == 'F') {
        int n = atoi(buf + 1);
        if (n >= 1 && n <= 12) return (WORD)(VK_F1 + n - 1);
    }
    return 0;
}

static int parse_combo(const char* combo, WORD* out, int max_keys) {
    int count = 0;
    const char* p = combo;
    while (*p && count < max_keys) {
        const char* plus = strchr(p, '+');
        size_t len = plus ? (size_t)(plus - p) : strlen(p);
        while (len > 0 && (*p == ' ' || *p == '\t')) { p++; len--; }
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
        WORD vk = key_vk(p, len);
        if (vk) out[count++] = vk;
        if (!plus) break;
        p = plus + 1;
    }
    return count;
}

static void send_key(WORD vk, int keyup) {
    INPUT in;
    memset(&in, 0, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = keyup ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}

static void press_combo(const WORD* keys, int count, int hold_ms) {
    for (int i = 0; i < count; i++) send_key(keys[i], 0);
    if (hold_ms > 0) Sleep((DWORD)hold_ms);
    for (int i = count - 1; i >= 0; i--) send_key(keys[i], 1);
}

// ---- config ----
static char g_start_keys[64] = "CTRL+WIN+SHIFT";
static char g_stop_keys[64] = "CTRL+WIN+SHIFT";
static char g_hold_keys[64] = "ALT+WIN";
static char g_enter_keys[64] = "ENTER";
static char g_delete_keys[64] = "BACKSPACE";
static int g_port = 9125;
static WORD g_start_combo[6];
static int g_start_n = 0;
static WORD g_stop_combo[6];
static int g_stop_n = 0;
static WORD g_hold_combo[6];
static int g_hold_n = 0;
static WORD g_enter_combo[6];
static int g_enter_n = 0;
static WORD g_delete_combo[6];
static int g_delete_n = 0;

// Config values arrive as JSON (strings are quoted); strip one pair of quotes.
static void read_cfg_str(const char* key, char* out, size_t outsz) {
    if (!g_get_config) return;
    char buf[128];
    uint32_t sz = sizeof(buf) - 1;
    if (g_get_config(g_ctx, key, buf, &sz) != MPL_OK) return;
    if (sz >= sizeof(buf)) sz = sizeof(buf) - 1;
    buf[sz] = 0;
    size_t len = strlen(buf);
    char* s = buf;
    if (len >= 2 && buf[0] == '"' && buf[len - 1] == '"') { buf[len - 1] = 0; s = buf + 1; }
    if (*s) {
        strncpy(out, s, outsz - 1);
        out[outsz - 1] = 0;
    }
}

// ---- UDP listener thread ----
static HANDLE g_thread = NULL;
static volatile int g_stop = 0;

static DWORD WINAPI udp_thread(LPVOID arg) {
    (void)arg;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        plog(MPL_LOG_ERROR, "[voice-link] WSAStartup failed");
        return 0;
    }
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        plog(MPL_LOG_ERROR, "[voice-link] socket() failed");
        WSACleanup();
        return 0;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)g_port);
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[voice-link] bind UDP %d failed (voice_link.exe still running?)", g_port);
        plog(MPL_LOG_ERROR, msg);
        closesocket(s);
        WSACleanup();
        return 0;
    }
    // 200 ms receive timeout so deinit's stop flag is picked up promptly
    DWORD tv = 200;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    {
        char msg[224];
        snprintf(msg, sizeof(msg),
                 "[voice-link] listening UDP %d (start=%s stop=%s hold=%s enter=%s delete=%s)",
                 g_port, g_start_keys, g_stop_keys, g_hold_keys, g_enter_keys, g_delete_keys);
        plog(MPL_LOG_INFO, msg);
    }
    while (!g_stop) {
        char buf[128];
        int n = recvfrom(s, buf, sizeof(buf) - 1, 0, NULL, NULL);
        if (n <= 0) continue;  // timeout or error; loop re-checks g_stop
        buf[n] = 0;
        if (!strncmp(buf, "VOICE_START", 11)) {
            plog(MPL_LOG_INFO, "[voice-link] VOICE_START");
            press_combo(g_start_combo, g_start_n, 0);
        } else if (!strncmp(buf, "VOICE_STOP", 10)) {
            plog(MPL_LOG_INFO, "[voice-link] VOICE_STOP");
            press_combo(g_stop_combo, g_stop_n, 0);
        } else if (!strncmp(buf, "VOICE_HOLD_START", 16)) {
            plog(MPL_LOG_INFO, "[voice-link] VOICE_HOLD_START");
            for (int i = 0; i < g_hold_n; i++) send_key(g_hold_combo[i], 0);
        } else if (!strncmp(buf, "VOICE_HOLD_STOP", 15)) {
            plog(MPL_LOG_INFO, "[voice-link] VOICE_HOLD_STOP");
            for (int i = g_hold_n - 1; i >= 0; i--) send_key(g_hold_combo[i], 1);
        } else if (!strncmp(buf, "VOICE_ENTER", 11)) {
            plog(MPL_LOG_INFO, "[voice-link] VOICE_ENTER");
            press_combo(g_enter_combo, g_enter_n, 0);
        } else if (!strncmp(buf, "VOICE_DELETE", 12)) {
            plog(MPL_LOG_INFO, "[voice-link] VOICE_DELETE");
            press_combo(g_delete_combo, g_delete_n, 0);
        }
    }
    closesocket(s);
    WSACleanup();
    return 0;
}

extern "C" MPL_EXPORT mpl_result_t micyou_plugin_init(const mpl_host_api_t* host) {
    if (!host) return MPL_ERR_INVALID_ARG;
    g_log = host->log;
    g_get_config = host->get_config;
    g_ctx = host->ctx;

    read_cfg_str("startKeys", g_start_keys, sizeof(g_start_keys));
    read_cfg_str("stopKeys", g_stop_keys, sizeof(g_stop_keys));
    read_cfg_str("holdKeys", g_hold_keys, sizeof(g_hold_keys));
    read_cfg_str("enterKeys", g_enter_keys, sizeof(g_enter_keys));
    read_cfg_str("deleteKeys", g_delete_keys, sizeof(g_delete_keys));
    {
        char tmp[32] = {0};
        read_cfg_str("port", tmp, sizeof(tmp));
        if (tmp[0]) {
            int p = atoi(tmp);
            if (p > 1024 && p < 65535) g_port = p;
        }
    }

    g_start_n = parse_combo(g_start_keys, g_start_combo, 6);
    g_stop_n = parse_combo(g_stop_keys, g_stop_combo, 6);
    g_hold_n = parse_combo(g_hold_keys, g_hold_combo, 6);
    g_enter_n = parse_combo(g_enter_keys, g_enter_combo, 6);
    g_delete_n = parse_combo(g_delete_keys, g_delete_combo, 6);
    if (g_start_n == 0 || g_stop_n == 0 || g_hold_n == 0 || g_enter_n == 0 || g_delete_n == 0) {
        plog(MPL_LOG_ERROR, "[voice-link] invalid key combo in config");
        return MPL_ERR_INVALID_ARG;
    }

    g_stop = 0;
    g_thread = CreateThread(NULL, 0, udp_thread, NULL, 0, NULL);
    if (!g_thread) {
        plog(MPL_LOG_ERROR, "[voice-link] CreateThread failed");
        return MPL_ERR_RUNTIME;
    }
    plog(MPL_LOG_INFO, "[voice-link] plugin started");
    return MPL_OK;
}

extern "C" MPL_EXPORT void micyou_plugin_deinit(void) {
    g_stop = 1;
    if (g_thread) {
        WaitForSingleObject(g_thread, 1500);  // recv timeout is 200 ms
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}
