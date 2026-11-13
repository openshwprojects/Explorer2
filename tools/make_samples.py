#!/usr/bin/env python3
"""Generates the self-made sample files in testdata/.

These samples replace third-party files whose licence does not allow them to be shipped with
Explorer2. Everything written here is computed from the formulas in this file, so the output
contains nothing copied from anywhere else, and is covered by the repository's own licence.

Usage:  python tools/make_samples.py            (writes into ../testdata next to this script)
        python tools/make_samples.py <folder>   (writes into another folder)

The output is deterministic: running the script again reproduces the same bytes.
"""

import math
import struct
import sys
from pathlib import Path


# ---------------------------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------------------------

def uv_sphere(rings, segments, radius=1.0, squash=1.0):
    """Returns (vertices, triangles) of a sphere. Z is up; squash scales the height."""
    verts = [(0.0, 0.0, radius * squash)]
    for r in range(1, rings):
        phi = math.pi * r / rings
        for s in range(segments):
            theta = 2.0 * math.pi * s / segments
            verts.append((radius * math.sin(phi) * math.cos(theta),
                          radius * math.sin(phi) * math.sin(theta),
                          radius * math.cos(phi) * squash))
    verts.append((0.0, 0.0, -radius * squash))
    south = len(verts) - 1

    def ring_index(r, s):
        return 1 + (r - 1) * segments + (s % segments)

    tris = []
    for s in range(segments):
        tris.append((0, ring_index(1, s), ring_index(1, s + 1)))
    for r in range(1, rings - 1):
        for s in range(segments):
            a, b = ring_index(r, s), ring_index(r, s + 1)
            c, d = ring_index(r + 1, s), ring_index(r + 1, s + 1)
            tris.append((a, c, d))
            tris.append((a, d, b))
    for s in range(segments):
        tris.append((south, ring_index(rings - 1, s + 1), ring_index(rings - 1, s)))
    return verts, tris


def torus(major_segments, minor_segments, major_radius=2.0, minor_radius=0.7):
    """Returns (vertices, triangles) of a torus lying in the XY plane."""
    verts = []
    for i in range(major_segments):
        u = 2.0 * math.pi * i / major_segments
        for j in range(minor_segments):
            v = 2.0 * math.pi * j / minor_segments
            ring = major_radius + minor_radius * math.cos(v)
            verts.append((ring * math.cos(u), ring * math.sin(u), minor_radius * math.sin(v)))

    def index(i, j):
        return (i % major_segments) * minor_segments + (j % minor_segments)

    tris = []
    for i in range(major_segments):
        for j in range(minor_segments):
            a, b = index(i, j), index(i + 1, j)
            c, d = index(i + 1, j + 1), index(i, j + 1)
            tris.append((a, b, c))
            tris.append((a, c, d))
    return verts, tris


