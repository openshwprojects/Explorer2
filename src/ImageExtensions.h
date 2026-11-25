#pragma once
// ImageExtensions.h — single source of truth for extended image file extensions decoded natively by
// ImageFormatLoader, shared with IsImageExtension in Types.h so the viewer, file list, and preview pane
// never drift out of sync.
#include <string_view>

namespace Explorer2::ImageExt {

bool IsConsoleOrGameImageExt(std::wstring_view extLower);

inline bool IsExtendedImageExt(std::wstring_view extLower) {
    return extLower == L".tga" || extLower == L".icb" || extLower == L".vda" || extLower == L".vst" ||
           extLower == L".psd" || extLower == L".psb" ||
           extLower == L".xcf" ||
           extLower == L".hdr" || extLower == L".pic" ||
           extLower == L".ppm" || extLower == L".pgm" || extLower == L".pbm" || extLower == L".pnm" ||
           extLower == L".pcx" ||
           extLower == L".qoi" ||
           extLower == L".dds" ||
           extLower == L".svg" || extLower == L".svgz" ||
           extLower == L".xbm" ||
           extLower == L".xpm" ||
           extLower == L".pam" ||
           extLower == L".pfm" ||
           extLower == L".sgi" || extLower == L".rgb" || extLower == L".bw"  || extLower == L".rgba" ||
           extLower == L".ras" || extLower == L".sun" ||
           extLower == L".ktx" || extLower == L".ktx2" || extLower == L".basis" ||
           extLower == L".vtf" ||
           extLower == L".ff"  || extLower == L".farbfeld" ||
           extLower == L".fits" || extLower == L".fit" || extLower == L".fts" ||
           extLower == L".pkm" ||
           extLower == L".astc" ||
           extLower == L".pvr" ||
           extLower == L".iff" || extLower == L".ilbm" || extLower == L".lbm" ||
           extLower == L".tim" ||
           extLower == L".wbmp" ||
           extLower == L".bdf" ||
           extLower == L".psf" ||
           extLower == L".y4m" ||
           extLower == L".ani" ||
           extLower == L".gpl" || extLower == L".pal" || extLower == L".act" || extLower == L".aco" || extLower == L".ase" ||
           extLower == L".dcm" || extLower == L".dicom" ||
           extLower == L".exr" ||
           extLower == L".kra" || extLower == L".ora" || extLower == L".procreate" ||
           extLower == L".clip" || extLower == L".aseprite" || extLower == L".icns" ||
           extLower == L".jxl" || extLower == L".bpg" || extLower == L".blp" || extLower == L".utx" ||
           extLower == L".dpx" || extLower == L".cin" || extLower == L".cube" || extLower == L".3dl" ||
           extLower == L".ies" || extLower == L".ttf" || extLower == L".ttc" || extLower == L".otf" || extLower == L".woff" || extLower == L".woff2" ||
           extLower == L".cr2" || extLower == L".cr3" || extLower == L".nef" || extLower == L".arw" ||
           extLower == L".dng" || extLower == L".raf" || extLower == L".orf" || extLower == L".rw2" ||
           extLower == L".pef" || extLower == L".eps" || extLower == L".ai"  || extLower == L".cdr" ||
           extLower == L".afphoto" || extLower == L".afdesign" || extLower == L".afpub" ||
           extLower == L".emf" || extLower == L".wmf" || extLower == L".cgm" ||
           extLower == L".abr" || extLower == L".pat" || extLower == L".dst" || extLower == L".pes" ||
           extLower == L".ans" || extLower == L".sixel" || extLower == L".txd" || extLower == L".uasset" ||
           extLower == L".avif" || extLower == L".heic" || extLower == L".heif" || extLower == L".avci" ||
           extLower == L".jp2"  || extLower == L".j2k"  || extLower == L".jpf"  || extLower == L".jpx"  || extLower == L".j2c" ||
           extLower == L".tif"  || extLower == L".tiff" || extLower == L".btf"  || extLower == L".tf8"  || extLower == L".webp" ||
           extLower == L".cur"  || extLower == L".wal"  || extLower == L".wad"  || extLower == L".spr"  || extLower == L".lmp"  || extLower == L".art" ||
           extLower == L".bntx" || extLower == L".gxt"  || extLower == L".gtf"  || extLower == L".nut"  || extLower == L".tex"  || extLower == L".vtex_c" ||
           extLower == L".pdn"  || extLower == L".jls"  || extLower == L".sketch" ||
           extLower == L".pix"  || extLower == L".als"  || extLower == L".rle"  || extLower == L".miff" ||
           extLower == L".viff" || extLower == L".xv"   || extLower == L".info" ||
           extLower == L".pi1"  || extLower == L".pi2"  || extLower == L".pi3"  ||
           extLower == L".pc1"  || extLower == L".pc2"  || extLower == L".pc3"  ||
           extLower == L".koa"  || extLower == L".kla"  || extLower == L".scr"  ||
           extLower == L".xpr"  || extLower == L".stex" || extLower == L".ctex" ||
           extLower == L".bimage" || extLower == L".xwd" ||
           IsConsoleOrGameImageExt(extLower);
}

// Returns true for any image extension (WIC standard formats + ImageFormatLoader extended formats).
// Note: .fit, .tex, and .als are excluded from generic file-list image classification when inspected without header
// magic because Garmin .fit activity logs, LaTeX .tex documents, and Ableton Live .als projects share those extensions.
inline bool IsAnyImageExt(std::wstring_view extLower) {
    if (extLower == L".png"  || extLower == L".jpg"  || extLower == L".jpeg" ||
        extLower == L".bmp"  || extLower == L".gif"  || extLower == L".ico"  ||
        extLower == L".jfif" || extLower == L".wdp") {
        return true;
    }
    if (extLower == L".fit" || extLower == L".tex" || extLower == L".als") return false;
    return IsExtendedImageExt(extLower);
}

} // namespace Explorer2::ImageExt
