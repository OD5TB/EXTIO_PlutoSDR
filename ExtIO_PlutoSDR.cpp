// ExtIO_PlutoSDR.cpp
// ----------------------------------------------------------------------------
// A minimal-but-functional ExtIO DLL for the ADALM-Pluto / PlutoPlus.
// Targets HDSDR (Winrad-style ExtIO ABI), built as Win32 (x86).
//
// Talks to the device through libiio. RX-only.
// ----------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include <iio.h>

#include "extio_api.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")

// ---------------------------------------------------------------------------
// Configuration tables
// ---------------------------------------------------------------------------

struct SampleRate { double hz; const char* label; };
static const SampleRate kSampleRates[] = {
    {  521000.0,  "521 kS/s" },
    { 1024000.0,  "1.024 MS/s" },
    { 2048000.0,  "2.048 MS/s" },
    { 2400000.0,  "2.4 MS/s" },
    { 3200000.0,  "3.2 MS/s" },
    { 4000000.0,  "4 MS/s" },
    { 5000000.0,  "5 MS/s" },
    { 6000000.0,  "6 MS/s" },
    { 8000000.0,  "8 MS/s" },
    {10000000.0, "10 MS/s" },
};
static const int kNumSampleRates = (int)(sizeof(kSampleRates)/sizeof(kSampleRates[0]));

struct Bandwidth { long hz; const char* label; };
static const Bandwidth kBandwidths[] = {
    {  200000, "200 kHz" },
    {  500000, "500 kHz" },
    { 1000000, "1 MHz" },
    { 1500000, "1.5 MHz" },
    { 2000000, "2 MHz" },
    { 3000000, "3 MHz" },
    { 4000000, "4 MHz" },
    { 5000000, "5 MHz" },
    { 6000000, "6 MHz" },
    { 8000000, "8 MHz" },
};
static const int kNumBandwidths = (int)(sizeof(kBandwidths)/sizeof(kBandwidths[0]));

static const char* kGainModes[] = {"manual", "slow_attack", "fast_attack", "hybrid"};
static const int   kNumGainModes = 4;

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

static HMODULE         g_hModule = nullptr;
static HWND            g_hDlg    = nullptr;
static pfnExtIOCallback g_cb     = nullptr;

// Pluto handles
static iio_context*    g_ctx      = nullptr;
static iio_device*     g_phy      = nullptr;   // ad9361-phy (control)
static iio_device*     g_rxdev    = nullptr;   // cf-ad9361-lpc (data)
static iio_channel*    g_rx_phy   = nullptr;   // voltage0 on phy (RX path config)
static iio_channel*    g_rx_lo    = nullptr;   // altvoltage0 on phy (RX LO)
static iio_channel*    g_chI      = nullptr;   // voltage0 on rxdev (I)
static iio_channel*    g_chQ      = nullptr;   // voltage1 on rxdev (Q)
static iio_buffer*     g_rxbuf    = nullptr;

// Streaming
static std::thread     g_thread;
static std::atomic<bool> g_running{false};
static std::mutex      g_devMutex;        // protects the libiio handles
static const size_t    kBufSampleSets = 4096;  // 4096 IQ pairs per refill

// User settings (mutable via the dialog)
static char  g_uri[128]   = "ip:192.168.2.1";
static long  g_freqHz     = 100000000;   // current LO
static int   g_srIdx      = 3;           // 2.4 MS/s
static int   g_bwIdx      = 4;           // 2 MHz
static int   g_gainModeIdx = 1;          // slow_attack
static int   g_manualGain = 40;          // dB
static int   g_ppm        = 0;

// Status text shown in the dialog
static char  g_statusBuf[256] = "idle";

static void setStatus(const char* msg) {
    strncpy_s(g_statusBuf, msg, _TRUNCATE);
    if (g_hDlg) {
        char line[300];
        _snprintf_s(line, _TRUNCATE, "Status: %s", msg);
        SetDlgItemTextA(g_hDlg, IDC_STATUS, line);
    }
}

// ---------------------------------------------------------------------------
// libiio helpers
// ---------------------------------------------------------------------------

static bool plutoApplyTuning() {
    if (!g_rx_lo) return false;
    return iio_channel_attr_write_longlong(g_rx_lo, "frequency", (long long)g_freqHz) >= 0;
}

