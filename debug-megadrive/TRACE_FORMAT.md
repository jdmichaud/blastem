# blastdbg frame traces (BDTRACE version 1)

A trace records the state of a Mega Drive game once per VBlank, from power-on, while it
replays a per-VBlank pad input file. Runs are deterministic: the same ROM, options and
input file always give a byte-identical trace. `bdtrace.py`, next to this file, is the
reference reader.

## Running

```
blastdbg --trace FILE [--frames FIRST-LAST] [--input FILE] [--record LIST]
         [--screenshots DIR [--screenshot-every N]]
         [--translated FILE] [--translated-z80 FILE] [--watch START[-END]]...
         [--audio FILE.wav] [--audio-ym FILE.wav] [--audio-psg FILE.wav] [--audio-rate HZ]
         [-p 3|6] [-r J|U|E] ROM
```

| Option | Meaning |
|--------|---------|
| `--trace FILE` | Run in trace mode: no debugger, run from power-on, write records to FILE, exit after the last one |
| `--frames FIRST-LAST` | VBlanks to record, inclusive (default `0-599`). Emulation always starts at power-on |
| `--input FILE` | Pad input, see below. Without it no button is ever pressed |
| `--record LIST` | Extra sections in every record, comma separated: `vram`, `cram`, `vsram`, `regs`, `z80`, or `all`. Work RAM is always recorded |
| `--screenshots DIR` | Save `DIR/NNNNNN.png` for recorded VBlanks (created if needed) |
| `--screenshot-every N` | Only every Nth recorded VBlank, counted from FIRST (default 1) |
| `--translated FILE` | When the trace ends, write the start address of every translated 68K instruction (see `ta` in SKILL.md) |
| `--translated-z80 FILE` | Same for the Z80 |
| `--watch START[-END]` | Print every 68K write to this work RAM range on stdout, as `Write $FFxxxx.b = $vv ...` or `Write $FFxxxx.w = $vvvv pc=PPPPPP vblank=N cycle=C` (see `ww` in SKILL.md). Repeatable, up to 16 ranges. Does not change the trace |
| `--audio FILE.wav` | Write the sound BlastEm plays (YM2612 + PSG mix), see Audio below |
| `--audio-ym FILE.wav` | Write the YM2612 alone |
| `--audio-psg FILE.wav` | Write the PSG alone |
| `--audio-rate HZ` | Sample rate of the WAV files (default: `audio.rate` in the config, 48000) |
| `-p 3\|6` | Pad type for both ports (default 3-button) |
| `-r J\|U\|E` | Force the region |

On success it prints `Wrote N trace records` and exits with status 0. Errors are printed on
stderr and exit with status 1.

Size: a record with work RAM only is 65,648 bytes plus 24 bytes per logged write, roughly
4 MB per second of game time. With all sections it is about 140 KB per record.

## VBlanks and records

VBlank N is the Nth time the VDP starts vertical blanking (the level 6 interrupt becomes
pending), counting from 0 at power-on. It happens whether or not the game has the interrupt
enabled or masked. There is exactly one record per VBlank in the range, in order:

- **irq record**: the game accepted the level 6 interrupt after VBlank N and before VBlank
  N+1. The record is taken at acceptance: the exception frame (SR, PC) has been pushed,
  SR has been updated, the vector has been read, and the handler's first instruction has
  not run. `pc` is the handler address (the vector target). This is the same state the
  debugger shows at a breakpoint on the handler.
- **no_irq record**: the interrupt was not accepted before VBlank N+1 (the game had it
  masked or disabled, typically while loading). The record is the state at VBlank N, taken
  at the first synchronization point after the VBlank started. `pc` is approximate: the
  address after the last instruction that had a memory operand.

Only the first level 6 acceptance after a VBlank produces a record.

Records are numbered by VBlank, not by the game's own tick counter. A lag frame (the game's
logic overrunning a frame) appears as a VBlank whose record shows the tick counter
unchanged, so a comparison should index by the tick counter it finds in RAM.

## Input file

A text file with one line per VBlank. Line N (counting only data lines) is applied at
VBlank N and held until VBlank N+1. Before VBlank 0 no button is pressed, and after the last
line no button is pressed.

```
# comment lines and blank lines are ignored
000 000
080 000
```

Each data line holds two hexadecimal masks, pad 1 then pad 2, separated by spaces. Bits:

| Bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|-----|---|---|---|---|---|---|---|---|---|---|----|----|
| Button | up | down | left | right | A | B | C | start | X | Y | Z | mode |

X, Y, Z and mode have no effect on a 3-button pad. The input is applied at the same point the
record is captured, so an irq record's handler always sees line N. With the interrupt
masked, a pad read between the start of VBlank N and the next synchronization point still
sees line N-1.

## File layout

All integers are little-endian. Memory sections are copied as the 68K sees them (big-endian
words stay in their byte order).

### Header (128 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 8 | Magic `BDTRACE\0` |
| 8 | 4 | Format version (1) |
| 12 | 4 | Header size (128). Records start here |
| 16 | 32 | SHA-256 of the ROM image as loaded (the file itself for a plain `.bin`/`.md`) |
| 48 | 4 | Section flags: `0x01` VRAM, `0x02` CRAM, `0x04` VSRAM, `0x08` VDP registers, `0x10` Z80 RAM |
| 52 | 4 | First VBlank requested |
| 56 | 4 | Last VBlank requested |
| 60 | 4 | Number of records written |
| 64 | 4 | Master clock in Hz (53693175 NTSC, 53203395 PAL) |
| 68 | 1 | 1 if PAL (50 Hz) |
| 69 | 1 | Pad type (3 or 6) |
| 70 | 1 | Version register ($A10001) value, which holds the region |
| 71 | 1 | 1 if the trace was completed. 0 means the run was interrupted and the record count is not final |
| 72 | 4 | Record header size (112) |
| 76 | 4 | Write entry size (24) |
| 80 | 4 | Work RAM section size (65536) |
| 84 | 4 | VRAM section size (65536) |
| 88 | 4 | CRAM section size (128) |
| 92 | 4 | VSRAM section size (80 on a Mega Drive) |
| 96 | 4 | VDP register section size (24) |
| 100 | 4 | Z80 RAM section size (8192) |
| 104 | 4 | Screenshot interval (0 if none) |
| 108 | 20 | Reserved (0) |

