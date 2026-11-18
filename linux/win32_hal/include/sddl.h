#pragma once
#include <windows.h>
#ifndef SDDL_REVISION_1
#define SDDL_REVISION_1 1
#endif
extern "C" {
BOOL ConvertSidToStringSidW(PSID Sid, LPWSTR* StringSid);
BOOL ConvertStringSecurityDescriptorToSecurityDescriptorW(
    LPCWSTR StringSecurityDescriptor,
    DWORD StringSDRevision,
    PSECURITY_DESCRIPTOR* SecurityDescriptor,
    PULONG SecurityDescriptorSize);
}