static bool plutoApplyAll() {
    if (!g_rx_phy || !g_rx_lo) return false;
    bool ok = true;

    if (iio_channel_attr_write_longlong(g_rx_phy, "sampling_frequency",
                                        (long long)kSampleRates[g_srIdx].hz) < 0) ok = false;
    if (iio_channel_attr_write_longlong(g_rx_phy, "rf_bandwidth",
                                        kBandwidths[g_bwIdx].hz) < 0) ok = false;
    if (iio_channel_attr_write(g_rx_phy, "gain_control_mode",
                               kGainModes[g_gainModeIdx]) < 0) ok = false;
    if (g_gainModeIdx == 0) {
        if (iio_channel_attr_write_longlong(g_rx_phy, "hardwaregain",
                                            (long long)g_manualGain) < 0) ok = false;
    }
    if (g_ppm != 0) {
        long long base_xo = 40000000;
        if (iio_device_attr_read_longlong(g_phy, "xo_correction", &base_xo) >= 0) {
            long long corrected = base_xo + (long long)((double)base_xo * g_ppm / 1.0e6);
            iio_device_attr_write_longlong(g_phy, "xo_correction", corrected);
        }
    }
    if (!plutoApplyTuning()) ok = false;
    return ok;
}

static void plutoClose() {
    std::lock_guard<std::mutex> lk(g_devMutex);
    if (g_rxbuf) { iio_buffer_destroy(g_rxbuf); g_rxbuf = nullptr; }
    if (g_chI) { iio_channel_disable(g_chI); }
    if (g_chQ) { iio_channel_disable(g_chQ); }
    if (g_ctx) { iio_context_destroy(g_ctx); g_ctx = nullptr; }
    g_phy = g_rxdev = nullptr;
    g_rx_phy = g_rx_lo = g_chI = g_chQ = nullptr;
}

static bool plutoOpen() {
    std::lock_guard<std::mutex> lk(g_devMutex);
    if (g_ctx) return true;

    g_ctx = iio_create_context_from_uri(g_uri);
    if (!g_ctx) {
        setStatus("iio_create_context_from_uri failed — check URI / driver");
        return false;
    }

    g_phy   = iio_context_find_device(g_ctx, "ad9361-phy");
    g_rxdev = iio_context_find_device(g_ctx, "cf-ad9361-lpc");
    if (!g_phy || !g_rxdev) {
        setStatus("Pluto devices not found (ad9361-phy / cf-ad9361-lpc)");
        iio_context_destroy(g_ctx); g_ctx = nullptr;
        return false;
    }

    g_rx_phy = iio_device_find_channel(g_phy,  "voltage0",   false);
    g_rx_lo  = iio_device_find_channel(g_phy,  "altvoltage0", true);
    g_chI    = iio_device_find_channel(g_rxdev,"voltage0",   false);
    g_chQ    = iio_device_find_channel(g_rxdev,"voltage1",   false);
    if (!g_rx_phy || !g_rx_lo || !g_chI || !g_chQ) {
        setStatus("Required channels missing on Pluto");
        iio_context_destroy(g_ctx); g_ctx = nullptr;
        return false;
    }

    iio_channel_enable(g_chI);
    iio_channel_enable(g_chQ);

    setStatus("connected");
    return true;
}

// ---------------------------------------------------------------------------
// Streaming thread
// ---------------------------------------------------------------------------