def icosahedron(radius=1.0):
    """Returns (vertices, triangles) of a regular icosahedron."""
    t = (1.0 + math.sqrt(5.0)) / 2.0
    raw = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0),
           (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
           (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    scale = radius / math.sqrt(1.0 + t * t)
    verts = [(x * scale, y * scale, z * scale) for x, y, z in raw]
    tris = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
            (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
            (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
            (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    return verts, tris


def face_normal(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    return nx / length, ny / length, nz / length


# ---------------------------------------------------------------------------------------------
# Writers
# ---------------------------------------------------------------------------------------------

def chunk_3ds(chunk_id, payload):
    return struct.pack('<HI', chunk_id, 6 + len(payload)) + payload


def write_3ds(path, name, verts, tris):
    """Autodesk 3D Studio mesh: one named object with a vertex list and a face list."""
    vertex_list = struct.pack('<H', len(verts)) + b''.join(struct.pack('<3f', *v) for v in verts)
    # Each face: three vertex indices and a flags word (7 = all three edges visible)
    face_list = struct.pack('<H', len(tris)) + b''.join(struct.pack('<4H', a, b, c, 7) for a, b, c in tris)
    trimesh = chunk_3ds(0x4100, chunk_3ds(0x4110, vertex_list) + chunk_3ds(0x4120, face_list))
    obj = chunk_3ds(0x4000, name.encode('ascii') + b'\x00' + trimesh)
    editor = chunk_3ds(0x3D3D, chunk_3ds(0x3D3E, struct.pack('<I', 3)) + obj)
    main = chunk_3ds(0x4D4D, chunk_3ds(0x0002, struct.pack('<I', 3)) + editor)
    path.write_bytes(main)


def write_md2(path, frames, tris, skin_size=64):
    """Quake II model. `frames` is a list of (name, vertices); every frame has the same vertex count."""
    num_xyz = len(frames[0][1])
    st = []
    for x, y, z in frames[0][1]:
        # A simple cylindrical texture mapping, only so that the file carries valid coordinates
        u = (math.atan2(y, x) / (2.0 * math.pi) + 0.5) * (skin_size - 1)
        v = (0.5 - 0.5 * max(-1.0, min(1.0, z))) * (skin_size - 1)
        st.append((int(round(u)), int(round(v))))

    frame_size = 40 + num_xyz * 4
    ofs_st = 68
    ofs_tris = ofs_st + len(st) * 4
    ofs_frames = ofs_tris + len(tris) * 12
    ofs_end = ofs_frames + frame_size * len(frames)

    out = bytearray()
    out += struct.pack('<4s16i', b'IDP2', 8, skin_size, skin_size, frame_size,
                       0, num_xyz, len(st), len(tris), 0, len(frames),
                       68, ofs_st, ofs_tris, ofs_frames, ofs_end, ofs_end)
    for s, t in st:
        out += struct.pack('<2h', s, t)
    for a, b, c in tris:
        out += struct.pack('<6H', a, b, c, a, b, c)
    for name, verts in frames:
        lo = [min(v[i] for v in verts) for i in range(3)]
        hi = [max(v[i] for v in verts) for i in range(3)]
        scale = [((hi[i] - lo[i]) / 255.0) or 1.0 for i in range(3)]
        out += struct.pack('<3f3f16s', *scale, *lo, name.encode('ascii'))
        for v in verts:
            packed = [max(0, min(255, int(round((v[i] - lo[i]) / scale[i])))) for i in range(3)]
            out += struct.pack('<4B', packed[0], packed[1], packed[2], 0)
    assert len(out) == ofs_end
    path.write_bytes(bytes(out))


def write_stl_ascii(path, name, verts, tris):
    lines = ['solid ' + name]
    for a, b, c in tris:
        n = face_normal(verts[a], verts[b], verts[c])
        lines.append('  facet normal %.6f %.6f %.6f' % n)
        lines.append('    outer loop')
        for i in (a, b, c):
            lines.append('      vertex %.6f %.6f %.6f' % verts[i])
        lines.append('    endloop')
        lines.append('  endfacet')
    lines.append('endsolid ' + name)
    path.write_bytes(('\n'.join(lines) + '\n').encode('ascii'))


def write_obj_with_normals(path, name, verts, tris):
    """Wavefront OBJ using the "v//vn" face form, one normal per face."""
    lines = ['# Explorer2 sample model, generated by tools/make_samples.py', 'o ' + name]
    lines += ['v %.6f %.6f %.6f' % v for v in verts]
    lines += ['vn %.6f %.6f %.6f' % face_normal(verts[a], verts[b], verts[c]) for a, b, c in tris]
    lines.append('g ' + name + '_surface')
    for n, (a, b, c) in enumerate(tris, start=1):
        lines.append('f %d//%d %d//%d %d//%d' % (a + 1, n, b + 1, n, c + 1, n))
    path.write_bytes(('\n'.join(lines) + '\n').encode('ascii'))


def write_ply_ascii(path, verts, faces):
    lines = ['ply', 'format ascii 1.0', 'comment Explorer2 sample, generated by tools/make_samples.py',
             'element vertex %d' % len(verts), 'property float x', 'property float y', 'property float z',
             'element face %d' % len(faces), 'property list uchar int vertex_indices', 'end_header']
    lines += ['%.6f %.6f %.6f' % v for v in verts]
    lines += [' '.join([str(len(f))] + [str(i) for i in f]) for f in faces]
    path.write_bytes(('\n'.join(lines) + '\n').encode('ascii'))


def write_off(path, verts, faces):
    lines = ['OFF', '# Explorer2 sample, generated by tools/make_samples.py',
             '%d %d %d' % (len(verts), len(faces), 0)]
    lines += ['%.6f %.6f %.6f' % v for v in verts]
    lines += [' '.join([str(len(f))] + [str(i) for i in f]) for f in faces]
    path.write_bytes(('\n'.join(lines) + '\n').encode('ascii'))


def write_vtk_polydata(path, title, verts, tris):
    lines = ['# vtk DataFile Version 3.0', title, 'ASCII', 'DATASET POLYDATA', 'POINTS %d float' % len(verts)]
    lines += ['%.6f %.6f %.6f' % v for v in verts]
    lines.append('POLYGONS %d %d' % (len(tris), len(tris) * 4))
    lines += ['3 %d %d %d' % t for t in tris]
    path.write_bytes(('\n'.join(lines) + '\n').encode('ascii'))


def write_dbf(path, fields, rows):
    """dBASE III table. fields: (name, type, length, decimals); rows: tuples of already formatted strings."""
    record_len = 1 + sum(f[2] for f in fields)
    header_len = 32 + 32 * len(fields) + 1
    out = bytearray(struct.pack('<B3BIHH20x', 0x03, 126, 11, 12, len(rows), header_len, record_len))
    for name, ftype, length, decimals in fields:
        out += struct.pack('<11sc4xBB14x', name.encode('ascii'), ftype.encode('ascii'), length, decimals)
    out += b'\x0D'
    for row in rows:
        out += b' '
        for (name, ftype, length, decimals), value in zip(fields, row):
            text = value.encode('ascii')
            out += text.rjust(length) if ftype == 'N' else text.ljust(length)
    out += b'\x1A'
    path.write_bytes(bytes(out))


def write_zip(path, entries):
    """Writes a ZIP with fixed timestamps so the output is reproducible. entries: (name, bytes, stored)."""
    import zipfile
    with zipfile.ZipFile(path, 'w') as zf:
        for name, data, stored in entries:
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 12, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, data)


def xml_escape(text):
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def write_3mf(path, verts, tris):
    model = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">',
             ' <metadata name="Application">Explorer2 tools/make_samples.py</metadata>',
             ' <resources>', '  <object id="1" type="model">', '   <mesh>', '    <vertices>']
    model += ['     <vertex x="%.3f" y="%.3f" z="%.3f"/>' % v for v in verts]
    model += ['    </vertices>', '    <triangles>']
    model += ['     <triangle v1="%d" v2="%d" v3="%d"/>' % t for t in tris]
    model += ['    </triangles>', '   </mesh>', '  </object>', ' </resources>',
              ' <build>', '  <item objectid="1"/>', ' </build>', '</model>', '']
    content_types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                     '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
                     ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
                     ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n'
                     '</Types>\n')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
            ' <Relationship Target="/3D/3dmodel.model" Id="rel0" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n'
            '</Relationships>\n')
    write_zip(path, [('[Content_Types].xml', content_types.encode(), False),
                     ('_rels/.rels', rels.encode(), False),
                     ('3D/3dmodel.model', '\n'.join(model).encode(), False)])


ODF_NS = ('xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
          'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
          'xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" '
          'xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" '
          'xmlns:presentation="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0" '
          'xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"')


def odf_package(path, mime, content_xml):
    manifest = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                '<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">\n'
                f' <manifest:file-entry manifest:full-path="/" manifest:version="1.2" manifest:media-type="{mime}"/>\n'
                ' <manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>\n'
                '</manifest:manifest>\n')
    write_zip(path, [('mimetype', mime.encode(), True),
                     ('content.xml', content_xml.encode('utf-8'), False),
                     ('META-INF/manifest.xml', manifest.encode(), False)])


def write_ods(path, sheets):
    """sheets: list of (name, rows); a cell is a number or a string."""
    body = []
    for name, rows in sheets:
        body.append(f'<table:table table:name="{xml_escape(name)}">')
        for row in rows:
            body.append('<table:table-row>')
            for cell in row:
                if isinstance(cell, (int, float)):
                    body.append(f'<table:table-cell office:value-type="float" office:value="{cell}">'
                                f'<text:p>{cell}</text:p></table:table-cell>')
                else:
                    body.append('<table:table-cell office:value-type="string">'
                                f'<text:p>{xml_escape(cell)}</text:p></table:table-cell>')
            body.append('</table:table-row>')
        body.append('</table:table>')
    content = (f'<?xml version="1.0" encoding="UTF-8"?>\n<office:document-content {ODF_NS} office:version="1.2">'
               '<office:body><office:spreadsheet>' + ''.join(body) +
               '</office:spreadsheet></office:body></office:document-content>\n')
    odf_package(path, 'application/vnd.oasis.opendocument.spreadsheet', content)


def write_odp(path, slides):
    """slides: list of (title, bullet lines). Each slide also gets two plain shapes."""
    pages = []
    for number, (title, bullets) in enumerate(slides, start=1):
        items = ''.join(f'<text:p>{xml_escape(line)}</text:p>' for line in bullets)
        pages.append(
            f'<draw:page draw:name="page{number}">'
            '<draw:frame presentation:class="title" svg:x="2cm" svg:y="1cm" svg:width="24cm" svg:height="3cm">'
            f'<draw:text-box><text:p>{xml_escape(title)}</text:p></draw:text-box></draw:frame>'
            '<draw:frame presentation:class="outline" svg:x="2cm" svg:y="5cm" svg:width="16cm" svg:height="10cm">'
            f'<draw:text-box>{items}</draw:text-box></draw:frame>'
            '<draw:rect svg:x="20cm" svg:y="6cm" svg:width="5cm" svg:height="3cm"/>'
            '<draw:ellipse svg:x="20cm" svg:y="10cm" svg:width="5cm" svg:height="4cm"/>'
            '</draw:page>')
    content = (f'<?xml version="1.0" encoding="UTF-8"?>\n<office:document-content {ODF_NS} office:version="1.2">'
               '<office:body><office:presentation>' + ''.join(pages) +
               '</office:presentation></office:body></office:document-content>\n')
    odf_package(path, 'application/vnd.oasis.opendocument.presentation', content)


def write_docx(path, blocks):
    """blocks: list of (style, text) where style is 'Title', 'Heading1', 'Heading2', 'bullet' or ''."""
    paragraphs = []
    for style, text in blocks:
        props = ''
        if style == 'bullet':
            props = '<w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr>'
        elif style:
            props = f'<w:pPr><w:pStyle w:val="{style}"/></w:pPr>'
        paragraphs.append(f'<w:p>{props}<w:r><w:t xml:space="preserve">{xml_escape(text)}</w:t></w:r></w:p>')
    document = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
                '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>' +
                ''.join(paragraphs) +
                '<w:sectPr><w:pgSz w:w="11906" w:h="16838"/></w:sectPr></w:body></w:document>\n')
    content_types = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
                     '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                     '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
                     '<Default Extension="xml" ContentType="application/xml"/>'
                     '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
                     '</Types>\n')
    rels = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>'
            '</Relationships>\n')
    write_zip(path, [('[Content_Types].xml', content_types.encode(), False),
                     ('_rels/.rels', rels.encode(), False),
                     ('word/document.xml', document.encode('utf-8'), False)])


