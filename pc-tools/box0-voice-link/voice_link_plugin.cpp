// Box0 Voice Link - MicYou native plugin.
// The BOX0 wireless-mic page streams audio continuously and sends button
// signals over UDP 9125:
//   KEY_SEND n       -> tap the n-th configured hotkey (0=start 1=enter 2=delete)
//   VOICE_HOLD_START -> hold Left Alt+Left Win down (hold-to-talk while M held)
//   VOICE_HOLD_STOP  -> release Left Alt+Left Win
//   VOICE_DELETE     -> Backspace (delete left)
//   REC_START/STOP   -> start/stop a two-track recording session
// While recording, the plugin taps the live audio stream in
// micyou_plugin_process (mic track) and captures the default output device
// via WASAPI loopback (system track). Each session writes three files:
//   rec_<timestamp>_mic.wav / _sys.wav / _mix.wav  (into recDir)
// Hotkeys and port are editable on the plugin card in MicYou (config.read).

#include "micyou_plugin_abi.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <stdint.h>

#include <mmdeviceapi.h>
#include <audioclient.h>

static const mpl_plugin_info_t g_info = { MPL_ABI_VERSION, 1u, "dev.box0.voicelink", "1.2.0" };

extern "C" MPL_EXPORT const mpl_plugin_info_t* micyou_plugin_info(void) {
    return &g_info;
}

// Host API: only retain the frozen-prefix fields we actually call, so the
// plugin stays compatible regardless of the host's table size.
static void (*g_log)(void*, mpl_log_level_t, const char*) = NULL;
static mpl_result_t (*g_get_config)(void*, const char*, char*, uint32_t*) = NULL;
static mpl_result_t (*g_audio_state)(void*, char*, uint32_t*) = NULL;
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

static float read_cfg_float(const char* key, float dflt) {
    if (!g_get_config) return dflt;
    char buf[64];
    uint32_t sz = sizeof(buf) - 1;
    if (g_get_config(g_ctx, key, buf, &sz) != MPL_OK) return dflt;
    if (sz >= sizeof(buf)) sz = sizeof(buf) - 1;
    buf[sz] = 0;
    float v = (float)atof(buf);
    return v > 0 ? v : dflt;
}

// ---- recording engine ----
// Two tracks per session: the live mic stream (tapped in the host's realtime
// audio callback, copied into a lock-free ring) and the system playback
// (WASAPI loopback on the default output device). Both are written as 16-bit
// WAV while recording; on stop a _mix.wav is produced. All file I/O happens on
// the writer/sys threads or the UDP thread — never on the audio callback.

static volatile int g_stop = 0;

#define REC_RING_BYTES (1u << 21)  // 2 MiB ~= 5.5 s at 48 kHz stereo f32
#define REC_RING_MASK (REC_RING_BYTES - 1)

static uint8_t g_rec_ring[REC_RING_BYTES];
static std::atomic<uint32_t> g_rec_w{0};
static std::atomic<uint32_t> g_rec_r{0};

static std::atomic<int> g_recording{0};
static std::atomic<int> g_rec_stop_req{0};
static std::atomic<int> g_sys_active{0};
static std::atomic<ULONG64> g_last_mic_frame_ms{0};
static std::atomic<int> g_mic_frames_seen{0};
static std::atomic<int> g_mic_channels{0};
static std::atomic<int> g_rec_dropped_bytes{0};

static char g_rec_dir[256] = "";  // empty -> %USERPROFILE%\Documents\box0-rec
static float g_mic_gain = 1.0f;
static float g_sys_gain = 1.0f;
static char g_rec_base[MAX_PATH] = "";  // session path without extension

static CRITICAL_SECTION g_rec_cs;
static HANDLE g_rec_idle_evt = NULL;   // set when a stop request is fully done
static HANDLE g_sys_done_evt = NULL;   // set when the sys track is finalized

typedef struct {
    FILE* f;
    int rate;
    int channels;
    uint32_t data_bytes;
    int valid;
} wav_t;

static wav_t g_mic_wav;
static wav_t g_sys_wav;

