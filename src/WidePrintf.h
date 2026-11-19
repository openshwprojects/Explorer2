#pragma once
// WidePrintf.h - the formatting behind swprintf_s where Microsoft's C library is not there (see PlatformCompat.h).
//
// The C library's vswprintf cannot be given wide text outside ASCII. musl - the C library of the static Linux
// build - formats into multibyte text and converts the result back to wide characters. In the "C" locale (and
// this program never calls setlocale) a character outside ASCII cannot be converted: the call fails and the
// buffer stays empty. One degree sign in "%d°C", one Polish letter in a message or in a file name given to %s
// was enough to lose the whole text. A UTF-8 locale would not cure all of it either: the halves of a surrogate
// pair, which is how the sources write emoji, are refused in every locale.
//
// VFormatWideC therefore never converts wide text. The characters of the format and of wide arguments are copied
// as they are; only numbers go through the C library (snprintf), and what that returns is ASCII. A narrow
// argument becomes the characters of the same numbers, byte by byte - which is what Microsoft's library makes of
// %S / %hs in the "C" locale the program runs in on Windows (text known to be UTF-8 goes through
// PathUtils::Utf8ToWide before it is formatted).
//
// The format is in the C dialect: %ls / %lc take wide text, %s / %c narrow. Microsoft's dialect (%s wide, %S
// narrow, %I64d) is translated to it by TranslateMsFormat in PlatformCompat.h. Nothing here depends on the
// platform, so the self-tests run this formatter on Windows too, next to the real swprintf_s.

#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

namespace Explorer2::Compat {
namespace WidePrintfDetail {

// The caller's buffer. What does not fit is dropped and remembered: the result is then given up as a whole.
struct WideSink {
    wchar_t* buffer = nullptr;
    size_t capacity = 0;        // characters the buffer takes, the terminator not counted
    size_t length = 0;
    bool overflow = false;