def write_epub(path, title, author, chapters):
    """EPUB 3 with one XHTML file per chapter. chapters: list of (heading, paragraphs, list items)."""
    xhtml_head = ('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n'
                  '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="en">\n')
    files, manifest, spine, toc = [], [], [], []
    for number, (heading, paragraphs, items) in enumerate(chapters, start=1):
        name = f'chapter{number}.xhtml'
        body = [f'<h1>{xml_escape(heading)}</h1>'] + [f'<p>{xml_escape(p)}</p>' for p in paragraphs]
        if items:
            body += ['<ul>'] + [f'<li>{xml_escape(i)}</li>' for i in items] + ['</ul>']
        page = (xhtml_head + f'<head><title>{xml_escape(heading)}</title>'
                '<link rel="stylesheet" type="text/css" href="style.css"/></head>\n<body>\n' +
                '\n'.join(body) + '\n</body>\n</html>\n')
        files.append((f'OEBPS/{name}', page.encode('utf-8'), False))
        manifest.append(f'<item id="ch{number}" href="{name}" media-type="application/xhtml+xml"/>')
        spine.append(f'<itemref idref="ch{number}"/>')
        toc.append(f'<li><a href="{name}">{xml_escape(heading)}</a></li>')

    nav = (xhtml_head + '<head><title>Contents</title></head>\n<body>\n<nav epub:type="toc" id="toc">\n'
           '<h2>Contents</h2>\n<ol>\n' + '\n'.join(toc) + '\n</ol>\n</nav>\n</body>\n</html>\n')
    opf = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">\n'
           '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
           '<dc:identifier id="bookid">urn:uuid:5e1f0c2a-7b3d-4c58-9a41-e2a0b6d9c701</dc:identifier>\n'
           f'<dc:title>{xml_escape(title)}</dc:title>\n<dc:creator>{xml_escape(author)}</dc:creator>\n'
           '<dc:language>en</dc:language>\n'
           '<dc:rights>Written for the Explorer2 test suite; covered by the repository licence.</dc:rights>\n'
           '<meta property="dcterms:modified">2026-11-12T00:00:00Z</meta>\n</metadata>\n<manifest>\n'
           '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>\n'
           '<item id="css" href="style.css" media-type="text/css"/>\n' + '\n'.join(manifest) +
           '\n</manifest>\n<spine>\n' + '\n'.join(spine) + '\n</spine>\n</package>\n')
    container = ('<?xml version="1.0" encoding="utf-8"?>\n'
                 '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
                 '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>\n'
                 '</container>\n')
    css = 'body { font-family: serif; line-height: 1.5; margin: 1.5em; }\nh1 { font-size: 1.4em; }\n'
    write_zip(path, [('mimetype', b'application/epub+zip', True),
                     ('META-INF/container.xml', container.encode(), False),
                     ('OEBPS/content.opf', opf.encode('utf-8'), False),
                     ('OEBPS/nav.xhtml', nav.encode('utf-8'), False),
                     ('OEBPS/style.css', css.encode(), False)] + files)


