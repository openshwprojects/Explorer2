#pragma once
// linux/win32_hal/include/devpropdef.h - Device Property Key & Type definitions for Linux HAL.

#include <windows.h>

using DEVPROPGUID = GUID;
using DEVPROPID = ULONG;
using DEVPROPTYPE = ULONG;

struct DEVPROPKEY {
    DEVPROPGUID fmtid;
    DEVPROPID pid;
};

#define DEVPROP_TYPE_EMPTY  0x00000000u
#define DEVPROP_TYPE_STRING 0x00000012u