static void wav_write_header(wav_t* w) {
    uint8_t hdr[44];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr + 0, "RIFF", 4);
    uint32_t riff_sz = 36 + w->data_bytes;
    memcpy(hdr + 4, &riff_sz, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    uint32_t fmt_sz = 16;
    memcpy(hdr + 16, &fmt_sz, 4);
    uint16_t audio_fmt = 1, bits = 16;
    uint16_t ch = (uint16_t)w->channels;
    uint32_t byte_rate = (uint32_t)(w->rate * w->channels * 2);
    uint16_t block_align = (uint16_t)(w->channels * 2);
    memcpy(hdr + 20, &audio_fmt, 2);
    memcpy(hdr + 22, &ch, 2);
    memcpy(hdr + 24, &w->rate, 4);
    memcpy(hdr + 28, &byte_rate, 4);
    memcpy(hdr + 32, &block_align, 2);
    memcpy(hdr + 34, &bits, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &w->data_bytes, 4);
    fseek(w->f, 0, SEEK_SET);
    fwrite(hdr, 1, sizeof(hdr), w->f);
}

static int wav_open(wav_t* w, const char* path, int rate, int channels) {
    memset(w, 0, sizeof(*w));
    w->f = fopen(path, "wb+");
    if (!w->f) return -1;
    w->rate = rate;
    w->channels = channels;
    wav_write_header(w);
    w->valid = 1;
    return 0;
}

static void wav_write_f32(wav_t* w, const float* data, uint32_t frames) {
    int16_t buf[2048];
    uint32_t done = 0;
    while (done < frames) {
        uint32_t n = frames - done;
        if (n > sizeof(buf) / sizeof(int16_t) / (uint32_t)w->channels) {
            n = sizeof(buf) / sizeof(int16_t) / (uint32_t)w->channels;
        }
        uint32_t total = n * (uint32_t)w->channels;
        for (uint32_t i = 0; i < total; i++) {
            float v = data[(size_t)done * w->channels + i] * 32767.0f;
            if (v > 32767.0f) v = 32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            buf[i] = (int16_t)v;
        }
        fwrite(buf, 2, total, w->f);
        w->data_bytes += total * 2;
        done += n;
    }
}

static void wav_finalize(wav_t* w) {
    if (!w->valid) return;
    wav_write_header(w);
    fclose(w->f);
    w->valid = 0;
}

// Ask the host for the audio pipeline's sample rate (audio_state JSON).
static int query_host_rate(void) {
    if (!g_audio_state) return 0;
    char buf[256];
    uint32_t sz = sizeof(buf) - 1;
    if (g_audio_state(g_ctx, buf, &sz) != MPL_OK || sz == 0) return 0;
    if (sz >= sizeof(buf)) sz = sizeof(buf) - 1;
    buf[sz] = 0;
    const char* p = strstr(buf, "sampleRate");
    if (!p) return 0;
    p = strchr(p, ':');
    return p ? atoi(p + 1) : 0;
}

// Realtime audio tap (host audio thread). Must stay lock-free and fast.
extern "C" MPL_EXPORT mpl_result_t micyou_plugin_process(float* data, uint32_t samples,
                                                         uint32_t channels, double queued_ms,
                                                         uint32_t* bypass) {
    (void)data;
    (void)queued_ms;
    if (bypass) *bypass = 1;  // we never modify the stream
    if (!g_recording.load(std::memory_order_relaxed) || channels == 0 || samples == 0) {
        return MPL_OK;
    }
    if (g_mic_channels.load(std::memory_order_relaxed) == 0) {
        g_mic_channels.store((int)channels, std::memory_order_relaxed);
    }
    uint32_t bytes = samples * channels * (uint32_t)sizeof(float);
    uint32_t w = g_rec_w.load(std::memory_order_relaxed);
    uint32_t r = g_rec_r.load(std::memory_order_acquire);
    uint32_t avail = (r - w - 1) & REC_RING_MASK;
    if (bytes > avail) {  // ring full: drop the newest tail, keep recording
        g_rec_dropped_bytes.fetch_add(bytes - avail, std::memory_order_relaxed);
        bytes = avail;
    }
    if (bytes > 0) {
        const uint8_t* src = (const uint8_t*)data;
        uint32_t first = REC_RING_BYTES - w;
        if (first > bytes) first = bytes;
        memcpy(g_rec_ring + w, src, first);
        if (bytes > first) {
            memcpy(g_rec_ring, src + first, bytes - first);
        }
        g_rec_w.store((w + bytes) & REC_RING_MASK, std::memory_order_release);
        g_mic_frames_seen.store(1, std::memory_order_relaxed);
    }
    g_last_mic_frame_ms.store(GetTickCount64(), std::memory_order_relaxed);
    return MPL_OK;
}