def write_dxf(path):
    """A mounting bracket as plain DXF group code / value pairs (ENTITIES section only)."""
    pairs = [(999, 'Explorer2 sample drawing, generated by tools/make_samples.py'),
             (0, 'SECTION'), (2, 'ENTITIES')]

    def line(x1, y1, x2, y2, layer='OUTLINE'):
        pairs.extend([(0, 'LINE'), (8, layer), (10, x1), (20, y1), (30, 0.0), (11, x2), (21, y2), (31, 0.0)])

    def circle(x, y, r, layer='HOLES'):
        pairs.extend([(0, 'CIRCLE'), (8, layer), (10, x), (20, y), (30, 0.0), (40, r)])

    def arc(x, y, r, start, end, layer='OUTLINE'):
        pairs.extend([(0, 'ARC'), (8, layer), (10, x), (20, y), (30, 0.0), (40, r), (50, start), (51, end)])

    # L-shaped plate, 120 x 80 mm, with a rounded outer corner of radius 10
    line(0, 0, 110, 0)
    arc(110, 10, 10, 270, 360)
    line(120, 10, 120, 30)
    line(120, 30, 40, 30)
    line(40, 30, 40, 80)
    line(40, 80, 0, 80)
    line(0, 80, 0, 0)
    for x, y in ((15, 15), (60, 15), (105, 15), (20, 45), (20, 68)):
        circle(x, y, 3.25)
    circle(20, 15, 9.0, 'CENTRE')
    for i in range(6):   # hatch lines on the short leg
        line(4, 34 + i * 7, 36, 38 + i * 7, 'HATCH')
    pairs.extend([(0, 'TEXT'), (8, 'NOTES'), (10, 48.0), (20, 44.0), (30, 0.0), (40, 5.0), (1, 'BRACKET 120 x 80')])
    pairs.extend([(0, 'ENDSEC'), (0, 'EOF')])

    out = []
    for code, value in pairs:
        out.append('%3d' % code)
        out.append(('%.4f' % value) if isinstance(value, float) else str(value))
    path.write_bytes(('\n'.join(out) + '\n').encode('ascii'))


