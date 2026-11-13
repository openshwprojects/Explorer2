#!/usr/bin/env python3
"""Robustness test for Explorer2's file readers.

Makes damaged copies of every sample in testdata/ (cut short, bytes changed, size fields set to
extreme values, numbers in text files replaced, data appended) and lets Explorer2 read each of them
through `Explorer2.exe --inspect-files`. A reader must either read a damaged file or turn it down;
a crash, an escaped C++ exception or a reader that never returns is a fault, and is reported with
the file that caused it.

Usage:  python tools/fuzz_samples.py [--exe PATH] [--seed N] [--workers N] [--only PATTERN]
                                     [--pdb PATH] [--findings DIR]

The damaged files are written to a folder under %TEMP% and removed again; the files that caused a
fault are copied to --findings (default: build/fuzz_findings). The run is deterministic for a seed.
Exit code: 0 when no fault was found, 1 otherwise.
"""

import argparse
import concurrent.futures
import ctypes
import fnmatch
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LARGE_FILE = 256 * 1024          # samples above this size get fewer variants, to keep the run short
BOMBS = [0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x0000FFFF, 0x00000000, 0x10000000, 0xFFFFFFFE, 0x00010000]
TEXT_BOMBS = [b'99999999999', b'-1', b'0', b'4294967295', b'1e309', b'-99999999', b'2147483648', b'']


