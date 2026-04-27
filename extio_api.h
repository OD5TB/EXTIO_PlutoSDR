// extio_api.h — ExtIO ABI used by HDSDR / Winrad-derived SDR software.
// Reference: Alberto di Bene I2PHD's original ExtIO specification.

#pragma once

#include <windows.h>

// Hardware types passed back through InitHW's `hwtype` out-parameter.
// We use exthwUSBfloat32: ExtIO callback delivers IQ as interleaved float32 in [-1.0, +1.0].
#define exthwSDR14         1
#define exthwSDRX          2
#define exthwUSBdata16     3
#define exthwSCdata        4
#define exthwUSBdata24     5
#define exthwUSBdata32     6
#define exthwUSBfloat32    7

// Status codes (passed via the `status` argument when `cnt < 0`).
#define extHw_LO_CHANGED        100
#define extHw_SR_CHANGED        101
#define extHw_TUNE_CHANGED      103
#define extHw_MODE_CHANGED      105
#define extHw_FILTER_CHANGED    108
#define extHw_PLL_LOCKED        109
#define extHw_PLL_UNLOCKED      110

#ifdef __cplusplus
extern "C" {
#endif

// IMPORTANT: The host's callback is plain __cdecl, NOT __stdcall.
typedef int (*pfnExtIOCallback)(int cnt, int status, float IQoffs, void* IQdata);

#ifdef __cplusplus
}
#endif