static void build_rec_paths(void) {
    char dir[MAX_PATH];
    if (g_rec_dir[0]) {
        strncpy(dir, g_rec_dir, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = 0;
    } else {
        const char* home = getenv("USERPROFILE");
        if (!home) home = "C:\\";
        snprintf(dir, sizeof(dir), "%s\\Documents\\box0-rec", home);
    }
    CreateDirectoryA(dir, NULL);
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(g_rec_base, sizeof(g_rec_base),
             "%s\\rec_%04d%02d%02d_%02d%02d%02d", dir,
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static void rec_start(void) {
    EnterCriticalSection(&g_rec_cs);
    if (!g_recording.load()) {
        build_rec_paths();
        g_rec_w.store(0, std::memory_order_relaxed);
        g_rec_r.store(0, std::memory_order_relaxed);
        g_rec_dropped_bytes.store(0, std::memory_order_relaxed);
        g_mic_frames_seen.store(0, std::memory_order_relaxed);
        g_mic_channels.store(0, std::memory_order_relaxed);
        g_last_mic_frame_ms.store(GetTickCount64(), std::memory_order_relaxed);
        memset(&g_mic_wav, 0, sizeof(g_mic_wav));
        memset(&g_sys_wav, 0, sizeof(g_sys_wav));
        g_rec_stop_req.store(0, std::memory_order_relaxed);
        ResetEvent(g_sys_done_evt);
        ResetEvent(g_rec_idle_evt);
        g_sys_active.store(1, std::memory_order_relaxed);
        g_recording.store(1, std::memory_order_release);
        char msg[320];
        snprintf(msg, sizeof(msg), "[voice-link] REC_START -> %s_{mic,sys,mix}.wav", g_rec_base);
        plog(MPL_LOG_INFO, msg);
    }
    LeaveCriticalSection(&g_rec_cs);
}

// Called from the UDP thread (waits for completion) or the writer thread
// watchdog (waitless). Writer thread finalizes files and mixes.
static void rec_stop(int wait) {
    if (g_recording.load()) {
        g_rec_stop_req.store(1, std::memory_order_release);
    }
    if (wait) {
        WaitForSingleObject(g_rec_idle_evt, 8000);
    }
}

// ---- two-track mixer (streaming, linear resample, 16-bit out) ----
typedef struct {
    FILE* f;
    int rate;
    int ch;
    uint32_t frames;    // total frames in data chunk
    uint32_t idx;       // next frame to read
    int16_t buf[2 * 8192 + 8];  // frame cache [base, base+n)
    uint32_t base;
    uint32_t n;
    int eof;
    float step;         // in_frames per out_frame
    double pos;         // current read position in input frames
    float gain;
} mixsrc_t;

static int mix_open(mixsrc_t* m, const char* path, int out_rate, float gain) {
    memset(m, 0, sizeof(*m));
    m->f = fopen(path, "rb");
    if (!m->f) return -1;
    uint8_t hdr[44];
    if (fread(hdr, 1, 44, m->f) != 44 || memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVEfmt ", 8) != 0 || memcmp(hdr + 36, "data", 4) != 0) {
        fclose(m->f);
        m->f = NULL;
        return -1;
    }
    uint16_t fmt, ch, bits;
    uint32_t rate, data_sz;
    memcpy(&fmt, hdr + 20, 2);
    memcpy(&ch, hdr + 22, 2);
    memcpy(&rate, hdr + 24, 4);
    memcpy(&data_sz, hdr + 40, 4);
    memcpy(&bits, hdr + 34, 2);
    if (fmt != 1 || bits != 16 || ch == 0 || rate == 0) {
        fclose(m->f);
        m->f = NULL;
        return -1;
    }
    m->rate = (int)rate;
    m->ch = ch;
    m->frames = data_sz / (ch * 2);
    m->step = (double)m->rate / (double)out_rate;
    m->gain = gain;
    m->pos = 0.0;
    return 0;
}

// Ensure frames [need, need+1] are cached; returns 0 while data is available.
static int mix_ensure(mixsrc_t* m, uint32_t need) {
    while (!m->eof && m->base + m->n < need + 2) {
        if (m->idx >= m->frames) { m->eof = 1; break; }
        if (m->n > 0 && m->base + m->n <= need) {
            // discard consumed frames, keep the last one for interpolation
            memmove(m->buf, m->buf + (m->n - 1) * m->ch, (size_t)m->ch * 2);
            m->base += m->n - 1;
            m->n = 1;
        }
        uint32_t want = 8192 - m->n;
        uint32_t left = m->frames - m->idx;
        if (want > left) want = left;
        if (want == 0) { m->eof = 1; break; }
        if (fread(m->buf + m->n * m->ch, (size_t)m->ch * 2, want, m->f) != want) {
            m->eof = 1;
            break;
        }
        m->idx += want;
        m->n += want;
    }
    return m->base + m->n > need ? 0 : -1;
}

static float mix_sample(const mixsrc_t* m, uint32_t i, int c) {
    return (float)m->buf[(i - m->base) * m->ch + (c < m->ch ? c : 0)] / 32768.0f;
}

// Read frame at fractional position pos (lerp between adjacent frames).
static float mix_read(const mixsrc_t* m, double pos, int c) {
    uint32_t i = (uint32_t)pos;
    float frac = (float)(pos - (double)i);
    float a = mix_sample(m, i, c);
    if (frac <= 0.0f) return a;
    float b = (i + 1 >= m->base + m->n) ? a : mix_sample(m, i + 1, c);
    return a + (b - a) * frac;
}

static void mix_tracks(const char* base) {
    char mic_path[MAX_PATH], sys_path[MAX_PATH], out_path[MAX_PATH];
    snprintf(mic_path, sizeof(mic_path), "%s_mic.wav", base);
    snprintf(sys_path, sizeof(sys_path), "%s_sys.wav", base);
    snprintf(out_path, sizeof(out_path), "%s_mix.wav", base);

    mixsrc_t a, b;
    int have_a = mix_open(&a, mic_path, 1, g_mic_gain) == 0;
    int have_b = mix_open(&b, sys_path, 1, g_sys_gain) == 0;
    if (!have_a && !have_b) {
        plog(MPL_LOG_WARN, "[voice-link] mix skipped: no tracks");
        return;
    }
    // resolve output rate/channels lazily after both sources are open
    int out_rate = have_a ? a.rate : b.rate;
    if (have_b && b.rate > out_rate) out_rate = b.rate;
    if (out_rate <= 0) out_rate = 48000;
    int out_ch = 1;
    if ((have_a && a.ch > 1) || (have_b && b.ch > 1)) out_ch = 2;
    if (have_a) a.step = (double)a.rate / out_rate;
    if (have_b) b.step = (double)b.rate / out_rate;

    wav_t out;
    if (wav_open(&out, out_path, out_rate, out_ch) != 0) {
        plog(MPL_LOG_ERROR, "[voice-link] cannot open mix wav");
        if (have_a) fclose(a.f);
        if (have_b) fclose(b.f);
        return;
    }
    // recompute fractional positions against real step
    uint32_t total_a = have_a ? a.frames : 0;
    uint32_t total_b = have_b ? b.frames : 0;
    double dur_a = have_a ? (double)total_a / a.rate : 0.0;
    double dur_b = have_b ? (double)total_b / b.rate : 0.0;
    double dur = dur_a > dur_b ? dur_a : dur_b;
    uint32_t out_frames = (uint32_t)(dur * out_rate + 0.5);
    int16_t obuf[4096];
    for (uint32_t done = 0; done < out_frames;) {
        uint32_t n = out_frames - done;
        if (n > 4096 / (uint32_t)out_ch) n = 4096 / (uint32_t)out_ch;
        for (uint32_t i = 0; i < n; i++) {
            for (int c = 0; c < out_ch; c++) {
                float v = 0.0f;
                if (have_a) {
                    if (mix_ensure(&a, (uint32_t)a.pos) == 0) v += mix_read(&a, a.pos, c) * a.gain;
                    a.pos += a.step;
                }
                if (have_b) {
                    if (mix_ensure(&b, (uint32_t)b.pos) == 0) v += mix_read(&b, b.pos, c) * b.gain;
                    b.pos += b.step;
                }
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                obuf[i * out_ch + c] = (int16_t)(v * 32767.0f);
            }
        }
        fwrite(obuf, 2, n * (uint32_t)out_ch, out.f);
        out.data_bytes += n * (uint32_t)out_ch * 2;
        done += n;
    }
    wav_finalize(&out);
    if (have_a) fclose(a.f);
    if (have_b) fclose(b.f);
    char msg[320];
    snprintf(msg, sizeof(msg), "[voice-link] mix written: %s", out_path);
    plog(MPL_LOG_INFO, msg);
}

// ---- sys track: WASAPI loopback of the default output device ----
static DWORD WINAPI sys_thread(LPVOID arg) {
    (void)arg;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    while (!g_stop) {
        if (!g_sys_active.load(std::memory_order_relaxed)) {
            Sleep(50);
            continue;
        }
        int ok = 0;
        IMMDeviceEnumerator* en = NULL;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
                                       CLSCTX_ALL, IID_PPV_ARGS(&en)))) {
            IMMDevice* dev = NULL;
            if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) {
                IAudioClient* ac = NULL;
                if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&ac))) {
                    WAVEFORMATEX* fmt = NULL;
                    if (SUCCEEDED(ac->GetMixFormat(&fmt))) {
                        if (SUCCEEDED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                     AUDCLNT_STREAMFLAGS_LOOPBACK,
                                                     10000000, 0, fmt, NULL))) {
                            char path[MAX_PATH];
                            snprintf(path, sizeof(path), "%s_sys.wav", g_rec_base);
                            if (wav_open(&g_sys_wav, path, (int)fmt->nSamplesPerSec, fmt->nChannels) == 0) {
                                IAudioCaptureClient* cap = NULL;
                                if (SUCCEEDED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cap))) {
                                    ok = 1;
                                    ac->Start();
                                    while (ok && g_sys_active.load(std::memory_order_relaxed) && !g_stop) {
                                        UINT32 pkt = 0;
                                        if (FAILED(cap->GetNextPacketSize(&pkt)) || pkt == 0) {
                                            Sleep(10);
                                            continue;
                                        }
                                        BYTE* data = NULL;
                                        UINT32 frames = 0;
                                        DWORD flags = 0;
                                        if (FAILED(cap->GetBuffer(&data, &frames, &flags, NULL, NULL))) {
                                            ok = 0;
                                            break;
                                        }
                                        if (frames > 0) {
                                            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                                                static const float zeros[1024] = {0};
                                                uint32_t left = frames;
                                                while (left > 0) {
                                                    uint32_t n = left > 1024 ? 1024 : left;
                                                    wav_write_f32(&g_sys_wav, zeros, n);
                                                    left -= n;
                                                }
                                            } else {
                                                wav_write_f32(&g_sys_wav, (const float*)data, frames);
                                            }
                                        }
                                        cap->ReleaseBuffer(frames);
                                    }
                                    ac->Stop();
                                    if (cap) cap->Release();
                                }
                                wav_finalize(&g_sys_wav);
                            } else {
                                plog(MPL_LOG_ERROR, "[voice-link] cannot open sys wav");
                            }
                        }
                        CoTaskMemFree(fmt);
                    }
                    ac->Release();
                }
                dev->Release();
            } else {
                plog(MPL_LOG_WARN, "[voice-link] no default output device, sys track skipped");
            }
            en->Release();
        }
        (void)ok;
        g_sys_active.store(0, std::memory_order_relaxed);
        SetEvent(g_sys_done_evt);
    }
    CoUninitialize();
    return 0;
}

