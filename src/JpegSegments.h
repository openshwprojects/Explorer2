#pragma once
// JpegSegments.h - the metadata segments of a JPEG file as plain bytes: the EXIF block (APP1 "Exif"), the colour
// profile (APP2 "ICC_PROFILE"), and what a picture that was turned or cropped needs corrected in its EXIF block.
//
// No codec here. Windows' imaging library carries a photo's metadata itself (ShellMenuEmulator.cpp,
// CarryPhotoMetadata); the Linux build's JPEG encoder writes the picture only, and this puts the photo's date,
// camera, GPS position and colour profile back into the file it wrote. It also corrects the orientation note of a
// JPEG that `jpegtran` turned without loss.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Explorer2::JpegSegments {

// One segment before the compressed picture: its marker (0xE1 = APP1...), where its 0xFF is, and its whole size
// (marker, length field and payload)
struct Segment {
    uint8_t marker = 0;
    size_t offset = 0;
    size_t size = 0;
};

// The segments between SOI and the start of the picture (SOS). False when the bytes are no JPEG or a segment
// runs past the end.
inline bool ListHeaderSegments(const uint8_t* d, size_t n, std::vector<Segment>& out) {
    out.clear();
    if (!d || n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t at = 2;
    while (at + 4 <= n) {
        if (d[at] != 0xFF) return false;
        const uint8_t marker = d[at + 1];
        if (marker == 0xFF) { ++at; continue; }                                         // fill byte
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { at += 2; continue; } // no length
        if (marker == 0xDA || marker == 0xD9) return true;                              // the picture begins / ends
        const size_t length = (static_cast<size_t>(d[at + 2]) << 8) | d[at + 3];
        if (length < 2 || length > n - at - 2) return false;
        out.push_back({ marker, at, length + 2 });
        at += length + 2;
    }
    return false;
}

inline bool IsExifSegment(const uint8_t* d, const Segment& s) {
    return s.marker == 0xE1 && s.size >= 10 && std::memcmp(d + s.offset + 4, "Exif\0\0", 6) == 0;
}
inline bool IsIccSegment(const uint8_t* d, const Segment& s) {
    return s.marker == 0xE2 && s.size >= 18 && std::memcmp(d + s.offset + 4, "ICC_PROFILE\0", 12) == 0;
}

// The payload of the first APP1 EXIF segment ("Exif\0\0" and the TIFF structure after it); empty when there is none
inline std::vector<uint8_t> FindExif(const uint8_t* d, size_t n) {
    std::vector<Segment> segs;
    ListHeaderSegments(d, n, segs);
    for (const Segment& s : segs) {
        if (IsExifSegment(d, s)) return std::vector<uint8_t>(d + s.offset + 4, d + s.offset + s.size);
    }
    return {};
}

// Every APP2 colour profile segment, whole (a large profile is split over several)
inline std::vector<std::vector<uint8_t>> FindIccSegments(const uint8_t* d, size_t n) {
    std::vector<std::vector<uint8_t>> out;
    std::vector<Segment> segs;
    ListHeaderSegments(d, n, segs);
    for (const Segment& s : segs) {
        if (IsIccSegment(d, s)) out.emplace_back(d + s.offset, d + s.offset + s.size);
    }
    return out;
}

// ---- The TIFF structure inside an EXIF block --------------------------------------------------------------------
namespace Detail {

struct Tiff {
    uint8_t* base = nullptr;    // first byte of the TIFF header ("II" or "MM")
    size_t size = 0;
    bool bigEndian = false;

    uint16_t U16(size_t at) const {
        return bigEndian ? static_cast<uint16_t>((base[at] << 8) | base[at + 1])
                         : static_cast<uint16_t>(base[at] | (base[at + 1] << 8));
    }
    uint32_t U32(size_t at) const {
        return bigEndian ? (static_cast<uint32_t>(base[at]) << 24) | (static_cast<uint32_t>(base[at + 1]) << 16) |
                               (static_cast<uint32_t>(base[at + 2]) << 8) | base[at + 3]
                         : static_cast<uint32_t>(base[at]) | (static_cast<uint32_t>(base[at + 1]) << 8) |
                               (static_cast<uint32_t>(base[at + 2]) << 16) | (static_cast<uint32_t>(base[at + 3]) << 24);
    }
    void Put16(size_t at, uint16_t v) {
        if (bigEndian) { base[at] = static_cast<uint8_t>(v >> 8); base[at + 1] = static_cast<uint8_t>(v); }
        else { base[at] = static_cast<uint8_t>(v); base[at + 1] = static_cast<uint8_t>(v >> 8); }
    }
    void Put32(size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) base[at + i] = static_cast<uint8_t>(bigEndian ? (v >> (24 - 8 * i)) : (v >> (8 * i)));
    }
    // An IFD at `offset` with all its entries inside the block, or 0 entries
    uint16_t EntryCount(uint32_t offset) const {
        if (offset < 8 || offset > size || size - offset < 2) return 0;
        const uint16_t count = U16(offset);
        if (static_cast<size_t>(count) * 12u + 6u > size - offset) return 0;
        return count;
    }
    // Where the entry with this tag is (its first byte), or 0
    size_t FindEntry(uint32_t ifd, uint16_t tag) const {
        const uint16_t count = EntryCount(ifd);
        for (uint16_t i = 0; i < count; ++i) {
            const size_t e = ifd + 2u + i * 12u;
            if (U16(e) == tag) return e;
        }
        return 0;
    }
};

// The TIFF structure of an EXIF payload ("Exif\0\0" + TIFF), or false
inline bool OpenTiff(std::vector<uint8_t>& exif, Tiff& t) {
    if (exif.size() < 6 + 8 || std::memcmp(exif.data(), "Exif\0\0", 6) != 0) return false;
    t.base = exif.data() + 6;
    t.size = exif.size() - 6;
    if (t.base[0] == 'I' && t.base[1] == 'I') t.bigEndian = false;
    else if (t.base[0] == 'M' && t.base[1] == 'M') t.bigEndian = true;
    else return false;
    return t.U16(2) == 42;
}

} // namespace Detail

