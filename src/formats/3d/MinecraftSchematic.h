#pragma once
// MinecraftSchematic.h — Minecraft Binary NBT Voxel Reader (.schem, .schematic, .nbt)
// Decompresses Gzip / Zlib / Raw Named Binary Tag (NBT) trees and decodes:
//   - Sponge Schematic v1 / v2 (Width, Height, Length, Palette + LEB128 varint BlockData)
//   - Sponge Schematic v3 (Schematic -> Width, Height, Length, Blocks -> Palette + Data)
//   - MCEdit / Schematica Legacy .schematic (Width, Height, Length + Blocks byte array)
//   - Vanilla Minecraft Structure .nbt (size[3], palette / palettes[0], blocks[pos, state])
#include "../../ByteIO.h"
#include "../../ReaderTextUtils.h"
#include "../../ZipDeflate.h"
#include "../common/VoxelMesh3D.h"

namespace Formats {

namespace DetailMinecraftNbt {

enum NbtTagType : uint8_t {
    TAG_End        = 0,
    TAG_Byte       = 1,
    TAG_Short      = 2,
    TAG_Int        = 3,
    TAG_Long       = 4,
    TAG_Float      = 5,
    TAG_Double     = 6,
    TAG_Byte_Array = 7,
    TAG_String     = 8,
    TAG_List       = 9,
    TAG_Compound   = 10,
    TAG_Int_Array  = 11,
    TAG_Long_Array = 12
};

struct NbtNode {
    uint8_t type = TAG_End;
    std::string name;
    int64_t intVal = 0;
    double dblVal = 0.0;
    std::string strVal;
    std::vector<uint8_t> byteArr;
    std::vector<int32_t> intArr;
    std::vector<NbtNode> children; // for TAG_List and TAG_Compound

    const NbtNode* Find(std::string_view childName) const {
        for (const auto& c : children) {
            if (c.name == childName) return &c;
        }
        return nullptr;
    }

    const NbtNode* FindCaseInsensitive(std::string_view childName) const {
        std::string target = ReaderText::ToLowerAscii(childName);
        for (const auto& c : children) {
            if (ReaderText::ToLowerAscii(c.name) == target) return &c;
        }
        return nullptr;
    }
};

class NbtReader {
public:
    NbtReader(const uint8_t* d, size_t s) : data_(d), size_(s) {}