// ---- mic writer thread: drains the ring, finalizes and mixes on stop ----
static DWORD WINAPI rec_writer_thread(LPVOID arg) {
    (void)arg;
    ULONG64 last_flush_ms = 0;
    while (!g_stop) {
        if (g_recording.load(std::memory_order_relaxed)) {
            uint32_t r = g_rec_r.load(std::memory_order_relaxed);
            uint32_t w = g_rec_w.load(std::memory_order_acquire);
            if (w != r) {
                if (!g_mic_wav.valid) {
                    int ch = g_mic_channels.load(std::memory_order_relaxed);
                    if (ch == 0) {
                        Sleep(5);  // waiting for the first audio frame
                        continue;
                    }
                    int rate = query_host_rate();
                    if (rate <= 0) rate = 16000;
                    char path[MAX_PATH];
                    snprintf(path, sizeof(path), "%s_mic.wav", g_rec_base);
                    if (wav_open(&g_mic_wav, path, rate, ch) != 0) {
                        plog(MPL_LOG_ERROR, "[voice-link] cannot open mic wav");
                        g_rec_stop_req.store(1, std::memory_order_relaxed);
                    }
                }
                uint32_t contig = (w - r) & REC_RING_MASK;
                uint32_t first = REC_RING_BYTES - r;
                if (first > contig) first = contig;
                if (g_mic_wav.valid) {
                    wav_write_f32(&g_mic_wav, (const float*)(g_rec_ring + r), first / 4);
                    if (contig > first) {
                        uint32_t off = first / 4;
                        wav_write_f32(&g_mic_wav, (const float*)g_rec_ring, (contig - first) / 4);
                        (void)off;
                    }
                }
                g_rec_r.store((r + contig) & REC_RING_MASK, std::memory_order_release);
                ULONG64 now = GetTickCount64();
                if (now - last_flush_ms > 1000) {
                    last_flush_ms = now;
                    if (g_mic_wav.valid) fflush(g_mic_wav.f);
                }
            } else {
                // watchdog: mic frames stopped arriving (host quit / link dead)
                if (g_mic_frames_seen.load(std::memory_order_relaxed) &&
                    GetTickCount64() - g_last_mic_frame_ms.load(std::memory_order_relaxed) > 3000) {
                    plog(MPL_LOG_WARN, "[voice-link] mic audio timeout, stopping recording");
                    g_rec_stop_req.store(1, std::memory_order_relaxed);
                }
                Sleep(10);
            }
        }
        if (g_rec_stop_req.load(std::memory_order_relaxed)) {
            char base[MAX_PATH];
            EnterCriticalSection(&g_rec_cs);
            strncpy(base, g_rec_base, sizeof(base) - 1);
            base[sizeof(base) - 1] = 0;
            g_recording.store(0, std::memory_order_release);
            g_sys_active.store(0, std::memory_order_relaxed);
            // drain whatever is left in the ring
            uint32_t r = g_rec_r.load(std::memory_order_relaxed);
            uint32_t w = g_rec_w.load(std::memory_order_acquire);
            if (g_mic_wav.valid && w != r) {
                uint32_t contig = (w - r) & REC_RING_MASK;
                uint32_t first = REC_RING_BYTES - r;
                if (first > contig) first = contig;
                wav_write_f32(&g_mic_wav, (const float*)(g_rec_ring + r), first / 4);
                if (contig > first) {
                    wav_write_f32(&g_mic_wav, (const float*)g_rec_ring, (contig - first) / 4);
                }
            }
            wav_finalize(&g_mic_wav);
            int dropped = g_rec_dropped_bytes.load(std::memory_order_relaxed);
            if (dropped > 0) {
                char msg[96];
                snprintf(msg, sizeof(msg), "[voice-link] recording dropped %d ring bytes", dropped);
                plog(MPL_LOG_WARN, msg);
            }
            LeaveCriticalSection(&g_rec_cs);
            WaitForSingleObject(g_sys_done_evt, 3000);
            mix_tracks(base);
            char msg[320];
            snprintf(msg, sizeof(msg), "[voice-link] REC_STOP -> %s_{mic,sys,mix}.wav", base);
            plog(MPL_LOG_INFO, msg);
            g_rec_stop_req.store(0, std::memory_order_relaxed);
            SetEvent(g_rec_idle_evt);
        }
        if (!g_recording.load(std::memory_order_relaxed) &&
            !g_rec_stop_req.load(std::memory_order_relaxed)) {
            Sleep(20);
        }
    }
    return 0;
}