def write_svg(path):
    """A small landscape drawn with every basic SVG shape plus cubic paths."""
    parts = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<svg xmlns="http://www.w3.org/2000/svg" width="640" height="420" viewBox="0 0 640 420">',
             '  <!-- Explorer2 sample illustration, generated by tools/make_samples.py -->',
             '  <rect x="0" y="0" width="640" height="420" fill="#dbeafe"/>',
             '  <circle cx="520" cy="90" r="46" fill="#fde047" stroke="#f59e0b" stroke-width="4"/>']
    for i in range(12):   # sun rays
        a = 2.0 * math.pi * i / 12
        parts.append('  <line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#f59e0b" stroke-width="3"/>' % (
            520 + 58 * math.cos(a), 90 + 58 * math.sin(a), 520 + 78 * math.cos(a), 90 + 78 * math.sin(a)))
    parts += [
        '  <polygon points="0,300 110,170 200,250 300,130 420,270 520,200 640,300 640,420 0,420" fill="#64748b"/>',
        '  <polygon points="300,130 268,172 300,160 330,174" fill="#f8fafc"/>',
        '  <path d="M0 330 C 90 290, 170 350, 260 320 C 350 290, 430 350, 520 322 C 570 306, 610 318, 640 310 L 640 420 L 0 420 Z" fill="#16a34a"/>',
        '  <path d="M0 372 C 120 350, 220 396, 340 372 C 450 350, 540 392, 640 368 L 640 420 L 0 420 Z" fill="#15803d"/>',
        '  <ellipse cx="150" cy="80" rx="62" ry="22" fill="#ffffff"/>',
        '  <ellipse cx="196" cy="66" rx="44" ry="20" fill="#ffffff"/>',
        '  <ellipse cx="360" cy="56" rx="50" ry="16" fill="#ffffff"/>',
        '  <g fill="#b45309" stroke="#78350f" stroke-width="2">',
        '    <rect x="96" y="300" width="74" height="54"/>',
        '    <polygon points="88,300 133,262 178,300" fill="#dc2626"/>',
        '    <rect x="124" y="322" width="18" height="32" fill="#78350f"/>',
        '  </g>',
        '  <polyline points="0,404 80,396 160,406 240,394 320,404 400,392 480,404 560,394 640,402" fill="none" stroke="#14532d" stroke-width="3"/>']
    for x in (430, 470, 505):   # three trees
        parts.append('  <rect x="%d" y="318" width="8" height="30" fill="#78350f"/>' % (x - 4))
        parts.append('  <polygon points="%d,270 %d,326 %d,326" fill="#166534"/>' % (x, x - 22, x + 22))
    parts.append('</svg>')
    path.write_bytes(('\n'.join(parts) + '\n').encode('utf-8'))


