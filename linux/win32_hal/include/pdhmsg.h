#pragma once
// linux/win32_hal/include/pdhmsg.h - PDH status codes.

#include <pdh.h>

#define PDH_CSTATUS_VALID_DATA 0x00000000u
#define PDH_CSTATUS_NEW_DATA   0x00000001u
#define PDH_MORE_DATA          (static_cast<PDH_STATUS>(0x800007D2u))
#define PDH_NO_DATA            (static_cast<PDH_STATUS>(0x800007D5u))