// The orientation an EXIF payload notes (1 to 8), 1 when it notes none
inline int ExifOrientation(std::vector<uint8_t> exif) {
    Detail::Tiff t;
    if (!Detail::OpenTiff(exif, t)) return 1;
    const size_t e = t.FindEntry(t.U32(4), 0x0112);
    if (!e || t.U16(e + 2) != 3) return 1;                  // SHORT
    const uint16_t v = t.U16(e + 8);
    return (v >= 1 && v <= 8) ? v : 1;
}

// The date and time the picture was taken (ASCII "YYYY:MM:DD HH:MM:SS" from IFD0's DateTime), empty when none
inline std::string ExifDateTime(std::vector<uint8_t> exif) {
    Detail::Tiff t;
    if (!Detail::OpenTiff(exif, t)) return {};
    const size_t e = t.FindEntry(t.U32(4), 0x0132);
    if (!e || t.U16(e + 2) != 2) return {};                 // ASCII
    const uint32_t count = t.U32(e + 4);
    if (count == 0 || count > 64) return {};
    const size_t at = (count <= 4) ? e + 8 : t.U32(e + 8);
    if (at > t.size || count > t.size - at) return {};
    std::string s(reinterpret_cast<const char*>(t.base + at), count);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// Corrects an EXIF payload for a picture that was made upright and may have new sides: the orientation becomes 1,
// the noted pixel size the new one, and the embedded preview picture (IFD1) is unlinked - it would keep showing
// the old picture. Tags the block does not have are not added. False when the block cannot be read.
inline bool FixExifForSavedPicture(std::vector<uint8_t>& exif, uint32_t newW, uint32_t newH) {
    Detail::Tiff t;
    if (!Detail::OpenTiff(exif, t)) return false;
    const uint32_t ifd0 = t.U32(4);
    const uint16_t count0 = t.EntryCount(ifd0);
    if (count0 == 0) return false;
    if (const size_t e = t.FindEntry(ifd0, 0x0112); e && t.U16(e + 2) == 3) t.Put16(e + 8, 1);
    if (const size_t e = t.FindEntry(ifd0, 0x8769); e && t.U16(e + 2) == 4) {           // the EXIF sub-IFD
        const uint32_t sub = t.U32(e + 8);
        for (const auto& [tag, value] : { std::pair<uint16_t, uint32_t>{ 0xA002, newW }, std::pair<uint16_t, uint32_t>{ 0xA003, newH } }) {
            const size_t p = t.FindEntry(sub, tag);
            if (!p) continue;
            if (t.U16(p + 2) == 3 && value <= 0xFFFFu) t.Put16(p + 8, static_cast<uint16_t>(value));
            else if (t.U16(p + 2) == 4) t.Put32(p + 8, value);
        }
    }
    t.Put32(ifd0 + 2u + count0 * 12u, 0);                  // no IFD1, so no preview picture
    return true;
}

// Removes the APP1 EXIF and APP2 colour profile segments of a JPEG and puts the given ones in their place: right
// after SOI and any JFIF APP0 segment. An empty payload puts no EXIF segment. False when the bytes are no JPEG or
// a segment would be too long.
inline bool ReplacePhotoSegments(std::vector<uint8_t>& jpeg, const std::vector<uint8_t>& exifPayload,
                                 const std::vector<std::vector<uint8_t>>& iccSegments) {
    std::vector<Segment> segs;
    if (!ListHeaderSegments(jpeg.data(), jpeg.size(), segs)) return false;
    if (exifPayload.size() > 65533u) return false;
    std::vector<uint8_t> out;
    out.reserve(jpeg.size() + exifPayload.size() + 64);
    out.push_back(0xFF);
    out.push_back(0xD8);
    size_t insertAt = 2;            // after SOI, and after a JFIF APP0 when the file starts with one
    size_t i = 0;
    while (i < segs.size() && segs[i].marker == 0xE0) {
        out.insert(out.end(), jpeg.begin() + segs[i].offset, jpeg.begin() + segs[i].offset + segs[i].size);
        insertAt = segs[i].offset + segs[i].size;
        ++i;
    }
    if (!exifPayload.empty()) {
        const size_t length = exifPayload.size() + 2;
        out.push_back(0xFF);
        out.push_back(0xE1);
        out.push_back(static_cast<uint8_t>(length >> 8));
        out.push_back(static_cast<uint8_t>(length));
        out.insert(out.end(), exifPayload.begin(), exifPayload.end());
    }
    for (const auto& icc : iccSegments) {
        if (icc.size() >= 18 && icc[0] == 0xFF && icc[1] == 0xE2) out.insert(out.end(), icc.begin(), icc.end());
    }
    // Everything else as it was, without the old EXIF and colour profile segments
    size_t copyFrom = insertAt;
    for (; i < segs.size(); ++i) {
        const Segment& s = segs[i];
        if (IsExifSegment(jpeg.data(), s) || IsIccSegment(jpeg.data(), s)) {
            out.insert(out.end(), jpeg.begin() + copyFrom, jpeg.begin() + s.offset);
            copyFrom = s.offset + s.size;
        }
    }
    out.insert(out.end(), jpeg.begin() + copyFrom, jpeg.end());
    jpeg = std::move(out);
    return true;
}

// A JPEG written from another one keeps the source's photo data: its EXIF block (corrected for a picture that is
// upright and newW x newH now) and its colour profile. False when either file cannot be read as a JPEG.
inline bool CarryPhotoSegments(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& written, uint32_t newW, uint32_t newH) {
    std::vector<uint8_t> exif = FindExif(src, srcSize);
    const std::vector<std::vector<uint8_t>> icc = FindIccSegments(src, srcSize);
    if (exif.empty() && icc.empty()) return true;           // nothing to carry
    if (!exif.empty() && !FixExifForSavedPicture(exif, newW, newH)) exif.clear();
    return ReplacePhotoSegments(written, exif, icc);
}

} // namespace Explorer2::JpegSegments
