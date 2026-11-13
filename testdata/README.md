# Test samples

The self-tests read the files in this folder (`Explorer2.exe --self-test all`). They are also
handy for trying out the viewers.

Most of the files were made for this project and are covered by the repository licence
(see [Made for this project](#made-for-this-project)). Eighteen come from other projects. They
are listed below with their authors and licences, and are byte-for-byte copies of the originals;
only some file names differ.

One of them, `quake2_ratamahatta.md2`, must not be distributed commercially. Read
[One file with a non-commercial restriction](#one-file-with-a-non-commercial-restriction) before
you sell copies of Explorer2 or put it into a commercial product.

## Files from other projects

| File | What it is | Taken from | Credit | Licence |
| --- | --- | --- | --- | --- |
| `blender_box.blend` | Blender 2.93 default scene | [assimp](https://github.com/assimp/assimp), `test/models/BLEND/box.blend` | assimp team | BSD 3-Clause, [text](LICENSES/assimp-LICENSE.txt) |
| `chinook_sample.sqlite` | Chinook sample database | [chinook-database](https://github.com/lerocha/chinook-database), `ChinookDatabase/DataSources/Chinook_Sqlite.sqlite` | © 2008-2024 Luis Rocha | MIT, [text](LICENSES/Chinook-LICENSE.md) |
| `collada_elf_girl.dae` | "Elf Girl" character model | [three.js](https://github.com/mrdoob/three.js) examples, `examples/models/collada/elf/elf.dae`; original on [Sketchfab](https://sketchfab.com/3d-models/elf-girl-52f2e84961b94760b7805c178890d644) | "Elf Girl" by [halloween](https://sketchfab.com/yellow09) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| `dxt1_gradient.dds` | DXT1 texture | [Pillow](https://github.com/python-pillow/Pillow), `Tests/images/dxt1-rgb-4bbp-noalpha_MipMaps-1.dds` | Metallicow | CC0 / public domain, as stated by the author in [Pillow pull request 1644](https://github.com/python-pillow/Pillow/pull/1644#issuecomment-169101551) |
| `eagle_board.brd`, `eagle_schematic.sch` | Adafruit Feather RP2040, Eagle board and schematic | [Adafruit-Feather-RP2040-PCB](https://github.com/adafruit/Adafruit-Feather-RP2040-PCB), `Adafruit Feather RP2040.brd` and `.sch` | Designed by Adafruit Industries | Creative Commons Attribution, Share-Alike; see the [upstream README](LICENSES/Adafruit-Feather-RP2040-PCB-README.md), which has to accompany the files |
| `hopper.xbm`, `hopper.xpm`, `hopper_color.ppm`, `hopper_lossless.qoi` | Pillow's "hopper" test image | [Pillow](https://github.com/python-pillow/Pillow), `Tests/images/hopper.xbm`, `.xpm`, `.ppm`, `.qoi` | Pillow contributors; the photograph of Grace Hopper is a U.S. Navy picture in the public domain | MIT-CMU, [text](LICENSES/Pillow-LICENSE.txt) |
| `truevision_abc.tga` | Targa image with an image-ID field | [Pillow](https://github.com/python-pillow/Pillow), `Tests/images/tga_id_field.tga` | Pillow contributors | MIT-CMU, [text](LICENSES/Pillow-LICENSE.txt) |
| `interf_u.kicad_pcb`, `interf_u.kicad_sch` | KiCad demo project "interf_u" | [KiCad](https://gitlab.com/kicad/code/kicad), `demos/interf_u/` | KiCad Developers | [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) |
| `khronos_box.glb` | glTF sample model "Box" | [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets), `Models/Box/glTF-Binary/Box.glb` | © 2017 Cesium | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| `minimal_document.pdf` | One-page PDF | [py-pdf/sample-files](https://github.com/py-pdf/sample-files), `001-trivial/minimal-document.pdf` | py-pdf sample-files contributors | [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) |
| `quake2_ratamahatta.md2` | "Ratamahatta" player model for Quake II | [three.js](https://github.com/mrdoob/three.js) examples, `examples/models/md2/ratamahatta/ratamahatta.md2` | Brian "EvilBastard" Collins, 1998 | **Non-commercial only, not covered by the GPL**; terms in the model's [readme](LICENSES/Ratamahatta-readme.txt) |
| `sample_2layers.psd` | Photoshop file with two layers | [psd-tools](https://github.com/psd-tools/psd-tools), `tests/psd_files/2layers.psd` | psd-tools contributors, © 2019 Kota Yamaguchi | MIT, [text](LICENSES/psd-tools-LICENSE.txt) |
| `sample_deck.pptx` | PowerPoint file with one slide | [python-pptx](https://github.com/scanny/python-pptx), `tests/test_files/test.pptx` | © 2013 Steve Canny | MIT, [text](LICENSES/python-pptx-LICENSE.txt) |

The licence texts that have to travel with these files are in [`LICENSES/`](LICENSES). These
licences apply to the listed files only, not to Explorer2.

## One file with a non-commercial restriction

`quake2_ratamahatta.md2` is the "Ratamahatta" player model for Quake II, made by Brian
"EvilBastard" Collins in 1998. Its [readme](LICENSES/Ratamahatta-readme.txt) allows anyone to
edit the model as long as the author is credited, and says that the model is not to be
distributed as part of any commercial product. The readme also names an earlier model,
"Forgotten One", as its base. QUAKE and QUAKE II are registered trademarks of id Software, Inc.

That restriction is stricter than the GPL, so this one file is **not** covered by the
repository licence. It is included because Explorer2 is given away free of charge.

If you sell copies of Explorer2, or ship it inside a commercial product, delete
`quake2_ratamahatta.md2` and its readme first. Nothing depends on the file: the self-test that
reads it is skipped when it is absent, and `quake2_sphere.md2` exercises the same reader.

## Made for this project

Every other file in this folder was written or generated for Explorer2 and contains nothing
taken from elsewhere. Some of them cite real books and papers, or use well-known reference
values such as a few rows of the Iris data set; the files themselves are original.

[`tools/make_samples.py`](../tools/make_samples.py) regenerates the following ones from the
formulas and text it contains:

`icosahedron_model.3ds`, `quake2_sphere.md2`, `torus_ascii.stl`, `wavefront_torus.obj`,
`torus_mesh.vtk`, `octahedron_mesh.ply`, `geomview_pyramid.off`, `box_part.3mf`,
`bracket_cad.dxf`, `vector_illustration.svg`, `sample.bdf`, `sample_book.epub`,
`shapes_presentation.odp`, `word_document.docx`, `calc_spreadsheet.ods`, `parcels_gis.dbf`,
`bootloader_image.srec`.

## Adding a sample

Add a file here only if you made it yourself or its licence clearly allows redistribution, and
record where it came from in the table above.