The section sizes are given even for sections that are not recorded.

### Record

A 112-byte record header, then the sections, then the write entries.

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | Magic `REC\0` |
| 4 | 4 | VBlank number |
| 8 | 4 | Flags: bit 0 set for an irq record, clear for no_irq |
| 12 | 4 | Number of write entries following the sections |
| 16 | 8 | Capture cycle: master clock cycles since power-on (the 68K runs at master/7) |
| 24 | 2 | Pad 1 input mask in effect (the input line of this VBlank) |
| 26 | 2 | Pad 2 input mask |
| 28 | 4 | PC (see above) |
| 32 | 4 | SR (low 16 bits) |
| 36 | 32 | D0-D7 |
| 68 | 32 | A0-A7 (A7 is the active stack pointer: SSP in supervisor mode) |
| 100 | 4 | The inactive stack pointer (USP in supervisor mode) |
| 104 | 4 | The VDP's own count of completed frames (diagnostic) |
| 108 | 4 | Reserved (0) |

Sections, in this order, each present only if its flag is set (work RAM always is):

| Section | Size | Contents |
|---------|------|----------|
| Work RAM | 65536 | $FF0000-$FFFFFF, byte i is address $FF0000+i |
| VRAM | 65536 | As read through the data port: byte i is VRAM address i |
| CRAM | 128 | 64 big-endian words `0000BBB0GGG0RRR0` |
| VSRAM | 80 | 40 big-endian words |
| VDP registers | 24 | Registers 0-23 |
| Z80 RAM | 8192 | $A00000-$A01FFF (Z80 $0000-$1FFF) |

### Write entries (24 bytes each)

Every write to the sound chips and the Z80 bus logged since the previous record, up to and
including this record's capture cycle, sorted by cycle (ties keep their execution order).

| Offset | Size | Field |
|--------|------|-------|
| 0 | 8 | Cycle: master clock cycles since power-on |
| 8 | 4 | PC of the writing instruction (68K only) |
| 12 | 4 | Address, meaning depends on the kind |
| 16 | 1 | Kind (below) |
| 17 | 1 | Source: 0 = 68K, 1 = Z80 |
| 18 | 1 | Flags: bit 0 set if the PC is valid |
| 19 | 1 | Value |
| 20 | 4 | Reserved (0) |

| Kind | Name | Address | Logged when |
|------|------|---------|-------------|
| 1 | ym | YM2612 port 0-3 (0 = address part 1, 1 = data part 1, 2 = address part 2, 3 = data part 2) | 68K write to $A04000-$A05FFF, Z80 write to $4000-$5FFF |
| 2 | psg | VDP port offset ($11) | 68K write to $C00011, Z80 write to $7F11 |
| 3 | z80ram | Z80 RAM offset $0000-$1FFF | 68K write to $A00000-$A03FFF while it holds the Z80 bus (writes without the bus are dropped by the hardware and not logged) |
| 4 | z80busreq | $A11100 | 68K write, value bit 0 = request |
| 5 | z80reset | $A11200 | 68K write, value bit 0 = release from reset |
| 6 | z80bank | $6000-$60FF | Bank register write from the 68K ($A06000) or the Z80 ($6000-$60FF) |

The 68K PC is exact: it is the start of the instruction doing the write. Z80 writes have no
PC (flag clear, PC 0). Word writes to the Z80 bus log the byte that reaches it (the high
byte). The Z80's writes to its own RAM are not logged.

## Audio

The WAV files are 16-bit stereo PCM. They hold what BlastEm's own mixer produces, computed with
its code: each chip's output is low-pass filtered (`audio.lowpass_cutoff`, 3390 Hz by default),
resampled to the output rate by linear interpolation, scaled by its gain (`audio.fm_gain`,
`audio.psg_gain`, then `audio.gain`, all 0 dB by default), summed and clamped to 16 bits. The
YM2612 is emulated as configured (`audio.fm_dac`, `zero_offset` by default, which gives the YM2612
a constant DC offset while it is silent). The PSG, a mono chip, is written to both channels. The
mix equals the YM2612 file plus the PSG file within one unit of rounding.

Sample k is the instant k / rate seconds after power-on, so a master clock cycle c of the trace
(record cycles, write cycles) is sample c × rate / master clock. Use the record's cycle to find
where a VBlank's audio starts: VBlank 0 is not at cycle 0 (it is at cycle 768992 in Streets of Rage),
so N × 896040 is off by that offset. The files end at the last record. Like the trace, the
audio is deterministic: the same run gives byte-identical files.

Measured on Streets of Rage: the PSG output starts 2 samples after the cycle of its first volume
write (the low-pass filter's delay), and its pitch matches the tone registers exactly.

Real-time playback also adjusts the resampling rate slightly to keep audio and video in sync.
The capture doesn't, since it isn't paced by a sound card.

## Screenshots

`NNNNNN.png` is the frame displayed just before VBlank N: its active display (320x224 in H40
mode, 256x224 in H32), without borders. VBlank 0 has no screenshot, because the VDP starts
partway through a frame at power-on.

## Version history

- 1: first version.
