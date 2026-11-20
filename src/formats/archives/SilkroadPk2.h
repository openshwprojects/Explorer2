#pragma once
// SilkroadPk2.h - Joymax PK2 archives (.pk2: Silkroad Online and other Joymax games).
//
// A 256-byte header ("JoyMax File Manager!\n", a version, an encryption flag) and directory blocks of 20 entries
// of 128 bytes each:
//   type (0 empty, 1 folder, 2 file), name[81], access / create / modify time (FILETIME), position (int64),
//   size (uint32), next block (int64, used in the 20th entry only), 2 bytes of padding.
// The root folder's block is right after the header. A folder entry points at the block of its children, whose
// first two entries are "." and ".."; a block that is full continues at the next block the 20th entry names. A
// file entry points at its data, which is stored as it is.
//
// The entries are encrypted with Blowfish (ECB, block by block) under the key every Joymax client uses: "169841"
// with each byte XORed with a fixed base key. Implementations differ in how they read the 8-byte blocks (as two
// little- or big-endian words) and some archives use the key without the XOR: the reader tries each way on the
// root block and keeps the one under which it reads as a directory.
//
// The cipher itself is in Blowfish.h.

#include "../common/ArchiveFormatCommon.h"
#include "../../Blowfish.h"
#include <set>

namespace Explorer2::Formats {

namespace Pk2Detail {

using ::Explorer2::ByteIO::RdU32LE;

// One way of reading the entries: a key, and the byte order of the two words of a block
struct Pk2Cipher {
    Blowfish::Cipher fish;
    bool littleEndianWords = true;
    bool encrypted = true;

    static uint32_t Word(const uint8_t* p, bool le) {
        return le ? (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24))
                  : ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3]);
    }
    static void PutWord(uint8_t* p, uint32_t v, bool le) {
        for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(le ? (v >> (8 * i)) : (v >> (24 - 8 * i)));
    }
    // Decrypts (or encrypts) whole 8-byte blocks in place
    void Apply(uint8_t* p, size_t n, bool decrypt) const {
        if (!encrypted) return;
        for (size_t at = 0; at + 8 <= n; at += 8) {
            uint32_t l = Word(p + at, littleEndianWords), r = Word(p + at + 4, littleEndianWords);
            if (decrypt) fish.Decrypt(l, r);
            else fish.Encrypt(l, r);
            PutWord(p + at, l, littleEndianWords);
            PutWord(p + at + 4, r, littleEndianWords);
        }
    }
};

inline constexpr size_t kHeaderSize = 256;
inline constexpr size_t kEntrySize = 128;
inline constexpr size_t kEntriesPerBlock = 20;
inline constexpr size_t kBlockSize = kEntrySize * kEntriesPerBlock;

// The key every Joymax client uses
inline std::vector<uint8_t> JoymaxKey(bool xorWithBase) {
    static const uint8_t kBase[] = { 0x03, 0xF8, 0xE4, 0x44, 0x88, 0x99, 0x3F, 0x64, 0xFE, 0x35 };
    const char* ascii = "169841";
    std::vector<uint8_t> key(6);
    for (size_t i = 0; i < 6; ++i) key[i] = static_cast<uint8_t>(ascii[i]) ^ (xorWithBase ? kBase[i] : 0);
    return key;
}

struct Pk2Entry {
    uint8_t type = 0;
    std::string name;
    uint64_t modifyTime = 0;
    uint64_t position = 0;
    uint32_t size = 0;
    uint64_t nextChain = 0;
};

inline bool ReadEntry(const uint8_t* data, size_t size, uint64_t at, const Pk2Cipher& cipher, Pk2Entry& out) {
    if (at > size || size - at < kEntrySize) return false;
    uint8_t e[kEntrySize];
    std::memcpy(e, data + at, kEntrySize);
    cipher.Apply(e, kEntrySize, true);
    out.type = e[0];
    out.name.assign(reinterpret_cast<const char*>(e + 1), strnlen(reinterpret_cast<const char*>(e + 1), 81));
    std::memcpy(&out.modifyTime, e + 98, 8);
    std::memcpy(&out.position, e + 106, 8);
    out.size = RdU32LE(e + 114);
    std::memcpy(&out.nextChain, e + 118, 8);
    return true;
}

