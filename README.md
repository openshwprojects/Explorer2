# Explorer2

A native file manager for Windows, written in C++20 on plain Win32, Direct2D and DirectWrite.

Explorer2 has no third-party library dependencies. The file views, the previewers and every
file-format reader live in this repository and build into a single `Explorer2.exe`.
The interface is available in Polish and English.

## Features

**Browsing**

- Details, list and five icon-size views, with sorting and grouping
- Disk-usage views: a squarified treemap and a sunburst chart
- Dual-pane split view, navigation tree with pinned Quick Access folders, breadcrumb bar
- Search with filters for date, size, file type and file content
- In-window image and media viewer with a filmstrip

**Preview pane**

- Text and source code with syntax highlighting, and a hex view
- Images, 3D models, PCB and schematic files, spreadsheets, documents and PDF
- Archives open like folders (ZIP, TAR, RAR and more) and can be extracted in the background
- Previews are read on a background thread: a large PDF, archive or document, a file inside an
  archive or on an FTP server never holds the window up while it loads

**Tools**

- Transfer queue for copy, move and delete: pause and resume, resuming interrupted copies, speed
  limit, conflict policies and checksum verification
- Own delete engine: handles long paths, read-only items and names Explorer cannot remove, never
  follows junctions or symlinks, and lists every item it could not delete so it can be retried.
  A Recycle Bin delete never turns into a permanent one: an item the bin cannot take is reported
  and left in place
- FTP and FTPS client with bookmarks and dual-pane transfers
- Backups: scheduled incremental folder sync, or cloud targets through rclone
- Hardware inventory and live telemetry: CPU, GPU, RAM, disks and SMART, network, thermal,
  USB and COM ports
- Long-term system monitoring, recorded by an optional background process
- Installed programs list, LAN scanner, Git panel and an operation log

## File formats

Common image formats are decoded through the Windows Imaging Component. Everything else is read by
code in [`src/formats/`](src/formats), one header per format family. A sample of what is covered:

| Area | Examples |
| --- | --- |
| Images | TGA, PSD, XCF, DDS, HDR, Netpbm, PCX, QOI, SVG, KTX, FITS |
| 3D models | OBJ, STL, PLY, 3DS, MD2, MD3, GLB, COLLADA, Blender, 3MF, VTK |
| Electronics and CAD | KiCad, Eagle, Gerber, Excellon, DXF, Specctra DSN, Intel HEX, GDSII |
| Documents and data | CSV, XLSX, ODS, DOCX, ODT, RTF, PPTX, EPUB, FB2, PDF, SQLite, DBF |
| Archives | ZIP, TAR, gzip, RAR4, RAR5, 7z |

Depth varies. The common formats get a full viewer, while many of the rarer ones are inspectors
that show a file's header, structure and metadata rather than rendering it.

## Building

Requirements: Windows 10 or 11 (x64) and Visual Studio 2022, or its Build Tools, with the
"Desktop development with C++" workload. The workload includes the Windows SDK.

```bat
build.bat
```

The script locates Visual Studio through `vswhere`, compiles everything in `src/` and writes
`Explorer2.exe` to the repository root. Intermediate files go to `build/`.

## Running

```bat
Explorer2.exe [folder]
```