static void streamThread() {
    std::vector<float> floatBuf;
    floatBuf.resize(kBufSampleSets * 2);  // I,Q interleaved

    while (g_running.load()) {
        ssize_t n = -1;
        {
            std::lock_guard<std::mutex> lk(g_devMutex);
            if (!g_rxbuf) break;
            n = iio_buffer_refill(g_rxbuf);
        }
        if (n < 0) {
            setStatus("iio_buffer_refill failed");
            Sleep(10);
            continue;
        }

        // Walk the buffer using iio_buffer_first/step — this is the portable way
        // to iterate over enabled channels regardless of stride/order.
        char* p_start;
        char* p_end;
        ptrdiff_t step;
        {
            std::lock_guard<std::mutex> lk(g_devMutex);
            if (!g_rxbuf || !g_chI) break;
            p_start = (char*)iio_buffer_first(g_rxbuf, g_chI);
            p_end   = (char*)iio_buffer_end(g_rxbuf);
            step    = iio_buffer_step(g_rxbuf);
        }

        size_t k = 0;
        const float scale = 1.0f / 2048.0f;   // 12-bit signed → [-1, +1)
        for (char* p = p_start; p < p_end && k + 1 < floatBuf.size(); p += step) {
            int16_t iv = ((int16_t*)p)[0];
            int16_t qv = ((int16_t*)p)[1];
            floatBuf[k++] = iv * scale;
            floatBuf[k++] = qv * scale;
        }

        if (g_cb && k > 0) {
            // ExtIO callback: cnt is number of IQ pairs.
            g_cb((int)(k / 2), 0, 0.0f, floatBuf.data());
        }
    }
}

static bool startStreaming() {
    {
        std::lock_guard<std::mutex> lk(g_devMutex);
        if (!g_ctx || !g_rxdev) return false;
        if (g_rxbuf) return true;
        g_rxbuf = iio_device_create_buffer(g_rxdev, kBufSampleSets, false);
        if (!g_rxbuf) {
            setStatus("iio_device_create_buffer failed");
            return false;
        }
    }
    g_running.store(true);
    g_thread = std::thread(streamThread);
    setStatus("streaming");
    return true;
}

static void stopStreaming() {
    g_running.store(false);
    if (g_thread.joinable()) g_thread.join();
    std::lock_guard<std::mutex> lk(g_devMutex);
    if (g_rxbuf) { iio_buffer_destroy(g_rxbuf); g_rxbuf = nullptr; }
    setStatus("stopped");
}

// ---------------------------------------------------------------------------
// Settings dialog
// ---------------------------------------------------------------------------

static void populateCombos(HWND hDlg) {
    HWND srBox = GetDlgItem(hDlg, IDC_SAMPLERATE);
    SendMessage(srBox, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < kNumSampleRates; ++i)
        SendMessageA(srBox, CB_ADDSTRING, 0, (LPARAM)kSampleRates[i].label);
    SendMessage(srBox, CB_SETCURSEL, g_srIdx, 0);

    HWND bwBox = GetDlgItem(hDlg, IDC_BANDWIDTH);
    SendMessage(bwBox, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < kNumBandwidths; ++i)
        SendMessageA(bwBox, CB_ADDSTRING, 0, (LPARAM)kBandwidths[i].label);
    SendMessage(bwBox, CB_SETCURSEL, g_bwIdx, 0);

    HWND gmBox = GetDlgItem(hDlg, IDC_GAINMODE);
    SendMessage(gmBox, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < kNumGainModes; ++i)
        SendMessageA(gmBox, CB_ADDSTRING, 0, (LPARAM)kGainModes[i]);
    SendMessage(gmBox, CB_SETCURSEL, g_gainModeIdx, 0);

    SetDlgItemTextA(hDlg, IDC_URI, g_uri);
    SetDlgItemInt(hDlg, IDC_GAIN, g_manualGain, TRUE);
    SetDlgItemInt(hDlg, IDC_PPM, g_ppm, TRUE);
}

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        g_hDlg = hDlg;
        populateCombos(hDlg);
        setStatus(g_statusBuf);
        return TRUE;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_CONNECT && code == BN_CLICKED) {
            char tmp[128] = {0};
            GetDlgItemTextA(hDlg, IDC_URI, tmp, sizeof(tmp));
            if (tmp[0]) strncpy_s(g_uri, tmp, _TRUNCATE);
            if (plutoOpen()) plutoApplyAll();
            return TRUE;
        }
        if (id == IDC_DISCONNECT && code == BN_CLICKED) {
            stopStreaming();
            plutoClose();
            setStatus("disconnected");
            return TRUE;
        }
        if (id == IDC_APPLY && code == BN_CLICKED) {
            g_srIdx       = (int)SendMessage(GetDlgItem(hDlg, IDC_SAMPLERATE), CB_GETCURSEL, 0, 0);
            g_bwIdx       = (int)SendMessage(GetDlgItem(hDlg, IDC_BANDWIDTH),  CB_GETCURSEL, 0, 0);
            g_gainModeIdx = (int)SendMessage(GetDlgItem(hDlg, IDC_GAINMODE),   CB_GETCURSEL, 0, 0);
            if (g_srIdx < 0) g_srIdx = 0;
            if (g_bwIdx < 0) g_bwIdx = 0;
            if (g_gainModeIdx < 0) g_gainModeIdx = 1;
            g_manualGain = (int)GetDlgItemInt(hDlg, IDC_GAIN, nullptr, TRUE);
            g_ppm        = (int)GetDlgItemInt(hDlg, IDC_PPM,  nullptr, TRUE);

            if (g_ctx) {
                plutoApplyAll();
                if (g_cb) {
                    g_cb(-1, extHw_SR_CHANGED, 0.0f, nullptr);
                    g_cb(-1, extHw_LO_CHANGED, 0.0f, nullptr);
                }
            }
            return TRUE;
        }
        if (id == IDCANCEL) {
            ShowWindow(hDlg, SW_HIDE);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        ShowWindow(hDlg, SW_HIDE);
        return TRUE;

    case WM_DESTROY:
        g_hDlg = nullptr;
        return TRUE;
    }
    return FALSE;
}