// Whether the root block reads as a directory under this cipher: its first entry is the folder "." pointing at
// the block itself, or at least every entry has a known type and a printable name
inline bool RootLooksRight(const uint8_t* data, size_t size, const Pk2Cipher& cipher) {
    Pk2Entry first;
    if (!ReadEntry(data, size, kHeaderSize, cipher, first)) return false;
    if (first.type == 1 && first.name == "." && first.position == kHeaderSize) return true;
    int named = 0;
    for (size_t i = 0; i < kEntriesPerBlock; ++i) {
        Pk2Entry e;
        if (!ReadEntry(data, size, kHeaderSize + i * kEntrySize, cipher, e) || e.type > 2) return false;
        if (e.type == 0) continue;
        if (e.name.empty()) return false;
        for (unsigned char c : e.name) if (c < 0x20 || c == 0x7F) return false;
        ++named;
    }
    return named > 0;
}

// The cipher under which the archive reads as one; false when none does
inline bool FindCipher(const uint8_t* data, size_t size, Pk2Cipher& out) {
    if (size < kHeaderSize + kBlockSize) return false;
    if (data[34] == 0) {                        // the header says the entries are not encrypted
        out.encrypted = false;
        if (RootLooksRight(data, size, out)) return true;
    }
    for (bool xorKey : { true, false }) {
        const std::vector<uint8_t> key = JoymaxKey(xorKey);
        Pk2Cipher c;
        c.fish.Initialize(key.data(), key.size());
        for (bool le : { true, false }) {
            c.littleEndianWords = le;
            if (RootLooksRight(data, size, c)) {
                out = c;
                return true;
            }
        }
    }
    return false;
}

} // namespace Pk2Detail

inline bool InspectSilkroadPk2(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    using namespace Pk2Detail;
    if (!data || size < kHeaderSize + kBlockSize || std::memcmp(data, "JoyMax File Manager", 19) != 0) return false;
    Pk2Cipher cipher;
    if (!FindCipher(data, size, cipher)) return false;
    outInfo.formatName = cipher.encrypted ? L"Joymax PK2 (Silkroad Online)" : L"Joymax PK2 (Silkroad Online, not encrypted)";

    // The folders, walked from the root; a block is read once at most (a damaged chain cannot loop)
    struct Pending { uint64_t block; std::wstring prefix; int depth; };
    std::vector<Pending> todo = { { kHeaderSize, L"", 0 } };
    std::set<uint64_t> seenBlocks;
    size_t entriesRead = 0;
    while (!todo.empty() && entriesRead < 2000000) {
        Pending p = std::move(todo.back());
        todo.pop_back();
        uint64_t block = p.block;
        while (block != 0 && seenBlocks.insert(block).second && block <= size && size - block >= kBlockSize) {
            uint64_t next = 0;
            for (size_t i = 0; i < kEntriesPerBlock; ++i) {
                Pk2Entry e;
                if (!ReadEntry(data, size, block + i * kEntrySize, cipher, e)) break;
                ++entriesRead;
                if (i + 1 == kEntriesPerBlock) next = e.nextChain;
                if (e.type == 0 || e.name.empty() || e.name == "." || e.name == "..") continue;
                const std::wstring name = PathUtils::Utf8ToWide(e.name);
                const std::wstring path = ArchivePath::Normalize(p.prefix + name);
                if (path.empty()) continue;
                if (e.type == 1) {
                    if (p.depth < 64 && e.position != 0) todo.push_back({ e.position, path + L"/", p.depth + 1 });
                    ArchiveItemEntry dir;
                    dir.pathInArchive = path;
                    dir.name = ArchivePath::BaseName(path);
                    dir.isDirectory = true;
                    outInfo.entries.push_back(std::move(dir));
                } else if (e.type == 2 && e.position <= size && size - e.position >= e.size) {
                    ArchiveItemEntry item;
                    item.pathInArchive = path;
                    item.name = ArchivePath::BaseName(path);
                    item.uncompressedSize = e.size;
                    item.compressedSize = e.size;
                    item.rawDataOffset = e.position;
                    item.compressionMethod = 0;            // stored as it is
                    if (e.modifyTime) DetailArc100::FileTimeU64ToDos(e.modifyTime, item.dosDate, item.dosTime);
                    outInfo.entries.push_back(std::move(item));
                }
            }
            block = next;
        }
    }
    for (const auto& e : outInfo.entries) {
        if (!e.isDirectory) { outInfo.valid = true; break; }
    }
    return outInfo.valid;
}

} // namespace Explorer2::Formats
