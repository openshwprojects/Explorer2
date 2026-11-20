#pragma once
// Blowfish.h - the Blowfish block cipher (Bruce Schneier, 1993): 64-bit blocks, keys of 1 to 56 bytes. The PK2
// archives of Joymax games hide their directories with it (formats/archives/SilkroadPk2.h).
//
// The P-array and the S-boxes start out as the first 1,042 32-bit words of pi's fraction in hexadecimal
// (0x243F6A88, 0x85A308D3...). They are computed here with Machin's formula, pi = 16 arctan(1/5) - 4 arctan(1/239),
// rather than copied in as 4 KB of constants; a self-test checks the cipher against the published test vectors
// (SelfTestSuite, Stage16.Test24).
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Explorer2::Blowfish {

namespace Detail {

// Fixed-point numbers as words, most significant first: word 0 is the integer part, the others the fraction
inline void DivideBy(std::vector<uint32_t>& x, uint32_t d) {
    uint64_t rem = 0;
    for (uint32_t& w : x) {
        const uint64_t cur = (rem << 32) | w;
        w = static_cast<uint32_t>(cur / d);
        rem = cur % d;
    }
}
inline void AddInto(std::vector<uint32_t>& acc, const std::vector<uint32_t>& v) {
    uint64_t carry = 0;
    for (size_t i = acc.size(); i-- > 0;) {
        const uint64_t s = static_cast<uint64_t>(acc[i]) + v[i] + carry;
        acc[i] = static_cast<uint32_t>(s);
        carry = s >> 32;
    }
}
inline void SubtractFrom(std::vector<uint32_t>& acc, const std::vector<uint32_t>& v) {
    uint64_t borrow = 0;
    for (size_t i = acc.size(); i-- > 0;) {
        const uint64_t sub = static_cast<uint64_t>(v[i]) + borrow;
        borrow = (static_cast<uint64_t>(acc[i]) < sub) ? 1 : 0;
        acc[i] = static_cast<uint32_t>((static_cast<uint64_t>(acc[i]) + (borrow << 32)) - sub);
    }
}
inline bool AllZero(const std::vector<uint32_t>& x) {
    for (uint32_t w : x) if (w) return false;
    return true;
}
// acc += mult * arctan(1/x), or -= when subtract
inline void AddArctanOfInverse(std::vector<uint32_t>& acc, uint32_t x, uint32_t mult, bool subtract) {
    std::vector<uint32_t> power(acc.size(), 0), term;
    power[0] = mult;
    DivideBy(power, x);                         // mult / x^(2k+1), starting with k = 0
    const uint32_t xx = x * x;
    for (uint32_t k = 0; !AllZero(power); ++k) {
        term = power;
        DivideBy(term, 2 * k + 1);
        const bool negative = ((k & 1) != 0) != subtract;
        if (negative) SubtractFrom(acc, term);
        else AddInto(acc, term);
        DivideBy(power, xx);
    }
}

} // namespace Detail

inline constexpr size_t kPiWords = 18 + 4 * 256;

// The first kPiWords words of pi's fraction, computed once
inline const std::vector<uint32_t>& PiFractionWords() {
    static const std::vector<uint32_t> s_words = [] {
        std::vector<uint32_t> acc(1 + kPiWords + 4, 0);      // 4 guard words absorb the rounding of the divisions
        Detail::AddArctanOfInverse(acc, 5, 16, false);
        Detail::AddArctanOfInverse(acc, 239, 4, true);
        return std::vector<uint32_t>(acc.begin() + 1, acc.begin() + 1 + static_cast<std::ptrdiff_t>(kPiWords));
    }();
    return s_words;
}

// The cipher with its key schedule; a block is two 32-bit words, the left one first (big-endian in the
// standard's byte order)
struct Cipher {
    std::array<uint32_t, 18> P{};
    std::array<std::array<uint32_t, 256>, 4> S{};

    uint32_t F(uint32_t x) const {
        return ((S[0][x >> 24] + S[1][(x >> 16) & 0xFF]) ^ S[2][(x >> 8) & 0xFF]) + S[3][x & 0xFF];
    }
    void Encrypt(uint32_t& l, uint32_t& r) const {
        for (int i = 0; i < 16; ++i) {
            l ^= P[i];
            r ^= F(l);
            std::swap(l, r);
        }
        std::swap(l, r);
        r ^= P[16];
        l ^= P[17];
    }
    void Decrypt(uint32_t& l, uint32_t& r) const {
        for (int i = 17; i > 1; --i) {
            l ^= P[i];
            r ^= F(l);
            std::swap(l, r);
        }
        std::swap(l, r);
        r ^= P[1];
        l ^= P[0];
    }
    // The key's bytes, repeated as often as needed (a key of no bytes leaves the cipher at pi)
    void Initialize(const uint8_t* key, size_t keyLen) {
        const std::vector<uint32_t>& pi = PiFractionWords();
        for (size_t i = 0; i < 18; ++i) P[i] = pi[i];
        for (size_t b = 0; b < 4; ++b) {
            for (size_t i = 0; i < 256; ++i) S[b][i] = pi[18 + b * 256 + i];
        }
        if (!key || keyLen == 0) return;
        size_t j = 0;
        for (size_t i = 0; i < 18; ++i) {
            uint32_t data = 0;
            for (int k = 0; k < 4; ++k) {
                data = (data << 8) | key[j];
                j = (j + 1) % keyLen;
            }
            P[i] ^= data;
        }
        uint32_t l = 0, r = 0;
        for (size_t i = 0; i < 18; i += 2) {
            Encrypt(l, r);
            P[i] = l;
            P[i + 1] = r;
        }
        for (size_t b = 0; b < 4; ++b) {
            for (size_t i = 0; i < 256; i += 2) {
                Encrypt(l, r);
                S[b][i] = l;
                S[b][i + 1] = r;
            }
        }
    }
};

} // namespace Explorer2::Blowfish