def variants(data, rng, large):
    """Yields (label, bytes) for the damaged copies of one sample."""
    n = len(data)
    cuts = sorted({c for c in (0, 1, 2, 3, 4, 8, 16, 32, 64, 128, n // 4, n // 2, 3 * n // 4, n - 1) if 0 <= c < n})
    if large:
        cuts = cuts[::2]
    for c in cuts:
        yield 'cut%d' % c, data[:c]
    if n == 0:
        return

    count = 10 if large else 24
    for i in range(count):                       # a few bytes changed, half of the time near the start
        buf = bytearray(data)
        limit = min(n, 512) if i % 2 == 0 else n
        for _ in range(rng.randint(1, 4)):
            buf[rng.randrange(limit)] = rng.randrange(256)
        yield 'flip%02d' % i, bytes(buf)

    for i in range(count):                       # a 32-bit field set to an extreme value
        buf = bytearray(data)
        limit = min(n, 256) if i % 2 == 0 else n
        if limit >= 4:
            at = rng.randrange(limit - 3)
            value = BOMBS[i % len(BOMBS)]
            buf[at:at + 4] = value.to_bytes(4, 'little' if i % 3 else 'big')
        yield 'bomb%02d' % i, bytes(buf)

    # Every field of the header in turn: a 32-bit value at each even offset of the first 128 bytes (64 for
    # a large sample), in both byte orders, and a 16-bit 0xFFFF. Counts and sizes live in these fields.
    header = min(n, 64 if large else 128)
    for at in range(0, max(0, header - 3), 2):
        for tag, value, order in (('ff', 0xFFFFFFFF, 'little'), ('7f', 0x7FFFFFFF, 'little'), ('40', 0x40000000, 'little'),
                                  ('7fb', 0x7FFFFFFF, 'big'), ('40b', 0x40000000, 'big')):
            buf = bytearray(data)
            buf[at:at + 4] = value.to_bytes(4, order)
            yield 'field%03d%s' % (at, tag), bytes(buf)
    for at in range(0, max(0, min(n, 64) - 1), 2):
        buf = bytearray(data)
        buf[at:at + 2] = b'\xff\xff'
        yield 'word%03d' % at, bytes(buf)

    numbers = [m.span() for m in re.finditer(rb'-?\d+(?:\.\d+)?', data[:200000])]
    if len(numbers) >= 3:                        # a number in a text format replaced by an extreme one
        for i in range(4 if large else 8):
            start, end = numbers[rng.randrange(len(numbers))]
            yield 'num%02d' % i, data[:start] + TEXT_BOMBS[i % len(TEXT_BOMBS)] + data[end:]
        sample_text = data[:4096]
        printable = sum(1 for b in sample_text if b in (9, 10, 13) or 32 <= b < 127)
        if printable >= 0.9 * len(sample_text):  # a text format: each of its first numbers in turn
            for i, (start, end) in enumerate(numbers[:20 if large else 60]):
                for tag, value in (('big', b'99999999999'), ('neg', b'-1'), ('zero', b'0')):
                    yield 'each%02d%s' % (i, tag), data[:start] + value + data[end:]

    yield 'tail', data + bytes(rng.randrange(256) for _ in range(4096))
    middle = n // 2
    yield 'bulge', data[:middle] + b'A' * 65536 + data[middle:]
    yield 'twice', data + data


def parse_result(path):
    """Reads an --inspect-files result. Returns (done, lines) with the lines split into fields."""
    if not path.exists():
        return False, []
    lines = [line.split('\t') for line in path.read_text(encoding='utf-8', errors='replace').splitlines() if line]
    return bool(lines) and lines[-1][0] == 'D', lines


def selfcheck(exe, work_dir, findings_dir):
    """Proves that the harness notices each kind of fault, using three inputs that fail on purpose."""
    batch = work_dir / '_selfcheck'
    batch.mkdir(parents=True, exist_ok=True)
    files = []
    for kind in ('ok_before', 'crash', 'ok_between', 'throw', 'hang', 'ok_after'):
        target = batch / ('__inspect_selfcheck_%s.bin' % kind)
        target.write_bytes(b'selfcheck')
        files.append(target)
    _, counts, findings = run_files(exe, files, batch, findings_dir / '_selfcheck')
    kinds = sorted(f['kind'] for f in findings)
    passed = kinds == ['crash', 'exception', 'hang'] and counts['accepted'] == 3
    print('self-check of the harness: %s (found %s, %d harmless inputs went through)' % (
        'passed' if passed else 'FAILED', ', '.join(kinds) or 'nothing', counts['accepted']))
    shutil.rmtree(findings_dir / '_selfcheck', ignore_errors=True)
    return passed


def run_batch(exe, sample, work_dir, seed, findings_dir):
    """Fuzzes one sample. Returns (inputs, counts, findings)."""
    data = sample.read_bytes()
    rng = random.Random((seed << 32) ^ zlib.crc32(sample.name.encode()))
    batch_dir = work_dir / sample.name
    batch_dir.mkdir(parents=True, exist_ok=True)
    files = []
    for label, content in variants(data, rng, len(data) > LARGE_FILE):
        target = batch_dir / ('%s__%s%s' % (sample.stem, label, sample.suffix))
        target.write_bytes(content)
        files.append(target)
    return run_files(exe, files, batch_dir, findings_dir)


def run_files(exe, files, batch_dir, findings_dir):
    """Lets the exe read a list of files, restarting it behind every file that stopped it."""

    counts = {'accepted': 0, 'rejected': 0, 'no_reader': 0, 'slow': 0}
    findings = []
    remaining = list(files)
    rounds = 0
    while remaining and rounds < len(files) + 2:
        rounds += 1
        list_file = batch_dir / '_list.txt'
        result_file = batch_dir / '_result.txt'
        list_file.write_text('\n'.join(str(f) for f in remaining) + '\n', encoding='utf-8')
        if result_file.exists():
            result_file.unlink()
        try:
            proc = subprocess.run([str(exe), '--inspect-files', str(list_file), str(result_file)], timeout=600)
            exit_code = proc.returncode
        except subprocess.TimeoutExpired:
            exit_code = -1
        done, lines = parse_result(result_file)

        open_step = None            # the reader that was started and did not report back
        fault = None
        for fields in lines:
            kind = fields[0]
            if kind == 'B':
                open_step = (int(fields[1]), fields[2])
            elif kind == 'E':
                open_step = None
                counts['accepted' if fields[3] == '1' else 'rejected'] += 1
                if int(fields[4]) > 3000:
                    counts['slow'] += 1
                    findings.append({'kind': 'slow', 'file': remaining[int(fields[1])], 'reader': fields[2],
                                     'detail': '%s ms' % fields[4]})
            elif kind == 'T':
                open_step = None
                findings.append({'kind': 'exception', 'file': remaining[int(fields[1])], 'reader': fields[2],
                                 'detail': fields[3] if len(fields) > 3 else ''})
            elif kind == 'N':
                counts['no_reader'] += 1
            elif kind == 'X':
                fault = {'kind': 'crash', 'file': remaining[int(fields[1])], 'reader': fields[2],
                         'detail': '%s in %s+0x%s' % (fields[3], fields[4], fields[5]),
                         'code': fields[3], 'module': fields[4], 'offset': int(fields[5], 16), 'index': int(fields[1])}
            elif kind == 'H':
                fault = {'kind': 'hang', 'file': remaining[int(fields[1])], 'reader': fields[2],
                         'detail': 'no answer after 20 s', 'index': int(fields[1])}
        if fault is None and not done:
            if open_step is not None:
                index, reader = open_step
                fault = {'kind': 'crash', 'file': remaining[index], 'reader': reader, 'index': index,
                         'detail': 'process ended with exit code 0x%08X' % (exit_code & 0xFFFFFFFF),
                         'code': '%08X' % (exit_code & 0xFFFFFFFF), 'module': '?', 'offset': 0}
            else:
                fault = {'kind': 'crash', 'file': remaining[0], 'reader': '(start-up)', 'index': len(remaining) - 1,
                         'detail': 'no result written, exit code 0x%08X' % (exit_code & 0xFFFFFFFF)}
        if fault is None:
            break
        findings.append(fault)
        remaining = remaining[fault['index'] + 1:]      # go on behind the file that stopped the run

    for finding in findings:
        findings_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(finding['file'], findings_dir / finding['file'].name)
        finding['file'] = finding['file'].name
    shutil.rmtree(batch_dir, ignore_errors=True)
    return len(files), counts, findings


class Symbolizer:
    """Turns an offset inside the exe into "function (file:line)" with dbghelp, when a .pdb is at hand."""

    def __init__(self, exe, pdb):
        self.ok = False
        if not pdb or not Path(pdb).exists():
            return
        try:
            self.dbghelp = ctypes.WinDLL('dbghelp')
            self.handle = ctypes.c_void_p(0x5EED)
            self.dbghelp.SymSetOptions(0x00000002 | 0x00000010)      # SYMOPT_UNDNAME | SYMOPT_LOAD_LINES
            self.dbghelp.SymInitializeW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
            if not self.dbghelp.SymInitializeW(self.handle, str(Path(pdb).parent), 0):
                return
            self.dbghelp.SymLoadModuleExW.restype = ctypes.c_uint64
            self.dbghelp.SymLoadModuleExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_wchar_p,
                                                      ctypes.c_uint64, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32]
            self.base = 0x140000000
            size = Path(exe).stat().st_size
            if not self.dbghelp.SymLoadModuleExW(self.handle, None, str(exe), None, self.base, size, None, 0):
                return
            self.ok = True
        except OSError:
            self.ok = False

    def describe(self, offset):
        if not self.ok:
            return ''

        class SymbolInfo(ctypes.Structure):
            _fields_ = [('SizeOfStruct', ctypes.c_uint32), ('TypeIndex', ctypes.c_uint32), ('Reserved', ctypes.c_uint64 * 2),
                        ('Index', ctypes.c_uint32), ('Size', ctypes.c_uint32), ('ModBase', ctypes.c_uint64),
                        ('Flags', ctypes.c_uint32), ('Value', ctypes.c_uint64), ('Address', ctypes.c_uint64),
                        ('Register', ctypes.c_uint32), ('Scope', ctypes.c_uint32), ('Tag', ctypes.c_uint32),
                        ('NameLen', ctypes.c_uint32), ('MaxNameLen', ctypes.c_uint32), ('Name', ctypes.c_wchar * 512)]

        class Line64(ctypes.Structure):
            _fields_ = [('SizeOfStruct', ctypes.c_uint32), ('Key', ctypes.c_void_p), ('LineNumber', ctypes.c_uint32),
                        ('FileName', ctypes.c_wchar_p), ('Address', ctypes.c_uint64)]

        address = ctypes.c_uint64(self.base + offset)
        text = ''
        info = SymbolInfo()
        info.SizeOfStruct = 88          # sizeof(SYMBOL_INFOW) with its one-character name field
        info.MaxNameLen = 511
        displacement = ctypes.c_uint64(0)
        self.dbghelp.SymFromAddrW.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.c_void_p]
        if self.dbghelp.SymFromAddrW(self.handle, address, ctypes.byref(displacement), ctypes.byref(info)):
            text = info.Name
        line = Line64()
        line.SizeOfStruct = ctypes.sizeof(Line64)
        line_displacement = ctypes.c_uint32(0)
        self.dbghelp.SymGetLineFromAddrW64.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
        if self.dbghelp.SymGetLineFromAddrW64(self.handle, address, ctypes.byref(line_displacement), ctypes.byref(line)):
            text += ' (%s:%d)' % (Path(line.FileName).name, line.LineNumber)
        return text


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', default=str(REPO / 'Explorer2.exe'))
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--workers', type=int, default=max(2, min(6, (os.cpu_count() or 4) // 2)))
    parser.add_argument('--only', default='*', help='file name pattern of the samples to use, for example "*.md2"')
    parser.add_argument('--pdb', default='', help='.pdb of the exe, to name the source line of a crash')
    parser.add_argument('--findings', default=str(REPO / 'build' / 'fuzz_findings'))
    args = parser.parse_args()

    exe = Path(args.exe).resolve()
    if not exe.exists():
        print('not found: %s' % exe)
        return 2
    samples = sorted(p for p in (REPO / 'testdata').iterdir()
                     if p.is_file() and p.name != 'README.md' and fnmatch.fnmatch(p.name, args.only))
    findings_dir = Path(args.findings)
    if findings_dir.exists():
        shutil.rmtree(findings_dir)
    work_dir = Path(tempfile.mkdtemp(prefix='Explorer2_Fuzz_'))

    if not selfcheck(exe, work_dir, findings_dir):
        shutil.rmtree(work_dir, ignore_errors=True)
        print('the harness does not notice faults with this exe, so a clean result would mean nothing; stopping')
        return 2

    total_inputs = 0
    totals = {'accepted': 0, 'rejected': 0, 'no_reader': 0, 'slow': 0}
    findings = []
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            jobs = {pool.submit(run_batch, exe, s, work_dir, args.seed, findings_dir): s for s in samples}
            for job in concurrent.futures.as_completed(jobs):
                inputs, counts, found = job.result()
                total_inputs += inputs
                for key in totals:
                    totals[key] += counts[key]
                for f in found:
                    f['sample'] = jobs[job].name
                findings.extend(found)
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)

    symbols = Symbolizer(exe, args.pdb)
    print('samples: %d   damaged files: %d   reader runs: %d accepted, %d turned down   no reader: %d' % (
        len(samples), total_inputs, totals['accepted'], totals['rejected'], totals['no_reader']))
    faults = [f for f in findings if f['kind'] != 'slow']
    print('faults: %d (crash %d, escaped exception %d, hang %d)   slow (> 3 s): %d' % (
        len(faults), sum(f['kind'] == 'crash' for f in findings), sum(f['kind'] == 'exception' for f in findings),
        sum(f['kind'] == 'hang' for f in findings), totals['slow']))

    groups = {}
    for f in findings:
        where = ''
        if f['kind'] == 'crash' and f.get('module', '').lower() == exe.name.lower():
            where = symbols.describe(f['offset'])
        key = (f['kind'], f['reader'], Path(f['sample']).suffix.lower(), where or f['detail'])
        groups.setdefault(key, []).append(f)
    for (kind, reader, suffix, where), items in sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        print('%4d x  %-9s %-8s %-11s %s' % (len(items), kind, reader, suffix, where))
        print('          e.g. %s   [%s]' % (items[0]['file'], items[0]['detail']))
    if findings:
        print('files that caused a fault are in %s' % findings_dir)
    return 1 if faults else 0


if __name__ == '__main__':
    sys.exit(main())
