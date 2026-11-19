#pragma once
// NiftiVolume.h — NIfTI-1 & NIfTI-2 Neuroimaging Volume (.nii / .nii.gz) Reader

namespace Formats {
namespace GeoSciDetail {

// ============================================================================
// 5. NIfTI-1 & NIfTI-2 3D/4D Neuroimaging Volume (.nii / .nii.gz)
// ============================================================================
inline const wchar_t* NiftiDatatypeName(int16_t dt) {
    switch (dt) {
        case 2:    return L"UINT8";
        case 4:    return L"INT16";
        case 8:    return L"INT32";
        case 16:   return L"FLOAT32";
        case 32:   return L"COMPLEX64";
        case 64:   return L"FLOAT64";
        case 128:  return L"RGB24";
        case 256:  return L"INT8";
        case 512:  return L"UINT16";
        case 768:  return L"UINT32";
        case 1024: return L"INT64";
        case 1280: return L"UINT64";
        default:   return L"VOXEL";
    }
}

inline bool ParseNiftiVolume(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<uint8_t> uncomp;
    const uint8_t* ptr = raw.data();
    size_t len = raw.size();
    bool wasGzip = false;

    if (len >= 10 && ptr[0] == 0x1Fu && ptr[1] == 0x8Bu) {
        if (!ZipDeflate::InflateGzip(ptr, len, uncomp, 128 * 1024 * 1024) || uncomp.empty()) return false;
        ptr = uncomp.data();
        len = uncomp.size();
        wasGzip = true;
    }
    if (len < 348) return false;

    int32_t hdrLE = RdI32LE(ptr);
    int32_t hdrBE = RdI32BE(ptr);
    bool nifti2 = false;
    bool be = false;
    if (hdrLE == 348) { nifti2 = false; be = false; }
    else if (hdrBE == 348) { nifti2 = false; be = true; }
    else if (hdrLE == 540 && len >= 540) { nifti2 = true; be = false; }
    else if (hdrBE == 540 && len >= 540) { nifti2 = true; be = true; }
    else return false;

    auto rdI16 = [&](size_t o) { return be ? RdI16BE(ptr + o) : RdI16LE(ptr + o); };
    auto rdI32 = [&](size_t o) { return be ? RdI32BE(ptr + o) : RdI32LE(ptr + o); };
    auto rdI64 = [&](size_t o) { return be ? RdI64BE(ptr + o) : RdI64LE(ptr + o); };
    auto rdF32 = [&](size_t o) { return be ? RdF32BE(ptr + o) : RdF32LE(ptr + o); };
    auto rdF64 = [&](size_t o) { return be ? RdF64BE(ptr + o) : RdF64LE(ptr + o); };

    int64_t dim[8] = {};
    double pixdim[8] = {};
    int16_t datatype = 0, bitpix = 0;
    int64_t voxOffset = 0;
    double sclSlope = 1.0, sclInter = 0.0;
    uint8_t xyztUnits = 0;
    std::string descrip;
    int32_t qformCode = 0, sformCode = 0;
    double srowX[4] = {}, srowY[4] = {}, srowZ[4] = {};

    if (!nifti2) {
        for (int i = 0; i < 8; ++i) dim[i] = rdI16(40 + static_cast<size_t>(i) * 2);
        if (dim[0] < 1 || dim[0] > 7) return false;
        datatype = rdI16(70);
        bitpix   = rdI16(72);
        for (int i = 0; i < 8; ++i) pixdim[i] = rdF32(76 + static_cast<size_t>(i) * 4);
        voxOffset = static_cast<int64_t>(rdF32(108));
        sclSlope  = rdF32(112);
        sclInter  = rdF32(116);
        xyztUnits = ptr[123];
        descrip.assign(reinterpret_cast<const char*>(ptr + 148), 80);
        qformCode = rdI16(252);
        sformCode = rdI16(254);
        for (int i = 0; i < 4; ++i) {
            srowX[i] = rdF32(280 + static_cast<size_t>(i) * 4);
            srowY[i] = rdF32(296 + static_cast<size_t>(i) * 4);
            srowZ[i] = rdF32(312 + static_cast<size_t>(i) * 4);
        }
    } else {
        if (std::memcmp(ptr + 4, "n+2", 3) != 0 && std::memcmp(ptr + 4, "ni2", 3) != 0) return false;
        datatype = rdI16(12);
        bitpix   = rdI16(14);
        for (int i = 0; i < 8; ++i) dim[i] = rdI64(16 + static_cast<size_t>(i) * 8);
        if (dim[0] < 1 || dim[0] > 7) return false;
        for (int i = 0; i < 8; ++i) pixdim[i] = rdF64(104 + static_cast<size_t>(i) * 8);
        voxOffset = rdI64(168);
        sclSlope  = rdF64(176);
        sclInter  = rdF64(184);
        descrip.assign(reinterpret_cast<const char*>(ptr + 240), 80);
        qformCode = rdI32(344);
        sformCode = rdI32(348);
        for (int i = 0; i < 4; ++i) {
            srowX[i] = rdF64(400 + static_cast<size_t>(i) * 8);
            srowY[i] = rdF64(432 + static_cast<size_t>(i) * 8);
            srowZ[i] = rdF64(464 + static_cast<size_t>(i) * 8);
        }
        xyztUnits = static_cast<uint8_t>(rdI32(500) & 0xFF);
    }

    while (!descrip.empty() && (descrip.back() == '\0' || descrip.back() == ' ')) descrip.pop_back();
    if (sclSlope == 0.0 || !std::isfinite(sclSlope)) { sclSlope = 1.0; }
    if (!std::isfinite(sclInter)) { sclInter = 0.0; }

    int64_t nx = std::max<int64_t>(1, dim[1]);
    int64_t ny = std::max<int64_t>(1, dim[2]);
    int64_t nz = std::max<int64_t>(1, dim[0] >= 3 ? dim[3] : 1);
    int64_t nt = std::max<int64_t>(1, dim[0] >= 4 ? dim[4] : 1);

    const wchar_t* spaceUnit = ((xyztUnits & 0x07u) == 1) ? L"m" : (((xyztUnits & 0x07u) == 2) ? L"mm" : (((xyztUnits & 0x07u) == 3) ? L"um" : L"vox"));

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"NIfTI_Header_And_Stats";
    hdrSh.rows.push_back({ L"Neuroimaging Attribute", L"Value" });
    hdrSh.rows.push_back({ L"Format", std::wstring(nifti2 ? L"NIfTI-2 (540B)" : L"NIfTI-1 (348B)") + (wasGzip ? L" [GZip Compressed]" : L"") });
    hdrSh.rows.push_back({ L"Voxel Grid Dimensions", std::to_wstring(nx) + L" \xD7 " + std::to_wstring(ny) + L" \xD7 " + std::to_wstring(nz) + (nt > 1 ? (L" \xD7 " + std::to_wstring(nt)) : L"") + L" (" + std::to_wstring(dim[0]) + L"D)" });
    hdrSh.rows.push_back({ L"Voxel Spacing (pixdim)", FmtNum(pixdim[1]) + L" \xD7 " + FmtNum(pixdim[2]) + L" \xD7 " + FmtNum(pixdim[3]) + L" " + spaceUnit });
    hdrSh.rows.push_back({ L"Voxel Data Type", std::wstring(NiftiDatatypeName(datatype)) + L" (" + std::to_wstring(bitpix) + L" bits/voxel)" });
    hdrSh.rows.push_back({ L"Voxel Buffer Offset", std::to_wstring(voxOffset) + L" B" });
    hdrSh.rows.push_back({ L"Intensity Scaling", L"slope=" + FmtNum(sclSlope) + L", inter=" + FmtNum(sclInter) });
    if (!descrip.empty()) hdrSh.rows.push_back({ L"Description", PathUtils::Utf8ToWide(descrip) });
    hdrSh.rows.push_back({ L"Coordinate Codes", L"qform=" + std::to_wstring(qformCode) + L", sform=" + std::to_wstring(sformCode) });
    if (sformCode > 0) {
        hdrSh.rows.push_back({ L"Affine srow_x", L"[" + FmtNum(srowX[0]) + L", " + FmtNum(srowX[1]) + L", " + FmtNum(srowX[2]) + L", " + FmtNum(srowX[3]) + L"]" });
        hdrSh.rows.push_back({ L"Affine srow_y", L"[" + FmtNum(srowY[0]) + L", " + FmtNum(srowY[1]) + L", " + FmtNum(srowY[2]) + L", " + FmtNum(srowY[3]) + L"]" });
        hdrSh.rows.push_back({ L"Affine srow_z", L"[" + FmtNum(srowZ[0]) + L", " + FmtNum(srowZ[1]) + L", " + FmtNum(srowZ[2]) + L", " + FmtNum(srowZ[3]) + L"]" });
    }

    SpreadsheetSheet sliceSh;
    sliceSh.name = L"NIfTI_Center_Slice";

    size_t hdrMin = nifti2 ? 540 : 348;
    size_t vOff = (voxOffset >= static_cast<int64_t>(hdrMin)) ? static_cast<size_t>(voxOffset) : hdrMin;
    size_t bytesPerVox = (datatype == 2 || datatype == 256) ? 1 :
                         (datatype == 4 || datatype == 512) ? 2 :
                         (datatype == 8 || datatype == 16 || datatype == 768) ? 4 :
                         (datatype == 64 || datatype == 1024 || datatype == 1280) ? 8 :
                         (datatype == 128) ? 3 : 0;

    if (bytesPerVox > 0 && vOff + bytesPerVox <= len) {
        auto readVox = [&](size_t idx) -> double {
            const uint8_t* p = ptr + vOff + idx * bytesPerVox;
            double rv = 0.0;
            switch (datatype) {
                case 2:    rv = p[0]; break;
                case 256:  rv = static_cast<int8_t>(p[0]); break;
                case 4:    rv = be ? RdI16BE(p) : RdI16LE(p); break;
                case 512:  rv = be ? RdU16BE(p) : RdU16LE(p); break;
                case 8:    rv = be ? RdI32BE(p) : RdI32LE(p); break;
                case 768:  rv = be ? RdU32BE(p) : RdU32LE(p); break;
                case 16:   rv = be ? RdF32BE(p) : RdF32LE(p); break;
                case 64:   rv = be ? RdF64BE(p) : RdF64LE(p); break;
                case 1024: rv = static_cast<double>(be ? RdI64BE(p) : RdI64LE(p)); break;
                case 1280: rv = static_cast<double>(be ? RdU64BE(p) : RdU64LE(p)); break;
                case 128:  rv = (static_cast<double>(p[0]) + p[1] + p[2]) / 3.0; break;
                default:   rv = 0.0; break;
            }
            return rv * sclSlope + sclInter;
        };

        size_t availVoxels = (len - vOff) / bytesPerVox;
        uint64_t declaredVox = static_cast<uint64_t>(nx) * static_cast<uint64_t>(ny) * static_cast<uint64_t>(nz) * static_cast<uint64_t>(nt);
        size_t statCap = static_cast<size_t>(std::min<uint64_t>({ static_cast<uint64_t>(availVoxels), declaredVox, 500000ULL }));

        if (statCap > 0) {
            double mn = 0.0, mx = 0.0, sm = 0.0;
            size_t finCnt = 0, nzCnt = 0;
            for (size_t i = 0; i < statCap; ++i) {
                double v = readVox(i);
                if (std::isfinite(v)) {
                    if (finCnt == 0) { mn = v; mx = v; }
                    else { mn = std::min(mn, v); mx = std::max(mx, v); }
                    sm += v;
                    ++finCnt;
                    if (std::fabs(v) > 1e-9) ++nzCnt;
                }
            }
            if (finCnt > 0) {
                hdrSh.rows.push_back({ L"Voxel Min Intensity", FmtNum(mn) });
                hdrSh.rows.push_back({ L"Voxel Max Intensity", FmtNum(mx) });
                hdrSh.rows.push_back({ L"Voxel Mean Intensity", FmtNum(sm / static_cast<double>(finCnt)) });
                hdrSh.rows.push_back({ L"Non-Zero Voxels Sampled", std::to_wstring(nzCnt) + L" / " + std::to_wstring(finCnt) });
            }

            // Center axial slice Z = nz / 2
            size_t zMid = static_cast<size_t>(nz / 2);
            size_t cols = static_cast<size_t>(std::min<int64_t>(nx, 48));
            size_t rows = static_cast<size_t>(std::min<int64_t>(ny, 64));
            size_t stepX = std::max<size_t>(1, static_cast<size_t>(nx) / cols);
            size_t stepY = std::max<size_t>(1, static_cast<size_t>(ny) / rows);

            std::vector<std::wstring> hr = { L"Y \\ X (Z=" + std::to_wstring(zMid) + L")" };
            for (size_t x = 0; x < cols; ++x) hr.push_back(L"X=" + std::to_wstring(x * stepX));
            sliceSh.rows.push_back(std::move(hr));

            for (size_t y = 0; y < rows; ++y) {
                size_t yy = y * stepY;
                std::vector<std::wstring> rv = { L"Y=" + std::to_wstring(yy) };
                for (size_t x = 0; x < cols; ++x) {
                    size_t xx = x * stepX;
                    uint64_t idx = xx + yy * static_cast<uint64_t>(nx) + zMid * static_cast<uint64_t>(nx) * static_cast<uint64_t>(ny);
                    rv.push_back(idx < availVoxels ? FmtNum(readVox(static_cast<size_t>(idx)), 4) : L"");
                }
                sliceSh.rows.push_back(std::move(rv));
            }
            FinalizeCols(sliceSh);
        }
    }
    FinalizeCols(hdrSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = nifti2 ? L"NIfTI-2 Neuroimaging Volume (.nii)" : L"NIfTI-1 Neuroimaging Volume (.nii)";
    out.sheets.push_back(std::move(hdrSh));
    if (sliceSh.rows.size() > 1) out.sheets.push_back(std::move(sliceSh));
    out.valid = true;
    return true;
}

} // namespace GeoSciDetail

inline bool ParseNiftiVolumeDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return GeoSciDetail::ParseNiftiVolume(raw, out); }

} // namespace Formats
