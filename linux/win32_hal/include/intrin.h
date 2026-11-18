#pragma once
// linux/win32_hal/include/intrin.h - MSVC __cpuid / __cpuidex intrinsics on x86/x86_64 GCC & Clang.

#if defined(__i386__) || defined(__x86_64__)
inline void __cpuidex(int cpuInfo[4], int function_id, int subfunction_id) noexcept {
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ __volatile__("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(static_cast<unsigned int>(function_id)),
                           "c"(static_cast<unsigned int>(subfunction_id)));
    cpuInfo[0] = static_cast<int>(eax);
    cpuInfo[1] = static_cast<int>(ebx);
    cpuInfo[2] = static_cast<int>(ecx);
    cpuInfo[3] = static_cast<int>(edx);
}
inline void __cpuid(int cpuInfo[4], int function_id) noexcept {
    __cpuidex(cpuInfo, function_id, 0);
}
#else
inline void __cpuidex(int cpuInfo[4], int, int) noexcept {
    cpuInfo[0] = cpuInfo[1] = cpuInfo[2] = cpuInfo[3] = 0;
}
inline void __cpuid(int cpuInfo[4], int) noexcept {
    cpuInfo[0] = cpuInfo[1] = cpuInfo[2] = cpuInfo[3] = 0;
}
#endif