def write_bdf(path):
    """An 8x8 bitmap font with four letters drawn for this sample."""
    glyphs = {'A': ['3C', '42', '42', '7E', '42', '42', '42', '00'],
              'B': ['7C', '42', '42', '7C', '42', '42', '7C', '00'],
              'C': ['3C', '42', '40', '40', '40', '42', '3C', '00'],
              'D': ['78', '44', '42', '42', '42', '44', '78', '00']}
    lines = ['STARTFONT 2.1', 'FONT -Explorer2-Sample-Medium-R-Normal--8-80-75-75-C-80-ISO10646-1',
             'SIZE 8 75 75', 'FONTBOUNDINGBOX 8 8 0 0', 'CHARS %d' % len(glyphs)]
    for letter, rows in glyphs.items():
        lines += ['STARTCHAR ' + letter, 'ENCODING %d' % ord(letter), 'BBX 8 8 0 0', 'BITMAP'] + rows + ['ENDCHAR']
    lines.append('ENDFONT')
    path.write_bytes(('\n'.join(lines) + '\n').encode('ascii'))


def write_srec(path):
    """Motorola S-records: a header, two 16-bit-address data records and a start-address record."""
    def record(kind, address, data):
        body = bytes([len(data) + 3, (address >> 8) & 0xFF, address & 0xFF]) + data
        checksum = (~sum(body)) & 0xFF
        return 'S%d%s%02X' % (kind, body.hex().upper(), checksum)

    payload = bytes((i * 37 + 11) & 0xFF for i in range(56))
    lines = [record(0, 0, b'E2SAMPLE'), record(1, 0x0000, payload[:28]), record(1, 0x001C, payload[28:]), record(9, 0x0000, b'')]
    path.write_bytes(('\n'.join(lines) + '\n').encode('ascii'))