static void ensureDialog() {
    if (g_hDlg) return;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    g_hDlg = CreateDialogParam(g_hModule, MAKEINTRESOURCE(IDD_SETTINGS),
                               nullptr, DlgProc, 0);
}

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hMod;
        DisableThreadLibraryCalls(hMod);
    } else if (reason == DLL_PROCESS_DETACH) {
        // If lpReserved is non-NULL the process is terminating: do NOT join the
        // streaming thread or touch GUI/USB handles — the loader lock is held
        // and other threads may already be dead. Rely on CloseHW() being called
        // by the host beforehand.
        if (lpReserved == nullptr) {
            stopStreaming();
            plutoClose();
            if (g_hDlg) { DestroyWindow(g_hDlg); g_hDlg = nullptr; }
        }
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// ExtIO ABI exports — names kept undecorated via the .def file.
// ---------------------------------------------------------------------------

extern "C" {

bool __stdcall InitHW(char* name, char* model, int& hwtype) {
    if (name)  strcpy_s(name, 16, "PlutoSDR");
    if (model) strcpy_s(model, 16, "ADALM-Pluto");
    hwtype = exthwUSBfloat32;
    return true;
}

bool __stdcall OpenHW(void) {
    ensureDialog();
    return true;   // Dialog Connect button does the real work
}

void __stdcall CloseHW(void) {
    stopStreaming();
    plutoClose();
    if (g_hDlg) { DestroyWindow(g_hDlg); g_hDlg = nullptr; }
}

int __stdcall StartHW(long extLOfreq) {
    g_freqHz = extLOfreq;
    if (!g_ctx && !plutoOpen()) return -1;
    plutoApplyAll();
    if (!startStreaming()) return -1;
    return (int)kBufSampleSets;   // # IQ pairs per callback
}

void __stdcall StopHW(void) {
    stopStreaming();
}

int __stdcall SetHWLO(long extLOfreq) {
    g_freqHz = extLOfreq;
    if (g_ctx) plutoApplyTuning();
    if (g_cb) g_cb(-1, extHw_LO_CHANGED, 0.0f, nullptr);
    return 0;
}

long __stdcall GetHWLO(void) { return g_freqHz; }

long __stdcall GetHWSR(void) {
    return (long)kSampleRates[g_srIdx].hz;
}

int __stdcall GetStatus(void) { return 0; }

void __stdcall SetCallback(pfnExtIOCallback fn) { g_cb = fn; }

void __stdcall ShowGUI(void) {
    ensureDialog();
    if (g_hDlg) ShowWindow(g_hDlg, SW_SHOW);
}

void __stdcall HideGUI(void) {
    if (g_hDlg) ShowWindow(g_hDlg, SW_HIDE);
}

void __stdcall SwitchGUI(void) {
    ensureDialog();
    if (!g_hDlg) return;
    ShowWindow(g_hDlg, IsWindowVisible(g_hDlg) ? SW_HIDE : SW_SHOW);
}

// Sample-rate enumeration ---------------------------------------------------

int __stdcall ExtIoGetSrates(int idx, double* samplerate) {
    if (idx < 0 || idx >= kNumSampleRates) return -1;
    if (samplerate) *samplerate = kSampleRates[idx].hz;
    return 0;
}

int __stdcall ExtIoGetActualSrateIdx(void) { return g_srIdx; }

int __stdcall ExtIoSetSrate(int idx) {
    if (idx < 0 || idx >= kNumSampleRates) return -1;
    g_srIdx = idx;
    if (g_ctx && g_rx_phy)
        iio_channel_attr_write_longlong(g_rx_phy, "sampling_frequency",
                                        (long long)kSampleRates[idx].hz);
    if (g_cb) g_cb(-1, extHw_SR_CHANGED, 0.0f, nullptr);
    return 0;
}

long __stdcall ExtIoGetBandwidth(int srate_idx) {
    // Report the closest sensible analog bandwidth.
    if (srate_idx >= 0 && srate_idx < kNumSampleRates)
        return (long)(kSampleRates[srate_idx].hz * 0.8);
    return (long)(kSampleRates[g_srIdx].hz * 0.8);
}

// Manual gain (dB) — Pluto RX hardwaregain ranges roughly -1..73 dB.
int __stdcall ExtIoGetMGCs(int idx, float* gain) {
    if (idx < 0 || idx > 73) return -1;
    if (gain) *gain = (float)idx;
    return 0;
}

int __stdcall ExtIoGetActualMgcIdx(void) { return g_manualGain; }

int __stdcall ExtIoSetMGC(int idx) {
    if (idx < 0 || idx > 73) return -1;
    g_manualGain = idx;
    if (g_ctx && g_rx_phy && g_gainModeIdx == 0)
        iio_channel_attr_write_longlong(g_rx_phy, "hardwaregain", (long long)idx);
    return 0;
}

// AGC mode list -------------------------------------------------------------
int __stdcall ExtIoGetAGCs(int idx, char* text) {
    if (idx < 0 || idx >= kNumGainModes) return -1;
    if (text) strcpy_s(text, 16, kGainModes[idx]);
    return 0;
}

int __stdcall ExtIoGetActualAGCidx(void) { return g_gainModeIdx; }

int __stdcall ExtIoSetAGC(int idx) {
    if (idx < 0 || idx >= kNumGainModes) return -1;
    g_gainModeIdx = idx;
    if (g_ctx && g_rx_phy)
        iio_channel_attr_write(g_rx_phy, "gain_control_mode", kGainModes[idx]);
    return 0;
}

// Settings persistence (key/value strings — HDSDR uses these) ---------------
int __stdcall ExtIoGetSetting(int idx, char* description, char* value) {
    switch (idx) {
        case 0: strcpy_s(description, 1024, "URI");
                strcpy_s(value, 1024, g_uri); return 0;
        case 1: strcpy_s(description, 1024, "SampleRateIdx");
                _itoa_s(g_srIdx, value, 1024, 10); return 0;
        case 2: strcpy_s(description, 1024, "BandwidthIdx");
                _itoa_s(g_bwIdx, value, 1024, 10); return 0;
        case 3: strcpy_s(description, 1024, "GainModeIdx");
                _itoa_s(g_gainModeIdx, value, 1024, 10); return 0;
        case 4: strcpy_s(description, 1024, "ManualGain");
                _itoa_s(g_manualGain, value, 1024, 10); return 0;
        case 5: strcpy_s(description, 1024, "PPM");
                _itoa_s(g_ppm, value, 1024, 10); return 0;
        default: return -1;
    }
}

void __stdcall ExtIoSetSetting(int idx, const char* value) {
    if (!value) return;
    switch (idx) {
        case 0: strncpy_s(g_uri, value, _TRUNCATE); break;
        case 1: g_srIdx       = atoi(value); break;
        case 2: g_bwIdx       = atoi(value); break;
        case 3: g_gainModeIdx = atoi(value); break;
        case 4: g_manualGain  = atoi(value); break;
        case 5: g_ppm         = atoi(value); break;
    }
}

const char* __stdcall VersionInfo(const char* progname, int verMajor, int verMinor) {
    (void)progname; (void)verMajor; (void)verMinor;
    return "ExtIO_PlutoSDR 0.1 (libiio)";
}

}  // extern "C"