    void Put(wchar_t c) {
        if (length < capacity) buffer[length++] = c;
        else overflow = true;
    }
    void Put(const wchar_t* s, size_t n) {
        if (n <= capacity - length) {
            if (n) std::wmemcpy(buffer + length, s, n);
            length += n;
        } else {
            overflow = true;
        }
    }
    void Fill(wchar_t c, size_t n) {
        if (n <= capacity - length) {
            for (size_t i = 0; i < n; ++i) buffer[length++] = c;
        } else {
            overflow = true;
        }
    }
    void PutBytes(const char* s, size_t n) {
        if (n <= capacity - length) {
            for (size_t i = 0; i < n; ++i) buffer[length++] = static_cast<wchar_t>(static_cast<unsigned char>(s[i]));
        } else {
            overflow = true;
        }
    }
};

// One number through snprintf. `spec` is a whole conversion ("%-8.3f") put together by the caller; in the "C"
// locale the output of a numeric conversion is ASCII.
// (The names here keep clear of the words system headers define as macros: "small" in Windows' rpcndr.h, "None"
// in X11's X.h. This header is included after either of them.)
template <typename T>
void PutNumber(WideSink& out, const char* spec, T value) {
    char digits[128];
    const int n = std::snprintf(digits, sizeof(digits), spec, value);
    if (n < 0) return;
    if (static_cast<size_t>(n) < sizeof(digits)) {
        out.PutBytes(digits, static_cast<size_t>(n));
        return;
    }
    if (static_cast<size_t>(n) > out.capacity - out.length) {      // longer than what is left: no need to format it
        out.overflow = true;
        return;
    }
    std::string longDigits(static_cast<size_t>(n) + 1, '\0');
    const int m = std::snprintf(&longDigits[0], longDigits.size(), spec, value);
    if (m > 0) out.PutBytes(longDigits.data(), (static_cast<size_t>(m) < longDigits.size()) ? static_cast<size_t>(m) : longDigits.size() - 1);
}

// Reads the digits of a width or a precision; a value above `cap` stays at `cap`
inline int ReadNumber(const wchar_t*& p, int cap) {
    long long v = 0;
    while (*p >= L'0' && *p <= L'9') {
        if (v <= cap) v = v * 10 + (*p - L'0');
        ++p;
    }
    return (v > cap) ? cap : static_cast<int>(v);
}

} // namespace WidePrintfDetail

// vswprintf with the rules above. Returns the number of characters written; when the result does not fit in
// `count` characters (the terminator included) it returns -1 and leaves an empty string, which is also how
// PlatformCompat.h's swprintf_s has always reported it.
inline int VFormatWideC(wchar_t* buffer, size_t count, const wchar_t* format, va_list args) {
    using namespace WidePrintfDetail;
    if (!buffer || count == 0) return -1;
    buffer[0] = L'\0';
    if (!format) return 0;

    WideSink out;
    out.buffer = buffer;
    out.capacity = count - 1;
    // Nothing longer than the buffer is ever useful: widths and precisions stop there, so that "%999999999d"
    // cannot make snprintf produce what would be thrown away
    const int cap = (count < static_cast<size_t>(INT_MAX / 2)) ? static_cast<int>(count) : INT_MAX / 2;

    for (const wchar_t* p = format; *p && !out.overflow; ) {
        if (*p != L'%') {
            const wchar_t* run = p;
            while (*p && *p != L'%') ++p;
            out.Put(run, static_cast<size_t>(p - run));
            continue;
        }
        const wchar_t* const specStart = p++;
        if (*p == L'%') {
            out.Put(L'%');
            ++p;
            continue;
        }

        bool left = false, plus = false, space = false, alt = false, zero = false;
        for (bool more = true; more; ) {
            switch (*p) {
            case L'-': left = true; ++p; break;
            case L'+': plus = true; ++p; break;
            case L' ': space = true; ++p; break;
            case L'#': alt = true; ++p; break;
            case L'0': zero = true; ++p; break;
            case L'\'': ++p; break;                 // grouping by thousands: accepted, not applied
            default: more = false; break;
            }
        }

        int width = 0;
        if (*p == L'*') {
            ++p;
            const int w = va_arg(args, int);
            if (w < 0) {                            // a negative width is the '-' flag and a width
                left = true;
                width = (w == INT_MIN) ? cap : -w;
            } else {
                width = w;
            }
            if (width > cap) width = cap;
        } else {
            width = ReadNumber(p, cap);
        }

        int precision = -1;                         // -1: none given
        if (*p == L'.') {
            ++p;
            if (*p == L'*') {
                ++p;
                const int pr = va_arg(args, int);
                precision = (pr < 0) ? -1 : ((pr > cap) ? cap : pr);
            } else {
                precision = ReadNumber(p, cap);
            }
        }

        enum class Len { Plain, hh, h, l, ll, L, z, j, t };
        Len len = Len::Plain;
        for (bool more = true; more; ) {
            switch (*p) {
            case L'h': len = (len == Len::h) ? Len::hh : Len::h; ++p; break;
            case L'l': len = (len == Len::l) ? Len::ll : Len::l; ++p; break;
            case L'q': len = Len::ll; ++p; break;
            case L'L': len = Len::L; ++p; break;
            case L'z': len = Len::z; ++p; break;
            case L'j': len = Len::j; ++p; break;
            case L't': len = Len::t; ++p; break;
            default: more = false; break;
            }
        }

        const wchar_t conv = *p;
        if (!conv) {                                // the format ends inside a conversion: kept as text
            out.Put(specStart, static_cast<size_t>(p - specStart));
            break;
        }
        ++p;

        // The same conversion for snprintf: "%", flags, width and precision here; Spec adds the length and the letter
        char spec[64];
        size_t specLen = 0;
        spec[specLen++] = '%';
        if (left) spec[specLen++] = '-';
        if (plus) spec[specLen++] = '+';
        if (space) spec[specLen++] = ' ';
        if (alt) spec[specLen++] = '#';
        if (zero) spec[specLen++] = '0';
        if (width > 0) specLen += static_cast<size_t>(std::snprintf(spec + specLen, 16, "%d", width));
        if (precision >= 0) specLen += static_cast<size_t>(std::snprintf(spec + specLen, 16, ".%d", precision));
        auto Spec = [&spec, specLen](const char* length, char letter) -> const char* {
            size_t n = specLen;
            while (*length) spec[n++] = *length++;
            spec[n++] = letter;
            spec[n] = '\0';
            return spec;
        };
        // Text is padded to the width on the side the '-' flag names: with spaces, or - on the left, as Microsoft's
        // library does it - with zeros when the '0' flag is given
        auto PadBefore = [&](size_t textLen) {
            if (!left && static_cast<size_t>(width) > textLen) out.Fill(zero ? L'0' : L' ', static_cast<size_t>(width) - textLen);
        };
        auto PadAfter = [&](size_t textLen) {
            if (left && static_cast<size_t>(width) > textLen) out.Fill(L' ', static_cast<size_t>(width) - textLen);
        };

        switch (conv) {
        case L'd':
        case L'i':
            switch (len) {
            case Len::l:  PutNumber(out, Spec("l", 'd'), va_arg(args, long)); break;
            case Len::ll:
            case Len::L:  PutNumber(out, Spec("ll", 'd'), va_arg(args, long long)); break;
            case Len::z:
            case Len::t:  PutNumber(out, Spec("t", 'd'), va_arg(args, std::ptrdiff_t)); break;
            case Len::j:  PutNumber(out, Spec("j", 'd'), va_arg(args, std::intmax_t)); break;
            case Len::hh: PutNumber(out, Spec("hh", 'd'), va_arg(args, int)); break;
            case Len::h:  PutNumber(out, Spec("h", 'd'), va_arg(args, int)); break;
            default:      PutNumber(out, Spec("", 'd'), va_arg(args, int)); break;
            }
            break;

        case L'u':
        case L'o':
        case L'x':
        case L'X': {
            const char letter = static_cast<char>(conv);
            switch (len) {
            case Len::l:  PutNumber(out, Spec("l", letter), va_arg(args, unsigned long)); break;
            case Len::ll:
            case Len::L:  PutNumber(out, Spec("ll", letter), va_arg(args, unsigned long long)); break;
            case Len::z:
            case Len::t:  PutNumber(out, Spec("z", letter), va_arg(args, std::size_t)); break;
            case Len::j:  PutNumber(out, Spec("j", letter), va_arg(args, std::uintmax_t)); break;
            case Len::hh: PutNumber(out, Spec("hh", letter), va_arg(args, unsigned int)); break;
            case Len::h:  PutNumber(out, Spec("h", letter), va_arg(args, unsigned int)); break;
            default:      PutNumber(out, Spec("", letter), va_arg(args, unsigned int)); break;
            }
            break;
        }

        case L'e': case L'E':
        case L'f': case L'F':
        case L'g': case L'G':
        case L'a': case L'A': {
            const char letter = static_cast<char>(conv);
            if (len == Len::L) PutNumber(out, Spec("L", letter), va_arg(args, long double));
            else PutNumber(out, Spec("", letter), va_arg(args, double));
            break;
        }

        case L'p':
            PutNumber(out, Spec("", 'p'), va_arg(args, void*));
            break;

        case L'c': {
            // A wide character arrives as wint_t, a narrow one as int
            const wchar_t ch = (len == Len::l)
                ? static_cast<wchar_t>(va_arg(args, unsigned int))
                : static_cast<wchar_t>(static_cast<unsigned char>(va_arg(args, int)));
            PadBefore(1);
            out.Put(ch);
            PadAfter(1);
            break;
        }

        case L's':
            // The precision is the most that is read of the argument, which then needs no terminator
            if (len == Len::l) {
                const wchar_t* s = va_arg(args, const wchar_t*);
                if (!s) s = L"(null)";
                size_t n = 0;
                while ((precision < 0 || n < static_cast<size_t>(precision)) && s[n]) ++n;
                PadBefore(n);
                out.Put(s, n);
                PadAfter(n);
            } else {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                size_t n = 0;
                while ((precision < 0 || n < static_cast<size_t>(precision)) && s[n]) ++n;
                PadBefore(n);
                out.PutBytes(s, n);         // each byte: the character of the same number
                PadAfter(n);
            }
            break;

        case L'n':
            (void)va_arg(args, void*);              // nothing is stored (Microsoft's _s functions refuse %n too)
            break;

        default:                                    // not a conversion: kept as text
            out.Put(specStart, static_cast<size_t>(p - specStart));
            break;
        }
    }

    if (out.overflow) {
        buffer[0] = L'\0';
        return -1;
    }
    buffer[out.length] = L'\0';
    return static_cast<int>(out.length);
}

inline int FormatWideC(wchar_t* buffer, size_t count, const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = VFormatWideC(buffer, count, format, args);
    va_end(args);
    return n;
}

} // namespace Explorer2::Compat
