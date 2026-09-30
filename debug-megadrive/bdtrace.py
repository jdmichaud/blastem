#!/usr/bin/env python3
"""Reader for blastdbg frame traces (BDTRACE files). See TRACE_FORMAT.md.

As a library:
    from bdtrace import Trace
    with Trace("run.trc") as trace:
        for rec in trace:
            print(rec.frame, rec.irq, rec.cycle, rec.ram[0x100:0x104].hex())

From the command line:
    bdtrace.py info run.trc               header and record summary
    bdtrace.py records run.trc            one line per record
    bdtrace.py writes run.trc [FRAME]     the write log, for all records or one frame
    bdtrace.py diff a.trc b.trc           first difference between two traces
"""
import signal
import struct
import sys
from dataclasses import dataclass, field

MAGIC = b"BDTRACE\0"
RECORD_MAGIC = b"REC\0"

REC_VRAM = 0x01
REC_CRAM = 0x02
REC_VSRAM = 0x04
REC_VDPREGS = 0x08
REC_Z80RAM = 0x10

WRITE_KINDS = {1: "ym", 2: "psg", 3: "z80ram", 4: "z80busreq", 5: "z80reset", 6: "z80bank"}
SOURCES = {0: "68k", 1: "z80"}
BUTTONS = ["up", "down", "left", "right", "a", "b", "c", "start", "x", "y", "z", "mode"]


@dataclass
class Header:
    version: int
    header_size: int
    rom_sha256: bytes
    record_flags: int
    first_frame: int
    last_frame: int
    record_count: int
    master_clock: int
    pal: bool
    pad_type: int
    version_reg: int
    complete: bool
    record_header_size: int
    write_entry_size: int
    ram_size: int
    vram_size: int
    cram_size: int
    vsram_size: int
    vdpregs_size: int
    z80ram_size: int
    screenshot_every: int

    @classmethod
    def parse(cls, data):
        if data[:8] != MAGIC:
            raise ValueError("not a BDTRACE file")
        (version, header_size) = struct.unpack_from("<II", data, 8)
        if version != 1:
            raise ValueError(f"unsupported BDTRACE version {version}")
        (record_flags, first, last, count, clock) = struct.unpack_from("<IIIII", data, 48)
        pal, pad_type, version_reg, complete = data[68], data[69], data[70], data[71]
        sizes = struct.unpack_from("<9I", data, 72)
        return cls(version, header_size, bytes(data[16:48]), record_flags, first, last, count, clock,
                   bool(pal), pad_type, version_reg, bool(complete), *sizes)

    def section_sizes(self):
        """(name, size) for each section present in a record, in file order."""
        sections = [("ram", self.ram_size)]
        for flag, name, size in ((REC_VRAM, "vram", self.vram_size), (REC_CRAM, "cram", self.cram_size),
                                 (REC_VSRAM, "vsram", self.vsram_size), (REC_VDPREGS, "vdpregs", self.vdpregs_size),
                                 (REC_Z80RAM, "z80ram", self.z80ram_size)):
            if self.record_flags & flag:
                sections.append((name, size))
        return sections


@dataclass
class Write:
    cycle: int
    pc: int
    address: int
    kind: str
    source: str
    pc_valid: bool
    value: int

    def __str__(self):
        pc = f"{self.pc:06X}" if self.pc_valid else "------"
        return f"{self.cycle:>14} {self.source} pc={pc} {self.kind:<9} addr={self.address:06X} value={self.value:02X}"


@dataclass
class Record:
    frame: int
    irq: bool
    cycle: int
    pad1: int
    pad2: int
    pc: int
    sr: int
    d: tuple
    a: tuple
    other_sp: int
    vdp_frame: int
    ram: bytes
    vram: bytes = None
    cram: bytes = None
    vsram: bytes = None
    vdpregs: bytes = None
    z80ram: bytes = None
    writes: list = field(default_factory=list)

    def ram_byte(self, address):
        """Work RAM byte at a 68K address ($FF0000-$FFFFFF, or $E00000 mirrors)."""
        return self.ram[address & 0xFFFF]

    def ram_word(self, address):
        return struct.unpack_from(">H", self.ram, address & 0xFFFF)[0]

    def ram_long(self, address):
        return struct.unpack_from(">I", self.ram, address & 0xFFFF)[0]


def pad_names(mask):
    return " ".join(name for bit, name in enumerate(BUTTONS) if mask & (1 << bit)) or "-"


