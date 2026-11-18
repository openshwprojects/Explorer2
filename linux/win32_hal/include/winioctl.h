#pragma once
// linux/win32_hal/include/winioctl.h - Storage, Sparse, Reparse, and SMART IOCTL definitions for Linux HAL.

#include <windows.h>

#define FSCTL_SET_SPARSE             0x000900C4u
#define FSCTL_SET_REPARSE_POINT      0x000900A4u
#define FSCTL_GET_REPARSE_POINT      0x000900A8u
#define FSCTL_DELETE_REPARSE_POINT   0x000900ACu
#define FSCTL_QUERY_ALLOCATED_RANGES 0x000940CFu
#define FSCTL_DUPLICATE_EXTENTS_TO_FILE 0x00098344u

#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE (16 * 1024)

struct FILE_ALLOCATED_RANGE_BUFFER {
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER Length;
};

struct DUPLICATE_EXTENTS_DATA {
    HANDLE FileHandle;
    LARGE_INTEGER SourceFileOffset;
    LARGE_INTEGER TargetFileOffset;
    LARGE_INTEGER ByteCount;
};

// Storage Queries & SMART
#define IOCTL_STORAGE_QUERY_PROPERTY 0x002D1400u
#define IOCTL_DISK_GET_DRIVE_GEOMETRY_EX 0x000700A0u
#define SMART_GET_VERSION 0x00074080u
#define SMART_SEND_DRIVE_COMMAND 0x0007C084u
#define SMART_RCV_DRIVE_DATA 0x0007C088u

enum STORAGE_PROPERTY_ID {
    StorageDeviceProperty = 0,
    StorageAdapterProperty = 1,
    StorageDeviceIdProperty = 2,
    StorageDeviceSeekPenaltyProperty = 7,
    StorageDeviceTrimProperty = 8,
    StorageAdapterProtocolSpecificProperty = 49,
    StorageDeviceProtocolSpecificProperty = 50,
    StorageDeviceTemperatureProperty = 52
};

enum STORAGE_QUERY_TYPE {
    PropertyStandardQuery = 0,
    PropertyExistsQuery = 1
};

struct STORAGE_PROPERTY_QUERY {
    STORAGE_PROPERTY_ID PropertyId;
    STORAGE_QUERY_TYPE QueryType;
    BYTE AdditionalParameters[1];
};

enum STORAGE_BUS_TYPE {
    BusTypeUnknown = 0x00,
    BusTypeScsi = 0x1,
    BusTypeAtapi = 0x2,
    BusTypeAta = 0x3,
    BusType1394 = 0x4,
    BusTypeSsa = 0x5,
    BusTypeFibre = 0x6,
    BusTypeUsb = 0x7,
    BusTypeRAID = 0x8,
    BusTypeiScsi = 0x9,
    BusTypeSas = 0xA,
    BusTypeSata = 0xB,
    BusTypeSd = 0xC,
    BusTypeMmc = 0xD,
    BusTypeVirtual = 0xE,
    BusTypeFileBackedVirtual = 0xF,
    BusTypeSpaces = 0x10,
    BusTypeNvme = 0x11,
    BusTypeSCM = 0x12,
    BusTypeUfs = 0x13
};

struct STORAGE_DEVICE_DESCRIPTOR {
    DWORD Version;
    DWORD Size;
    BYTE DeviceType;
    BYTE DeviceTypeModifier;
    BYTE RemovableMedia;
    BYTE CommandQueueing;
    DWORD VendorIdOffset;
    DWORD ProductIdOffset;
    DWORD ProductRevisionOffset;
    DWORD SerialNumberOffset;
    STORAGE_BUS_TYPE BusType;
    DWORD RawPropertiesLength;
    BYTE RawDeviceProperties[1];
};

struct DEVICE_SEEK_PENALTY_DESCRIPTOR {
    DWORD Version;
    DWORD Size;
    BYTE IncursSeekPenalty;
};

struct DEVICE_TRIM_DESCRIPTOR {
    DWORD Version;
    DWORD Size;
    BYTE TrimEnabled;
};