# ---------------------------------------------------------------------------------------------
# Sample set
# ---------------------------------------------------------------------------------------------

BOOK_CHAPTERS = [
    ('What a file format is', [
        'A file is a row of numbered bytes and nothing more. A format is the agreement that says what those '
        'bytes mean: where the picture begins, how long a name may be, which number comes first.',
        'Programs that write files and programs that read them never meet. The format is the only thing they '
        'share, so it has to say everything that matters and leave nothing to guesswork.',
        'Most formats were designed in a hurry for one program and then outlived it. That is why so many of '
        'them carry fields nobody uses any more, and why reading one well takes patience.'],
     ['Text formats can be opened in any editor.', 'Binary formats are smaller and faster to read.',
      'Container formats keep several files inside one.']),
    ('Reading a header', [
        'Nearly every binary format starts with a few bytes that identify it. A reader checks them first and '
        'stops at once when they do not match, because everything after them would be read wrongly.',
        'After the signature come the sizes: how wide, how many, how far to the next part. A careful reader '
        'compares each of them with the length of the file before trusting it.',
        'A header that promises more data than the file holds is the most common fault in a damaged file. '
        'Reporting it plainly is better than drawing half a picture and saying nothing.'],
     ['Check the signature.', 'Check every size against the file length.', 'Stop at the first thing that cannot be true.']),
    ('Why a preview helps', [
        'Opening a file in the program that made it answers every question about it, slowly. A preview answers '
        'the first question, which is usually the only one: is this the file I was looking for?',
        'A good preview is honest about what it shows. If it has read only the header, it says so, and it '
        'does not pretend that a thumbnail is the whole drawing.',
        'The samples that come with Explorer2 exist for that reason. Each one is small enough to read in a '
        'moment and still shaped like the real thing.'],
     []),
]

DECK_SLIDES = [
    ('Explorer2 sample presentation', ['A deck written for the test suite', 'Three slides, text and two shapes on each']),
    ('What the reader looks for', ['Every page of the deck', 'The paragraphs on each page, in order', 'Nothing else: styles are ignored']),
    ('Why it is small', ['A sample should be quick to read', 'It should still be a valid file', 'This one is both']),
]

DOCUMENT_BLOCKS = [
    ('Title', 'Explorer2 sample document'),
    ('', 'This document was written for the Explorer2 test suite. It holds a title, two headings, '
         'ordinary paragraphs and a short list, which is everything the document reader looks at.'),
    ('Heading1', 'Purpose'),
    ('', 'A reader for word-processing files has to find the paragraphs, tell headings from body text and '
         'notice which lines belong to a list.'),
    ('Heading2', 'What is checked'),
    ('bullet', 'The title is reported as a level-one heading.'),
    ('bullet', 'List items are reported as bullets.'),
    ('bullet', 'Characters and words are counted.'),
    ('Heading1', 'Notes'),
    ('', 'Special characters survive the round trip: < > & and the Polish letters ą ę ł ó ż.'),
]

SHEET_ROWS = [
    ['Sample', 'Length mm', 'Width mm', 'Mass g', 'Passed'],
    ['A-01', 120.5, 80.0, 412, 'yes'],
    ['A-02', 120.4, 80.1, 415, 'yes'],
    ['A-03', 121.9, 79.2, 398, 'no'],
    ['B-01', 60.0, 40.0, 104, 'yes'],
    ['B-02', 60.2, 39.9, 103, 'yes'],
    ['B-03', 59.1, 41.3, 111, 'no'],
]