    bool ReadRoot(NbtNode& outRoot) {
        if (pos_ >= size_) return false;
        uint8_t rootType = data_[pos_++];
        if (rootType != TAG_Compound) return false;
        if (!ReadString(outRoot.name)) return false;
        outRoot.type = TAG_Compound;
        return ReadPayload(TAG_Compound, outRoot, 0);
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;

    bool ReadString(std::string& out) {
        if (pos_ + 2 > size_) return false;
        uint16_t len = ByteIO::RdU16BE(data_ + pos_);
        pos_ += 2;
        if (len > size_ - pos_) return false;
        out.assign(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return true;
    }

    bool ReadPayload(uint8_t type, NbtNode& node, int depth) {
        if (depth > 64) return false;
        node.type = type;
        switch (type) {
        case TAG_Byte:
            if (pos_ + 1 > size_) return false;
            node.intVal = static_cast<int8_t>(data_[pos_++]);
            return true;
        case TAG_Short:
            if (pos_ + 2 > size_) return false;
            node.intVal = ByteIO::RdI16BE(data_ + pos_);
            pos_ += 2;
            return true;
        case TAG_Int:
            if (pos_ + 4 > size_) return false;
            node.intVal = ByteIO::RdI32BE(data_ + pos_);
            pos_ += 4;
            return true;
        case TAG_Long:
            if (pos_ + 8 > size_) return false;
            node.intVal = static_cast<int64_t>(ByteIO::RdU64BE(data_ + pos_));
            pos_ += 8;
            return true;
        case TAG_Float:
            if (pos_ + 4 > size_) return false;
            node.dblVal = ByteIO::RdF32BE(data_ + pos_);
            pos_ += 4;
            return true;
        case TAG_Double:
            if (pos_ + 8 > size_) return false;
            node.dblVal = ByteIO::RdF64BE(data_ + pos_);
            pos_ += 8;
            return true;
        case TAG_Byte_Array: {
            if (pos_ + 4 > size_) return false;
            int32_t len = ByteIO::RdI32BE(data_ + pos_);
            pos_ += 4;
            if (len < 0 || static_cast<size_t>(len) > size_ - pos_ || len > 67108864) return false;
            node.byteArr.assign(data_ + pos_, data_ + pos_ + static_cast<size_t>(len));
            pos_ += static_cast<size_t>(len);
            return true;
        }
        case TAG_String:
            return ReadString(node.strVal);
        case TAG_List: {
            if (pos_ + 5 > size_) return false;
            uint8_t elemType = data_[pos_++];
            int32_t len = ByteIO::RdI32BE(data_ + pos_);
            pos_ += 4;
            if (len < 0 || len > 8000000) return false;
            if (len == 0) return true;
            if (elemType == TAG_End || elemType > TAG_Long_Array) return false;
            node.children.resize(static_cast<size_t>(len));
            for (int32_t i = 0; i < len; ++i) {
                if (!ReadPayload(elemType, node.children[static_cast<size_t>(i)], depth + 1)) return false;
            }
            return true;
        }
        case TAG_Compound: {
            while (pos_ < size_) {
                uint8_t subType = data_[pos_++];
                if (subType == TAG_End) return true;
                if (subType > TAG_Long_Array) return false;
                NbtNode child;
                if (!ReadString(child.name)) return false;
                if (!ReadPayload(subType, child, depth + 1)) return false;
                node.children.push_back(std::move(child));
            }
            return false;
        }
        case TAG_Int_Array: {
            if (pos_ + 4 > size_) return false;
            int32_t len = ByteIO::RdI32BE(data_ + pos_);
            pos_ += 4;
            if (len < 0 || static_cast<uint64_t>(len) * 4ULL > size_ - pos_ || len > 16777216) return false;
            node.intArr.resize(static_cast<size_t>(len));
            for (int32_t i = 0; i < len; ++i) {
                node.intArr[static_cast<size_t>(i)] = ByteIO::RdI32BE(data_ + pos_ + static_cast<size_t>(i) * 4u);
            }
            pos_ += static_cast<size_t>(len) * 4u;
            return true;
        }
        case TAG_Long_Array: {
            if (pos_ + 4 > size_) return false;
            int32_t len = ByteIO::RdI32BE(data_ + pos_);
            pos_ += 4;
            if (len < 0 || static_cast<uint64_t>(len) * 8ULL > size_ - pos_ || len > 16777216) return false;
            pos_ += static_cast<size_t>(len) * 8u;
            return true;
        }
        default:
            return false;
        }
    }
};

inline bool IsAirOrInvisibleBlock(std::string_view rawState) {
    size_t brk = rawState.find('[');
    if (brk != std::string_view::npos) rawState = rawState.substr(0, brk);
    std::string low = ReaderText::ToLowerAscii(ReaderText::TrimSv(rawState));
    if (low.empty()) return true;
    return low == "air" ||
           low == "minecraft:air" ||
           low == "minecraft:cave_air" ||
           low == "cave_air" ||
           low == "minecraft:void_air" ||
           low == "void_air" ||
           low == "minecraft:structure_void" ||
           low == "structure_void" ||
           low == "minecraft:light" ||
           low == "minecraft:barrier" ||
           low == "barrier";
}

inline bool DecodeSpongeVarintVolume(
    int32_t width, int32_t height, int32_t length,
    const NbtNode& paletteNode,
    const std::vector<uint8_t>& varintBytes,
    std::vector<VoxelMesh3D::VoxelCoord>& outVoxels)
{
    if (width <= 0 || height <= 0 || length <= 0) return false;
    const uint64_t total = static_cast<uint64_t>(width) * height * length;
    if (total > 16777216ULL || varintBytes.empty()) return false;

    std::unordered_set<int32_t> airIds;
    for (const auto& entry : paletteNode.children) {
        if ((entry.type == TAG_Int || entry.type == TAG_Short || entry.type == TAG_Byte) &&
            IsAirOrInvisibleBlock(entry.name))
        {
            airIds.insert(static_cast<int32_t>(entry.intVal));
        }
    }

    outVoxels.reserve(std::min<size_t>(static_cast<size_t>(total), 262144u));
    size_t p = 0;
    for (int32_t iy = 0; iy < height; ++iy) {
        for (int32_t iz = 0; iz < length; ++iz) {
            for (int32_t ix = 0; ix < width; ++ix) {
                if (p >= varintBytes.size()) return false;
                uint32_t val = 0;
                int shift = 0;
                while (p < varintBytes.size()) {
                    uint8_t b = varintBytes[p++];
                    val |= static_cast<uint32_t>(b & 0x7Fu) << shift;
                    if ((b & 0x80u) == 0u) break;
                    shift += 7;
                    if (shift > 28) return false;
                }
                if (airIds.count(static_cast<int32_t>(val)) == 0) {
                    outVoxels.push_back({ ix, iy, -iz });
                }
            }
        }
    }
    return !outVoxels.empty();
}

} // namespace DetailMinecraftNbt

inline bool ParseMinecraftSchematic(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    LoadedMesh3D& mesh)
{
    using namespace DetailMinecraftNbt;
    if (!data || size < 8) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    std::vector<uint8_t> inflated;
    const uint8_t* nbtPtr = data;
    size_t nbtLen = size;

    if (size >= 10 && data[0] == 0x1Fu && data[1] == 0x8Bu) {
        if (!ZipDeflate::InflateGzip(data, size, inflated, 128 * 1024 * 1024) || inflated.empty()) return false;
        nbtPtr = inflated.data();
        nbtLen = inflated.size();
    } else if (size >= 6 && data[0] == 0x78u &&
               (data[1] == 0x01u || data[1] == 0x5Eu || data[1] == 0x9Cu || data[1] == 0xDAu))
    {
        if (!ZipDeflate::InflateZlib(data, size, inflated, 128 * 1024 * 1024) || inflated.empty()) return false;
        nbtPtr = inflated.data();
        nbtLen = inflated.size();
    } else if (data[0] != TAG_Compound) {
        return false;
    }

    NbtReader reader(nbtPtr, nbtLen);
    NbtNode root;
    if (!reader.ReadRoot(root)) return false;

    // Unwrap Sponge v3 outer {"Schematic": {...}} wrapper if present
    const NbtNode* schem = &root;
    if (const NbtNode* inner = root.Find("Schematic")) {
        if (inner->type == TAG_Compound) schem = inner;
    }

    std::vector<VoxelMesh3D::VoxelCoord> voxels;

    // 1. Sponge Schematic v3: Width, Height, Length + Blocks { Palette, Data }
    const NbtNode* wNode = schem->FindCaseInsensitive("Width");
    const NbtNode* hNode = schem->FindCaseInsensitive("Height");
    const NbtNode* lNode = schem->FindCaseInsensitive("Length");
    if (wNode && hNode && lNode) {
        const int32_t w = static_cast<int32_t>(wNode->intVal);
        const int32_t h = static_cast<int32_t>(hNode->intVal);
        const int32_t l = static_cast<int32_t>(lNode->intVal);

        if (const NbtNode* blocksComp = schem->Find("Blocks")) {
            if (blocksComp->type == TAG_Compound) {
                const NbtNode* pal = blocksComp->Find("Palette");
                const NbtNode* dat = blocksComp->Find("Data");
                if (pal && dat && pal->type == TAG_Compound && dat->type == TAG_Byte_Array) {
                    if (DecodeSpongeVarintVolume(w, h, l, *pal, dat->byteArr, voxels) &&
                        VoxelMesh3D::EmitCulledSparseVoxels(voxels, mesh))
                    {
                        mesh.formatName = L"Minecraft Sponge Schematic v3 (.schem)";
                        return true;
                    }
                }
            }
        }

        // 2. Sponge Schematic v1 / v2: Palette + BlockData
        const NbtNode* palV2 = schem->Find("Palette");
        const NbtNode* bdV2  = schem->Find("BlockData");
        if (palV2 && bdV2 && palV2->type == TAG_Compound && bdV2->type == TAG_Byte_Array) {
            if (DecodeSpongeVarintVolume(w, h, l, *palV2, bdV2->byteArr, voxels) &&
                VoxelMesh3D::EmitCulledSparseVoxels(voxels, mesh))
            {
                mesh.formatName = L"Minecraft Sponge Schematic (.schem)";
                return true;
            }
        }

        // 3. MCEdit / Schematica Legacy Alpha .schematic: Blocks byte array (0 = Air)
        const NbtNode* legacyBlocks = schem->Find("Blocks");
        if (legacyBlocks && legacyBlocks->type == TAG_Byte_Array && w > 0 && h > 0 && l > 0) {
            const uint64_t total = static_cast<uint64_t>(w) * h * l;
            if (total <= 16777216ULL && legacyBlocks->byteArr.size() >= static_cast<size_t>(total)) {
                voxels.reserve(std::min<size_t>(static_cast<size_t>(total), 262144u));
                size_t idx = 0;
                for (int32_t iy = 0; iy < h; ++iy) {
                    for (int32_t iz = 0; iz < l; ++iz) {
                        for (int32_t ix = 0; ix < w; ++ix, ++idx) {
                            if (legacyBlocks->byteArr[idx] != 0u) {
                                voxels.push_back({ ix, iy, -iz });
                            }
                        }
                    }
                }
                if (VoxelMesh3D::EmitCulledSparseVoxels(voxels, mesh)) {
                    mesh.formatName = L"Minecraft MCEdit Schematic (.schematic)";
                    return true;
                }
            }
        }
    }

    // 4. Vanilla Minecraft Structure .nbt: size[3], palette (or palettes[0]), blocks[{pos:[x,y,z], state:int}]
    const NbtNode* palList = root.Find("palette");
    if (!palList) {
        if (const NbtNode* pals = root.Find("palettes")) {
            if (pals->type == TAG_List && !pals->children.empty() && pals->children[0].type == TAG_List) {
                palList = &pals->children[0];
            }
        }
    }
    const NbtNode* blocksList = root.Find("blocks");
    if (palList && blocksList && palList->type == TAG_List && blocksList->type == TAG_List) {
        std::vector<uint8_t> solidState(palList->children.size(), 1u);
        for (size_t i = 0; i < palList->children.size(); ++i) {
            if (const NbtNode* nm = palList->children[i].Find("Name")) {
                if (nm->type == TAG_String && IsAirOrInvisibleBlock(nm->strVal)) {
                    solidState[i] = 0u;
                }
            }
        }

        voxels.reserve(blocksList->children.size());
        for (const auto& blk : blocksList->children) {
            if (blk.type != TAG_Compound) continue;
            const NbtNode* st = blk.Find("state");
            const NbtNode* ps = blk.Find("pos");
            if (!st || !ps) continue;
            int64_t stateIdx = st->intVal;
            if (stateIdx < 0 || static_cast<size_t>(stateIdx) >= solidState.size() || !solidState[static_cast<size_t>(stateIdx)]) {
                continue;
            }
            int32_t bx = 0, by = 0, bz = 0;
            if (ps->type == TAG_List && ps->children.size() >= 3) {
                bx = static_cast<int32_t>(ps->children[0].intVal);
                by = static_cast<int32_t>(ps->children[1].intVal);
                bz = static_cast<int32_t>(ps->children[2].intVal);
            } else if (ps->type == TAG_Int_Array && ps->intArr.size() >= 3) {
                bx = ps->intArr[0];
                by = ps->intArr[1];
                bz = ps->intArr[2];
            } else {
                continue;
            }
            voxels.push_back({ bx, by, -bz });
        }

        if (VoxelMesh3D::EmitCulledSparseVoxels(voxels, mesh)) {
            mesh.formatName = L"Minecraft Structure NBT (.nbt)";
            return true;
        }
    }

    return false;
}

} // namespace Formats