| Option | Purpose |
| --- | --- |
| `--self-test <stage> [report]` | Run the built-in tests and exit (see [Self-tests](#self-tests)) |
| `--dump-hw <file>` | Write a hardware, SMART and thermal diagnostics dump to a file and exit |
| `--inspect-files <list> <result>` | Run the file readers on every file named in a list and record what each did (used by `tools/fuzz_samples.py`) |
| `--monitor-daemon` | Run the background monitoring recorder. The app starts it when monitoring is enabled |

Settings, FTP bookmarks, backup jobs, monitoring data and logs are stored under
`%LOCALAPPDATA%\Explorer2`.

Every session writes a log to `%LOCALAPPDATA%\Explorer2\Logs` (the Log tab shows the current
one; logs older than 30 days and all but the newest 40 are removed). Should the program ever
stop with an error, the last lines of that session's log say where: the exception code and the
call stack, each entry as `module+offset`. A file reader that gives up on a damaged file leaves
a `[Reader]` line there as well.

### Optional tools

Explorer2 uses these when they are installed, and works without them:

- **git**: the Git panel
- **rclone**: cloud backup targets
- **7-Zip or WinRAR** (default install location): extracting compressed entries from RAR and 7z archives

### Things to know

- Delete moves items to the Recycle Bin and Shift+Delete removes them for good. Neither asks for
  confirmation unless you switch that on in the Delete button's menu.
- Ctrl+Z after a Recycle Bin delete works from hidden copies (hard links, so they take no extra
  space) that Explorer2 keeps in `.explorer2_undo` at the root of the drive while it runs. They
  are removed when it exits; what a crashed session leaves behind is removed at the next start.
- Extracting an archive never replaces a file that is already there. From ZIP and TAR archives
  the new file is saved next to it as `name (2)`; where 7-Zip, UnRAR or tar does the unpacking,
  that file is skipped. Entries that point outside the destination folder are never written.
- FTP bookmarks are saved as URLs in `ftp_bookmarks.ini`. A password that is part of the URL is
  stored there in plain text.
- If FileZilla is installed, the sites in its Site Manager appear in Explorer2's FTP list
  automatically.
- Monitoring is off by default. When enabled it records one CSV row per interval and can start
  with Windows, either from the per-user Run key or, for SMART access, as an elevated scheduled
  task named `Explorer2Monitor`.

## Self-tests

The test suite is built into the executable:

```bat
Explorer2.exe --self-test all build\selftest_report.txt
```

Explorer2 is a GUI program, so nothing is printed to the console. Read the report file and check
the exit code, which is 0 when every test passed. If a test takes the program down, the report says
that the run did not finish and `<report>.crash.txt` names the place. From PowerShell:

```powershell
$p = Start-Process .\Explorer2.exe -ArgumentList '--self-test','all','build\selftest_report.txt' -Wait -PassThru
$p.ExitCode
```

`<stage>` is `all`, a stage number (`1` to `10`, `12`, `13` or `14`), or `100x`, which repeats
stages 1 to 10 a hundred times.

The tests read sample files from `testdata/`, which must sit next to the executable or in the
current directory. Some of them exercise the real machine: they ping 127.0.0.1, enumerate USB
controllers, render a PDF through Windows, start a second copy of the executable to check that
it ends cleanly, move one small file to the Recycle Bin and take it out again, and briefly use
the hidden undo folder on the drive that holds `%TEMP%`. Everything else they write goes to
`%TEMP%\Explorer2_SelfTest` and is removed.

`python tools/fuzz_samples.py` is a second, slower check: it makes thousands of damaged copies
of the samples and has every file reader read them. A reader has to cope with a damaged file or
turn it down; a crash, an escaped exception or a reader that never returns is reported together
with the file that caused it.

Two tests need large private archives. They are skipped unless you point them at your own files:

| Variable | Value |
| --- | --- |
| `EXPLORER2_TEST_RAR5` | Path to a large RAR5 archive |
| `EXPLORER2_TEST_RAR5_FILES` | Optional: the number of files that archive must list |
| `EXPLORER2_TEST_IMAGE_RAR` | Path to an archive whose first image is a PNG |

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | Application sources |
| `src/formats/` | File-format readers, one header per format family |
| `res/` | Application icon |
| `testdata/` | Sample files for the self-tests, with their sources and licences |
| `tools/` | Scripts: `make_samples.py` regenerates the self-made samples, `fuzz_samples.py` is the damaged-file test |
| `build.bat` | Build script |

## License

Explorer2 is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version. It comes with no warranty. The full text
is in [LICENSE](LICENSE).

Copyright (C) 2026 the Explorer2 authors.

Some sample files in `testdata/` come from other projects and keep their own licences. They
are listed, with authors and licence texts, in [testdata/README.md](testdata/README.md). One of
them, `testdata/quake2_ratamahatta.md2`, is not covered by the GPL: its author does not allow it
to be distributed as part of a commercial product, so remove it before any commercial
redistribution.
