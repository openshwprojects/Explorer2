#pragma once
// linux/win32_hal/include/mmsystem.h - Multimedia Audio Device Enumeration HAL for Linux.

#include <windows.h>

using MMRESULT = UINT;
#define MMSYSERR_NOERROR 0u
#define MAXPNAMELEN 32

struct WAVEOUTCAPSW {
    WORD wMid;
    WORD wPid;
    UINT vDriverVersion;
    WCHAR szPname[MAXPNAMELEN];
    DWORD dwFormats;
    WORD wChannels;
    WORD wReserved1;
    DWORD dwSupport;
};

struct WAVEINCAPSW {
    WORD wMid;
    WORD wPid;
    UINT vDriverVersion;
    WCHAR szPname[MAXPNAMELEN];
    DWORD dwFormats;
    WORD wChannels;
    WORD wReserved1;
};

extern "C" {
UINT waveOutGetNumDevs();
MMRESULT waveOutGetDevCapsW(UINT_PTR uDeviceID, WAVEOUTCAPSW* pwoc, UINT cbwoc);
UINT waveInGetNumDevs();
MMRESULT waveInGetDevCapsW(UINT_PTR uDeviceID, WAVEINCAPSW* pwic, UINT cbwic);
}