class Trace:
    def __init__(self, path):
        self.file = open(path, "rb")
        self.header = Header.parse(self.file.read(128))
        self.sections = self.header.section_sizes()
        self.state_size = self.header.record_header_size + sum(size for _, size in self.sections)

    def close(self):
        self.file.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __iter__(self):
        """Records in file order. Each is read fully, including its sections and writes."""
        self.file.seek(self.header.header_size)
        while True:
            data = self.file.read(self.state_size)
            if not data:
                return
            if len(data) != self.state_size or data[:4] != RECORD_MAGIC:
                raise ValueError(f"truncated or corrupt record at offset {self.file.tell() - len(data)}")
            (frame, flags, num_writes, cycle, pad1, pad2, pc, sr) = struct.unpack_from("<IIIQHHII", data, 4)
            d = struct.unpack_from("<8I", data, 36)
            a = struct.unpack_from("<8I", data, 68)
            (other_sp, vdp_frame) = struct.unpack_from("<II", data, 100)
            pos = self.header.record_header_size
            parts = {}
            for name, size in self.sections:
                parts[name] = data[pos:pos + size]
                pos += size
            rec = Record(frame, bool(flags & 1), cycle, pad1, pad2, pc, sr, d, a, other_sp, vdp_frame, **parts)
            entry_size = self.header.write_entry_size
            wdata = self.file.read(num_writes * entry_size)
            for i in range(num_writes):
                (wcycle, wpc, waddr, kind, source, wflags, value) = struct.unpack_from("<QIIBBBB", wdata, i * entry_size)
                rec.writes.append(Write(wcycle, wpc, waddr, WRITE_KINDS.get(kind, str(kind)),
                                        SOURCES.get(source, str(source)), bool(wflags & 1), value))
            yield rec


def cmd_info(path):
    with Trace(path) as trace:
        h = trace.header
        print(f"ROM SHA-256   {h.rom_sha256.hex()}")
        print(f"frames        {h.first_frame}-{h.last_frame}, {h.record_count} records, complete={h.complete}")
        print(f"machine       {'PAL' if h.pal else 'NTSC'} {h.master_clock} Hz, version register {h.version_reg:02X}, {h.pad_type}-button pads")
        print(f"sections      {', '.join(f'{n} ({s})' for n, s in trace.sections)}")
        irq = no_irq = writes = 0
        for rec in trace:
            irq += rec.irq
            no_irq += not rec.irq
            writes += len(rec.writes)
        print(f"records       {irq} irq, {no_irq} no_irq, {writes} writes")


def cmd_records(path):
    with Trace(path) as trace:
        for rec in trace:
            print(f"{rec.frame:>6} {'irq   ' if rec.irq else 'no_irq'} cycle={rec.cycle:<12} pc={rec.pc:06X} sr={rec.sr:04X} "
                  f"writes={len(rec.writes):<4} pad1={pad_names(rec.pad1)} pad2={pad_names(rec.pad2)}")


def cmd_writes(path, frame=None):
    with Trace(path) as trace:
        for rec in trace:
            if frame is not None and rec.frame != frame:
                continue
            for w in rec.writes:
                print(f"{rec.frame:>6} {w}")


def cmd_diff(path_a, path_b):
    with Trace(path_a) as ta, Trace(path_b) as tb:
        if ta.header != tb.header:
            print("headers differ")
        for ra, rb in zip(ta, tb):
            for name in ("frame", "irq", "cycle", "pad1", "pad2", "pc", "sr", "d", "a", "other_sp", "vdp_frame"):
                if getattr(ra, name) != getattr(rb, name):
                    print(f"frame {ra.frame}: {name} differs: {getattr(ra, name)} vs {getattr(rb, name)}")
                    return 1
            for name, _ in ta.sections:
                va, vb = getattr(ra, name), getattr(rb, name)
                if va != vb:
                    offset = next(i for i in range(len(va)) if va[i] != vb[i])
                    print(f"frame {ra.frame}: {name} differs first at offset {offset:#x}: {va[offset]:02X} vs {vb[offset]:02X}")
                    return 1
            if ra.writes != rb.writes:
                for i, (wa, wb) in enumerate(zip(ra.writes, rb.writes)):
                    if wa != wb:
                        print(f"frame {ra.frame}: write {i} differs:\n  {wa}\n  {wb}")
                        return 1
                print(f"frame {ra.frame}: write counts differ: {len(ra.writes)} vs {len(rb.writes)}")
                return 1
        print("no difference")
        return 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "info":
        cmd_info(argv[2])
    elif cmd == "records":
        cmd_records(argv[2])
    elif cmd == "writes":
        cmd_writes(argv[2], int(argv[3]) if len(argv) > 3 else None)
    elif cmd == "diff" and len(argv) > 3:
        return cmd_diff(argv[2], argv[3])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    #exit quietly when piped into head and the like
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    sys.exit(main(sys.argv))