// ---- UDP listener thread ----
static HANDLE g_thread = NULL;
static HANDLE g_writer_thread = NULL;
static HANDLE g_sys_thread = NULL;

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
        if (!strncmp(buf, "REC_START", 9)) {
            rec_start();
        } else if (!strncmp(buf, "REC_STOP", 8)) {
            plog(MPL_LOG_INFO, "[voice-link] REC_STOP");
            rec_stop(1);
        } else if (!strncmp(buf, "KEY_SEND", 8)) {
            int idx = atoi(buf + 8);
            const WORD* combo = g_start_combo;
            int count = g_start_n;
            const char* name = "VOICE";
            if (idx == 1) { combo = g_enter_combo; count = g_enter_n; name = "ENTER"; }
            else if (idx == 2) { combo = g_delete_combo; count = g_delete_n; name = "DEL"; }
            else if (idx != 0) { continue; }
            char msg[64];
            snprintf(msg, sizeof(msg), "[voice-link] KEY_SEND %d (%s)", idx, name);
            plog(MPL_LOG_INFO, msg);
            press_combo(combo, count, 0);
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
    g_audio_state = host->audio_state;
    g_ctx = host->ctx;

    read_cfg_str("startKeys", g_start_keys, sizeof(g_start_keys));
    read_cfg_str("stopKeys", g_stop_keys, sizeof(g_stop_keys));
    read_cfg_str("holdKeys", g_hold_keys, sizeof(g_hold_keys));
    read_cfg_str("enterKeys", g_enter_keys, sizeof(g_enter_keys));
    read_cfg_str("deleteKeys", g_delete_keys, sizeof(g_delete_keys));
    read_cfg_str("recDir", g_rec_dir, sizeof(g_rec_dir));
    g_mic_gain = read_cfg_float("micGain", 1.0f);
    g_sys_gain = read_cfg_float("sysGain", 1.0f);
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
    InitializeCriticalSection(&g_rec_cs);
    g_rec_idle_evt = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_sys_done_evt = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_thread = CreateThread(NULL, 0, udp_thread, NULL, 0, NULL);
    g_writer_thread = CreateThread(NULL, 0, rec_writer_thread, NULL, 0, NULL);
    g_sys_thread = CreateThread(NULL, 0, sys_thread, NULL, 0, NULL);
    if (!g_thread || !g_writer_thread || !g_sys_thread) {
        plog(MPL_LOG_ERROR, "[voice-link] CreateThread failed");
        return MPL_ERR_RUNTIME;
    }
    plog(MPL_LOG_INFO, "[voice-link] plugin started");
    return MPL_OK;
}

extern "C" MPL_EXPORT void micyou_plugin_deinit(void) {
    // Best effort: finalize any active recording before the process goes away.
    if (g_recording.load()) {
        rec_stop(1);
    }
    g_stop = 1;
    if (g_thread) {
        WaitForSingleObject(g_thread, 1500);  // recv timeout is 200 ms
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_writer_thread) {
        WaitForSingleObject(g_writer_thread, 1000);
        CloseHandle(g_writer_thread);
        g_writer_thread = NULL;
    }
    if (g_sys_thread) {
        WaitForSingleObject(g_sys_thread, 1000);
        CloseHandle(g_sys_thread);
        g_sys_thread = NULL;
    }
    DeleteCriticalSection(&g_rec_cs);
}