PARCEL_FIELDS = [('PARCEL_ID', 'N', 8, 0), ('AREA_M2', 'N', 12, 2), ('LAND_USE', 'C', 14, 0), ('SURVEYED', 'C', 10, 0)]


def parcel_rows():
    uses = ['meadow', 'orchard', 'woodland', 'arable', 'pasture', 'garden']
    rows = []
    for i in range(12):
        area = 850.0 + ((i * 7919) % 4600) + (i % 4) * 0.25
        rows.append(('%d' % (1001 + i * 3), '%.2f' % area, uses[i % len(uses)], '2026-%02d-%02d' % (1 + i % 12, 3 + i * 2)))
    return rows


def generate(out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []

    def emit(file_name, writer, *args):
        target = out_dir / file_name
        writer(target, *args)
        written.append((file_name, target.stat().st_size))

    # 3D models
    ico_v, ico_t = icosahedron(1.0)
    emit('icosahedron_model.3ds', write_3ds, 'Icosahedron', ico_v, ico_t)

    ball_v, ball_t = uv_sphere(8, 10, 1.0)
    flat_v, _ = uv_sphere(8, 10, 1.0, squash=0.6)
    emit('quake2_sphere.md2', write_md2, [('round', ball_v), ('squashed', flat_v)], ball_t)

    torus_v, torus_t = torus(16, 10)
    emit('torus_ascii.stl', write_stl_ascii, 'explorer2_torus', torus_v, torus_t)
    emit('wavefront_torus.obj', write_obj_with_normals, 'torus', torus_v, torus_t)

    small_v, small_t = torus(8, 5, 1.5, 0.5)
    emit('torus_mesh.vtk', write_vtk_polydata, 'Explorer2 sample torus', small_v, small_t)

    octa_v = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    octa_f = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
    emit('octahedron_mesh.ply', write_ply_ascii, [tuple(float(c) for c in v) for v in octa_v], octa_f)

    pyramid_v = [(-1.0, -1.0, 0.0), (1.0, -1.0, 0.0), (1.0, 1.0, 0.0), (-1.0, 1.0, 0.0), (0.0, 0.0, 1.5)]
    pyramid_f = [(0, 3, 2, 1), (0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)]   # a square base and four sides
    emit('geomview_pyramid.off', write_off, pyramid_v, pyramid_f)

    box_v = [(x, y, z) for z in (0.0, 30.0) for y in (0.0, 20.0) for x in (0.0, 10.0)]
    box_t = [(0, 2, 3), (0, 3, 1), (4, 5, 7), (4, 7, 6), (0, 1, 5), (0, 5, 4),
             (2, 6, 7), (2, 7, 3), (0, 4, 6), (0, 6, 2), (1, 3, 7), (1, 7, 5)]
    emit('box_part.3mf', write_3mf, box_v, box_t)

    # Drawings and images
    emit('bracket_cad.dxf', write_dxf)
    emit('vector_illustration.svg', write_svg)
    emit('sample.bdf', write_bdf)

    # Documents and data
    emit('sample_book.epub', write_epub, 'A Short Guide to File Formats', 'Explorer2 test suite', BOOK_CHAPTERS)
    emit('shapes_presentation.odp', write_odp, DECK_SLIDES)
    emit('word_document.docx', write_docx, DOCUMENT_BLOCKS)
    emit('calc_spreadsheet.ods', write_ods, [('Measurements', SHEET_ROWS),
                                             ('Summary', [['Group', 'Count'], ['A', 3], ['B', 3]])])
    emit('parcels_gis.dbf', write_dbf, PARCEL_FIELDS, parcel_rows())
    emit('bootloader_image.srec', write_srec)

    return written


def main():
    default_dir = Path(__file__).resolve().parent.parent / 'testdata'
    out_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else default_dir
    for file_name, size in generate(out_dir):
        print(f'{size:>9}  {file_name}')


if __name__ == '__main__':
    main()
