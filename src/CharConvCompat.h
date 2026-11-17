#pragma once
// CharConvCompat.h - <charconv>, plus std::from_chars(first, last, float / double / long double&) where the C++
// library lacks it (libstdc++ before GCC 11, e.g. Ubuntu 20.04's GCC 9, which the Linux build of the readers
// accepts). On Windows and with newer libraries this is exactly <charconv>.
#include <charconv>

#if !defined(_WIN32) && defined(__GLIBCXX__) && !defined(__cpp_lib_to_chars)
#include <cerrno>
#include <cstdlib>
#include <system_error>

namespace Explorer2::CharConvCompat {

// from_chars rules (general format) on top of strtod: no leading blanks or '+', the C locale's '.', "0x1p3" read
// as 0 (no hex), and the value left untouched when it is out of range
template <typename T, typename Strto>
std::from_chars_result FromCharsFloat(const char* first, const char* last, T& value, Strto strto) {
    std::from_chars_result res{ first, std::errc::invalid_argument };
    if (!first || first >= last) return res;
    const char c0 = *first;
    if (c0 == '+' || c0 == ' ' || c0 == '\t' || c0 == '\n' || c0 == '\r' || c0 == '\f' || c0 == '\v') return res;
    char buf[512];
    size_t n = static_cast<size_t>(last - first);
    if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
    size_t o = 0;
    for (size_t i = 0; i < n; ++i) {
        const bool afterLeadingZero = (i == 1 && first[0] == '0') || (i == 2 && first[0] == '-' && first[1] == '0');
        if ((first[i] == 'x' || first[i] == 'X') && afterLeadingZero) break;
        buf[o++] = first[i];
    }
    buf[o] = '\0';
    char* end = nullptr;
    errno = 0;
    const T v = strto(buf, &end);
    const size_t used = static_cast<size_t>(end - buf);
    if (used == 0) return res;
    res.ptr = first + used;
    if (errno == ERANGE) {
        res.ec = std::errc::result_out_of_range;
        return res;
    }
    value = v;
    res.ec = std::errc();
    return res;
}

} // namespace Explorer2::CharConvCompat

namespace std {
inline from_chars_result from_chars(const char* first, const char* last, double& value) {
    return ::Explorer2::CharConvCompat::FromCharsFloat(first, last, value, [](const char* s, char** e) { return std::strtod(s, e); });
}
inline from_chars_result from_chars(const char* first, const char* last, float& value) {
    return ::Explorer2::CharConvCompat::FromCharsFloat(first, last, value, [](const char* s, char** e) { return std::strtof(s, e); });
}
inline from_chars_result from_chars(const char* first, const char* last, long double& value) {
    return ::Explorer2::CharConvCompat::FromCharsFloat(first, last, value, [](const char* s, char** e) { return std::strtold(s, e); });
}
} // namespace std
#endif