struct STORAGE_TEMPERATURE_INFO {
    WORD Index;
    SHORT Temperature;
    SHORT OverThreshold;
    SHORT UnderThreshold;
    BYTE OverThresholdChangable;
    BYTE UnderThresholdChangable;
    BYTE EventGenerated;
    BYTE Reserved0;
    DWORD Reserved1;
};

struct STORAGE_TEMPERATURE_DATA_DESCRIPTOR {
    DWORD Version;
    DWORD Size;
    SHORT CriticalTemperature;
    SHORT WarningTemperature;
    WORD InfoCount;
    BYTE Reserved0[2];
    DWORD Reserved1[2];
    STORAGE_TEMPERATURE_INFO TemperatureInfo[1];
};

enum STORAGE_PROTOCOL_TYPE {
    ProtocolTypeUnknown = 0,
    ProtocolTypeScsi = 1,
    ProtocolTypeAta = 2,
    ProtocolTypeNvme = 3,
    ProtocolTypeSd = 4
};

enum STORAGE_PROTOCOL_NVME_DATA_TYPE {
    NVMeDataTypeUnknown = 0,
    NVMeDataTypeIdentify = 1,
    NVMeDataTypeLogPage = 2,
    NVMeDataTypeFeature = 3
};

struct STORAGE_PROTOCOL_SPECIFIC_DATA {
    STORAGE_PROTOCOL_TYPE ProtocolType;
    DWORD DataType;
    DWORD ProtocolDataRequestValue;
    DWORD ProtocolDataRequestSubValue;
    DWORD ProtocolDataOffset;
    DWORD ProtocolDataLength;
    DWORD FixedProtocolReturnData;
    DWORD ProtocolDataRequestSubValue2;
    DWORD ProtocolDataRequestSubValue3;
    DWORD Reserved;
};

struct STORAGE_PROTOCOL_DATA_DESCRIPTOR {
    DWORD Version;
    DWORD Size;
    STORAGE_PROTOCOL_SPECIFIC_DATA ProtocolSpecificData;
};

enum MEDIA_TYPE { Unknown = 0, FixedMedia = 12, RemovableMedia = 11 };

struct DISK_GEOMETRY {
    LARGE_INTEGER Cylinders;
    MEDIA_TYPE MediaType;
    DWORD TracksPerCylinder;
    DWORD SectorsPerTrack;
    DWORD BytesPerSector;
};

struct DISK_GEOMETRY_EX {
    DISK_GEOMETRY Geometry;
    LARGE_INTEGER DiskSize;
    BYTE Data[1];
};

#pragma pack(push, 1)
struct IDEREGS {
    BYTE bFeaturesReg;
    BYTE bSectorCountReg;
    BYTE bSectorNumberReg;
    BYTE bCylLowReg;
    BYTE bCylHighReg;
    BYTE bDriveHeadReg;
    BYTE bCommandReg;
    BYTE bReserved;
};
struct SENDCMDINPARAMS {
    DWORD cBufferSize;
    IDEREGS irDriveRegs;
    BYTE bDriveNumber;
    BYTE bReserved[3];
    DWORD dwReserved[4];
    BYTE bBuffer[1];
};
struct DRIVERSTATUS {
    BYTE bDriverError;
    BYTE bIDEError;
    BYTE bReserved[2];
    DWORD dwReserved[2];
};
struct SENDCMDOUTPARAMS {
    DWORD cBufferSize;
    DRIVERSTATUS DriverStatus;
    BYTE bBuffer[1];
};
#pragma pack(pop)

#define SMART_CMD 0xB0u
#define READ_ATTRIBUTES 0xD0u
#define READ_THRESHOLDS 0xD1u
#define SMART_CYL_LOW 0x4Fu
#define SMART_CYL_HI  0xC2u

BOOL DeviceIoControl(
    HANDLE hDevice,
    DWORD dwIoControlCode,
    LPVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize,
    LPDWORD lpBytesReturned,
    LPOVERLAPPED lpOverlapped);
