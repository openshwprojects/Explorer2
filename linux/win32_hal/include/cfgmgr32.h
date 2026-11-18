#pragma once
// linux/win32_hal/include/cfgmgr32.h - PnP Configuration Manager (USB & Serial Topology) HAL for Linux.

#include <devpropdef.h>

using CONFIGRET = DWORD;
using DEVINST = DWORD;
using PDEVINST = DEVINST*;
using REGDISPOSITION = ULONG;

#define CR_SUCCESS 0x00000000u
#define CR_NO_SUCH_DEVNODE 0x0000000Du
#define CR_NO_SUCH_VALUE 0x00000025u
#define CR_FAILURE 0x00000013u

#define CM_LOCATE_DEVNODE_NORMAL 0x00000000u
#define CM_REGISTRY_HARDWARE     0x00000000u
#define RegDisposition_OpenExisting 0x00000001u

#define CM_DRP_DEVICEDESC                  0x00000001u
#define CM_DRP_HARDWAREID                  0x00000002u
#define CM_DRP_SERVICE                     0x00000005u
#define CM_DRP_CLASS                       0x00000008u
#define CM_DRP_DRIVER                      0x0000000Au
#define CM_DRP_MFG                         0x0000000Cu
#define CM_DRP_FRIENDLYNAME                0x0000000Du
#define CM_DRP_LOCATION_INFORMATION        0x0000000Eu
#define CM_DRP_PHYSICAL_DEVICE_OBJECT_NAME 0x0000000Fu

#define DN_STARTED 0x00000008u

#define CM_PROB_NOT_CONFIGURED     0x00000001u
#define CM_PROB_FAILED_START       0x0000000Au
#define CM_PROB_NORMAL_CONFLICT    0x0000000Cu
#define CM_PROB_NEED_RESTART       0x0000000Eu
#define CM_PROB_REINSTALL          0x00000012u
#define CM_PROB_DISABLED           0x00000016u
#define CM_PROB_FAILED_INSTALL     0x0000001Cu
#define CM_PROB_FAILED_ADD         0x0000001Fu
#define CM_PROB_DRIVER_FAILED_LOAD 0x00000027u
#define CM_PROB_FAILED_POST_START  0x0000002Bu
#define CM_PROB_PHANTOM            0x0000002Du
#define CM_PROB_UNSIGNED_DRIVER    0x00000034u

extern "C" {
CONFIGRET CM_Locate_DevNodeW(PDEVINST pdnDevInst, LPWSTR pDeviceID, ULONG ulFlags);
CONFIGRET CM_Get_Child(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags);
CONFIGRET CM_Get_Sibling(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags);
CONFIGRET CM_Get_Parent(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags);
CONFIGRET CM_Get_Device_IDW(DEVINST dnDevInst, PWSTR Buffer, ULONG BufferLen, ULONG ulFlags);
CONFIGRET CM_Get_DevNode_Registry_PropertyW(DEVINST dnDevInst, ULONG ulProperty, PULONG pulRegDataType,
                                            PVOID Buffer, PULONG pulLength, ULONG ulFlags);
CONFIGRET CM_Get_DevNode_PropertyW(DEVINST dnDevInst, const DEVPROPKEY* PropertyKey, DEVPROPTYPE* PropertyType,
                                   PBYTE PropertyBuffer, PULONG PropertyBufferSize, ULONG ulFlags);
CONFIGRET CM_Get_DevNode_Status(PULONG pulStatus, PULONG pulProblemNumber, DEVINST dnDevInst, ULONG ulFlags);
CONFIGRET CM_Open_DevNode_Key(DEVINST dnDevNode, REGSAM samDesired, ULONG ulHardwareProfile,
                              REGDISPOSITION Disposition, PHKEY phkDevice, ULONG ulFlags);
}
