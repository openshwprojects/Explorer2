#pragma once
// linux/win32_hal/include/bcrypt.h - Self-contained SHA-256 & SHA-1 BCrypt HAL for Linux.

#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>

using BCRYPT_ALG_HANDLE = void*;
using BCRYPT_HASH_HANDLE = void*;

#define BCRYPT_SHA1_ALGORITHM   L"SHA1"
#define BCRYPT_SHA256_ALGORITHM L"SHA256"
#define BCRYPT_OBJECT_LENGTH    L"ObjectLength"

namespace Explorer2::HalBcrypt {

struct AlgObj {
    int kind = 0; // 1 = SHA1, 256 = SHA256
};

struct Sha256State {
    uint32_t h[8];
    uint8_t block[64];
    size_t blockLen = 0;
    uint64_t totalBytes = 0;

    void Init() {
        h[0] = 0x6a09e667u; h[1] = 0xbb67ae85u; h[2] = 0x3c6ef372u; h[3] = 0xa54ff53au;
        h[2+2] = 0x510e527fu; h[5] = 0x9b05688cu; h[6] = 0x1f83d9abu; h[7] = 0x5be0cd19u;
        blockLen = 0;
        totalBytes = 0;
    }

    static inline uint32_t Ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Transform(const uint8_t data[64]) {
        static constexpr uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[i*4]) << 24) |
                   (static_cast<uint32_t>(data[i*4+1]) << 16) |
                   (static_cast<uint32_t>(data[i*4+2]) << 8) |
                   static_cast<uint32_t>(data[i*4+3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = Ror(w[i-15], 7) ^ Ror(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = Ror(w[i-2], 17) ^ Ror(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    void Update(const uint8_t* p, size_t len) {
        totalBytes += len;
        while (len > 0) {
            size_t take = 64 - blockLen;
            if (take > len) take = len;
            std::memcpy(block + blockLen, p, take);
            blockLen += take;
            p += take;
            len -= take;
            if (blockLen == 64) {
                Transform(block);
                blockLen = 0;
            }
        }
    }

    void Final(uint8_t out[32]) {
        uint64_t bits = totalBytes * 8ULL;
        block[blockLen++] = 0x80u;
        if (blockLen > 56) {
            while (blockLen < 64) block[blockLen++] = 0;
            Transform(block);
            blockLen = 0;
        }
        while (blockLen < 56) block[blockLen++] = 0;
        for (int i = 0; i < 8; ++i) block[56 + i] = static_cast<uint8_t>((bits >> (56 - i * 8)) & 0xFFu);
        Transform(block);
        for (int i = 0; i < 8; ++i) {
            out[i*4]   = static_cast<uint8_t>((h[i] >> 24) & 0xFFu);
            out[i*4+1] = static_cast<uint8_t>((h[i] >> 16) & 0xFFu);
            out[i*4+2] = static_cast<uint8_t>((h[i] >> 8) & 0xFFu);
            out[i*4+3] = static_cast<uint8_t>(h[i] & 0xFFu);
        }
    }
};

struct Sha1State {
    uint32_t h[5];
    uint8_t block[64];
    size_t blockLen = 0;
    uint64_t totalBytes = 0;

    void Init() {
        h[0]=0x67452301u; h[1]=0xEFCDAB89u; h[2]=0x98BADCFEu; h[3]=0x10325476u; h[4]=0xC3D2E1F0u;
        blockLen = 0; totalBytes = 0;
    }
    static inline uint32_t Rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
    void Transform(const uint8_t data[64]) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(data[i*4])<<24)|(uint32_t(data[i*4+1])<<16)|(uint32_t(data[i*4+2])<<8)|uint32_t(data[i*4+3]);
        for (int i = 16; i < 80; ++i) w[i] = Rol(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;            k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;            k = 0xCA62C1D6u; }
            uint32_t temp = Rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = Rol(b, 30); b = a; a = temp;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    void Update(const uint8_t* p, size_t len) {
        totalBytes += len;
        while (len > 0) {
            size_t take = 64 - blockLen;
            if (take > len) take = len;
            std::memcpy(block + blockLen, p, take);
            blockLen += take; p += take; len -= take;
            if (blockLen == 64) { Transform(block); blockLen = 0; }
        }
    }
    void Final(uint8_t out[20]) {
        uint64_t bits = totalBytes * 8ULL;
        block[blockLen++] = 0x80u;
        if (blockLen > 56) { while (blockLen < 64) block[blockLen++] = 0; Transform(block); blockLen = 0; }
        while (blockLen < 56) block[blockLen++] = 0;
        for (int i = 0; i < 8; ++i) block[56 + i] = static_cast<uint8_t>((bits >> (56 - i * 8)) & 0xFFu);
        Transform(block);
        for (int i = 0; i < 5; ++i) {
            out[i*4]   = uint8_t((h[i]>>24)&0xFFu); out[i*4+1] = uint8_t((h[i]>>16)&0xFFu);
            out[i*4+2] = uint8_t((h[i]>>8)&0xFFu);  out[i*4+3] = uint8_t(h[i]&0xFFu);
        }
    }
};

struct HashObj {
    int kind = 256;
    Sha256State sha256;
    Sha1State sha1;
};

} // namespace Explorer2::HalBcrypt

inline NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE* phAlgorithm, LPCWSTR pszAlgId, LPCWSTR, ULONG) {
    if (!phAlgorithm || !pszAlgId) return -1;
    auto* a = new Explorer2::HalBcrypt::AlgObj();
    if (std::wcscmp(pszAlgId, BCRYPT_SHA1_ALGORITHM) == 0) a->kind = 1;
    else if (std::wcscmp(pszAlgId, BCRYPT_SHA256_ALGORITHM) == 0) a->kind = 256;
    else { delete a; return -1; }
    *phAlgorithm = a;
    return 0;
}

inline NTSTATUS BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE hAlgorithm, ULONG) {
    delete static_cast<Explorer2::HalBcrypt::AlgObj*>(hAlgorithm);
    return 0;
}

inline NTSTATUS BCryptGetProperty(BCRYPT_ALG_HANDLE hObject, LPCWSTR pszProperty, PUCHAR pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG) {
    if (!hObject || !pszProperty) return -1;
    if (std::wcscmp(pszProperty, BCRYPT_OBJECT_LENGTH) == 0) {
        DWORD sz = 256;
        if (pcbResult) *pcbResult = sizeof(DWORD);
        if (pbOutput && cbOutput >= sizeof(DWORD)) std::memcpy(pbOutput, &sz, sizeof(DWORD));
        return 0;
    }
    return -1;
}

inline NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_HASH_HANDLE* phHash, PUCHAR, ULONG, PUCHAR, ULONG, ULONG) {
    if (!hAlgorithm || !phHash) return -1;
    auto* a = static_cast<Explorer2::HalBcrypt::AlgObj*>(hAlgorithm);
    auto* ho = new Explorer2::HalBcrypt::HashObj();
    ho->kind = a->kind;
    if (ho->kind == 1) ho->sha1.Init();
    else ho->sha256.Init();
    *phHash = ho;
    return 0;
}

inline NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE hHash, PUCHAR pbInput, ULONG cbInput, ULONG) {
    if (!hHash || (!pbInput && cbInput > 0)) return -1;
    auto* ho = static_cast<Explorer2::HalBcrypt::HashObj*>(hHash);
    if (ho->kind == 1) ho->sha1.Update(pbInput, cbInput);
    else ho->sha256.Update(pbInput, cbInput);
    return 0;
}

inline NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE hHash, PUCHAR pbOutput, ULONG cbOutput, ULONG) {
    if (!hHash || !pbOutput) return -1;
    auto* ho = static_cast<Explorer2::HalBcrypt::HashObj*>(hHash);
    if (ho->kind == 1) {
        if (cbOutput < 20) return -1;
        ho->sha1.Final(pbOutput);
        return 0;
    }
    if (cbOutput < 32) return -1;
    ho->sha256.Final(pbOutput);
    return 0;
}

inline NTSTATUS BCryptDestroyHash(BCRYPT_HASH_HANDLE hHash) {
    delete static_cast<Explorer2::HalBcrypt::HashObj*>(hHash);
    return 0;
}
