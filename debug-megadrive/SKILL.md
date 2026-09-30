---
name: debug-megadrive
description: Debug Mega Drive / Genesis ROMs using BlastEm's built-in 68K/Z80 debugger
argument-hint: <rom-file>
allowed-tools: Bash, Read, Grep, Glob
---

# Mega Drive ROM Debugger (blastdbg)

You are an expert Mega Drive / Sega Genesis reverse engineer and debugger. You use blastdbg, a headless build of BlastEm's command-line debugger, to inspect and debug ROM files.

## Setup

Debugger path: `${CLAUDE_SKILL_DIR}/blastdbg`
Disassembler path: `${CLAUDE_SKILL_DIR}/dis`

The ROM file to debug is: `$ARGUMENTS`

## Launching the debugger

blastdbg is a headless binary — no SDL, no window, no audio. Just run it directly:

```
${CLAUDE_SKILL_DIR}/blastdbg <rom-file>
```

It starts the debugger immediately at the ROM entry point, prints the first instruction, and waits for commands on stdin.

Options: `-p 3|6` pad type (default 3-button), `-r J|U|E` force the region, `-n` disable the Z80, `-h` help. The `--trace` options below run it without the debugger.

## Sending commands non-interactively

Pipe commands via stdin. Always wrap with `timeout` to prevent hangs, and always end with `q` to exit cleanly:

```bash
timeout 30 bash -c 'printf "CMD1\nCMD2\n...\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
```

For commands that involve `c` (continue) or `a ADDRESS` (advance), the debugger resumes execution until a breakpoint is hit. If no breakpoint is hit, the process runs until `timeout` kills it. **Always set a breakpoint before using `c`.**

**Stepping through code**: repeat `n` (or `s`) N times in your printf:

```bash
timeout 30 bash -c 'printf "n\nn\nn\nn\nn\np/x pc\np/x d0\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
```

**Always end with `q`**: stdout is buffered when piped, so if `timeout` kills the process, all of its output is lost, including the output of commands that ran before the hang.

**Breakpoint with continue**: set a breakpoint, continue, then inspect when it hits:

```bash
timeout 30 bash -c 'printf "b 1234\nc\np/x d0\np/x a0\nbt\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
```

## Seeing the screen

blastdbg renders every frame even though it has no window. `ss` saves the last **completed** frame (active display only, no borders: 320x224 in H40 mode, 256x224 in H32), so the image is always a whole frame, even when stopped mid-frame at a breakpoint. Open the saved PNG with the **Read** tool to look at it.

Use `fr N` to let the game run for N frames. It stops right after frame N completes, so an `ss` afterwards shows exactly that frame. At 60 frames per second, `fr 60` is one second of game time.

```bash
timeout 30 bash -c 'printf "fr 600\nss /tmp/title.png 2\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
```

Then Read `/tmp/title.png`. A scale of 2 makes small text and sprites easier to read. Before the first frame completes (for example at the entry point) there is nothing to save, so run at least `fr 1` first.

Notes:
- `fr` is cancelled if a breakpoint is hit first. `p f` prints the current frame number.
- Delete or avoid breakpoints in code that runs every frame (e.g. the VBlank handler) before using `fr`, or it will stop there instead.
- Boot, logos and intros often take several hundred frames. Take screenshots along the way to see where the game is.

## Controller input

Pads 1 and 2 are 3-button gamepads by default, the pad most games were written for. Start blastdbg with `-p 6` for 6-button pads. Button names (case-insensitive): `up`, `down`, `left`, `right`, `a`, `b`, `c`, `start`, `x`, `y`, `z`, `mode`. On a 3-button pad `x`, `y`, `z` and `mode` have no effect.

`jp` holds buttons down until `jr` releases them, so combine them with `fr` to press for a given number of frames. Most games read the pad once per frame and react on a new press, so:
- hold a button for a few frames (`fr 5`) so the game sees it,
- release it and let a few frames pass before pressing the same button again,
- wait for screen transitions (`fr 60` or more) before the next input.

Tap Start, wait one second, then screenshot:

```bash
timeout 60 bash -c 'printf "fr 600\njp start\nfr 5\njr\nfr 60\nss /tmp/menu.png\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
```

Hold right for 1.5 seconds, then press A and C together on pad 1, and B on pad 2:

```
jp right
fr 90
jr
jp a c
jp 2 b
fr 5
jr 1
jr 2
```

Every run starts from power-on, so to reach a given point in the game, replay the whole input sequence. Build it up step by step, checking with `ss` each time.

## Dumping VDP memory

`vd PREFIX` writes four files:

| File | Size | Contents |
|------|------|----------|
| `PREFIX.vram` | 64 KB | VRAM, in the same byte order the 68K sees through the data port (big endian words). Tiles are 32 bytes each (8x8, 4 bits per pixel, 4 bytes per row, high nibble = left pixel) |
| `PREFIX.cram` | 128 bytes | 64 palette entries as big endian words `0000BBB0GGG0RRR0` (4 palettes of 16 colors) |
| `PREFIX.vsram` | 80 bytes | 40 vertical scroll words |
| `PREFIX.regs` | 24 bytes | VDP registers 0-23 (decoded with `vr`) |

Use `vr` to find where plane A/B, the window, the sprite table and the horizontal scroll table live in VRAM, then read those regions from the dump (e.g. with Python) to decode tile maps, sprites or scroll values. VRAM, CRAM and VSRAM are not visible in the 68K address space, so `p` cannot read them.

## Frame traces

For comparing a game run frame by frame, blastdbg can record the machine state once per VBlank from power-on, while replaying a pad input file, without the debugger:

```bash
${CLAUDE_SKILL_DIR}/blastdbg --trace /tmp/run.trc --frames 0-3599 --input pads.txt --record all --screenshots /tmp/shots --screenshot-every 60 <rom>
```

Each record holds the VBlank number, whether the level 6 interrupt was taken, the cycle, the 68K registers, the 64 KB work RAM, optionally VRAM/CRAM/VSRAM/VDP registers/Z80 RAM, and every YM2612, PSG and Z80 bus write since the previous record with its cycle and 68K PC. Runs are deterministic: the same ROM, options and input give a byte-identical file.

The input file has one line per VBlank with two hex button masks (pad 1, pad 2), bit 0 = up through bit 11 = mode. The full format, semantics and options are in `${CLAUDE_SKILL_DIR}/TRACE_FORMAT.md`. Read traces with `${CLAUDE_SKILL_DIR}/bdtrace.py`, as a Python module or from the command line:

```bash
python3 ${CLAUDE_SKILL_DIR}/bdtrace.py info /tmp/run.trc      # header and totals
python3 ${CLAUDE_SKILL_DIR}/bdtrace.py records /tmp/run.trc   # one line per VBlank
python3 ${CLAUDE_SKILL_DIR}/bdtrace.py writes /tmp/run.trc 600
python3 ${CLAUDE_SKILL_DIR}/bdtrace.py diff a.trc b.trc       # first difference
```

## Watching RAM writes

`ww START [END]` reports every 68K write to a work RAM range without stopping, while `c`, `fr` or stepping runs the game. Addresses are hex, `$E00000`-`$FFFFFF` (reported at their `$FF0000` alias), and END defaults to START+1 (one word). Up to 16 ranges.

```bash
timeout 60 bash -c 'printf "ww FFFB06\nfr 200\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
```
```
Write $FFFB06.w = $003F pc=01A074 vblank=158 cycle=142370200
```

- The value is read back after the write. An odd address is a byte write (`.b`). An even address shows the whole word (`.w`), which may have been written as a word or as its high byte.
- A long write shows as two word writes (high word first).
- `pc` is the exact start of the writing instruction, `vblank` the most recent VBlank (-1 before the first), `cycle` the master clock cycle since power-on (the same clock as trace files).
- Only 68K writes are seen, not DMA or Z80 bank-window writes. Watching does not change emulation or timing; each write in a watched 2 KB page costs a call into C, so large ranges are slower.
- `--watch START[-END]` does the same in trace mode (see below).

## Translated code addresses

`ta FILE [ZFILE]` writes the start address of every instruction the JIT has translated since power-on: 68K addresses to FILE (6 hex digits per line, sorted), Z80 addresses to ZFILE (4 hex digits). Run the game through the parts you care about first, then dump:

```bash
timeout 120 bash -c 'printf "fr 3000\nta /tmp/code.68k /tmp/code.z80\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
```

This is an over-approximation of executed code: the JIT translates code ahead of execution, including both sides of conditional branches, whether or not they were taken. Treat the addresses as instruction starts and entry points for a disassembler, not as proof of execution. Mirrored addresses are reported at their lowest alias (work RAM code at `$E00000`-based addresses, Z80 RAM at `$0000`-`$1FFF`).

## Understanding the output

When the debugger stops (at entry or a breakpoint), it prints the current instruction:
```
208: tst.l $A10008
```
This is `ADDRESS: DISASSEMBLED_INSTRUCTION`. Each `>` in the output corresponds to a command prompt. Responses appear between prompts:
```
>$0.l: ffff00          <- response to p/x $0.l
>$4.l: 208             <- response to p/x $4.l
>20E: bne #248 <308>   <- next instruction after stepping with n
```

## 68K Debugger Commands

| Command | Description |
|---------|-------------|
| `b ADDRESS` | Set a breakpoint at ADDRESS (hex, no `$` prefix) |
| `d BREAKPOINT` | Delete 68K breakpoint by index number |
| `co BREAKPOINT` | Attach commands to run each time BREAKPOINT is hit |
| `a ADDRESS` | Advance (run) to ADDRESS, then break |
| `n` | Next instruction (step over: does NOT follow bsr/jsr) |
| `s` | Step into (follows bsr/jsr) |
| `o` | Step over, skipping backward branches (escape loops) |
| `c` | Continue execution until next breakpoint |
| `bt` | Print backtrace (walks the stack for return addresses) |
| `p VALUE` | Print register or memory value (decimal) |
| `p/x VALUE` | Print in hex |
| `p/X VALUE` | Print in uppercase hex |
| `p/d VALUE` | Print in decimal |
| `p/c VALUE` | Print as character |
| `di VALUE` | Auto-display value at every breakpoint hit |
| `di/x VALUE` | Auto-display in hex |
| `se DEST VALUE` | Set register or address to VALUE |
| `sr` | Soft reset the emulated system |
| `vs` | Print VDP sprite table |
| `vr` | Print VDP register info |
| `vd PREFIX` | Dump VRAM, CRAM, VSRAM and VDP registers to `PREFIX.vram`, `.cram`, `.vsram`, `.regs` |
| `ss FILE [SCALE]` | Save the last completed frame as PNG (PPM if FILE ends in `.ppm`), upscaled by SCALE (1-8) |
| `fr [N]` | Run N frames (default 1), then break |
| `jp [PAD] BUTTON...` | Press and hold gamepad buttons (PAD is 1 or 2, default 1) |
| `jr [PAD] [BUTTON...]` | Release gamepad buttons (all buttons on the pad if none given) |
| `j` | Show buttons currently held on both pads |
| `ww [START [END]]` | Print every 68K write to work RAM START-END (hex, default one word) while the game runs; no argument lists the watches |
| `wc` | Clear all write watches |
| `ta FILE [ZFILE]` | Write the start address of every 68K (and Z80) instruction translated so far, one hex address per line |
| `yc [N]` | Print YM-2612 channel info (all, or channel N: 1-6) |
| `yt` | Print YM-2612 timer info |
| `zb ADDRESS` | Set a Z80 breakpoint |
| `zp VALUE` | Print a Z80 register/memory value |
| `zp/x VALUE` | Print Z80 value in hex |
| `?` | Display help |
| `q` | Quit |

## Printable values for the `p` command

| Syntax | Description |
|--------|-------------|
| `d0`-`d7` | Data registers |
| `a0`-`a7` | Address registers (a7 = stack pointer) |
| `sr` | Status register (flags + supervisor byte) |
| `pc` | Program counter (current address) |
| `c` | Current CPU cycle count |
| `f` | Current VDP frame number |
| `d0.w`, `d0.b` | Word or byte portion of a register |
| `$ADDRESS` or `0xADDRESS` | Read **word** at memory address |
| `$ADDRESS.l` | Read **long** (32-bit) at memory address |
| `$ADDRESS.b` | Read **byte** at memory address |
| `(a0)`, `(d0)` | Read word at address held in register |
| `(a0).l`, `(a0).b` | Read long/byte via register indirection |

Note: `$` in memory addresses must be escaped as `\$` inside bash strings. Use single-quoted printf strings.

## `se` (set) command

```
se d0 $1234       # set d0 to 0x1234
se a0 d1          # set a0 to the value of d1
se d0 0xFF        # set d0 to 255
```

## Z80 Debugger Commands

When a Z80 breakpoint is hit, you enter the Z80 debugger context:

| Command | Description |
|---------|-------------|
| `b ADDRESS` | Set Z80 breakpoint |
| `de BREAKPOINT` | Delete Z80 breakpoint by index |
| `a ADDRESS` | Advance to address |
| `n` | Next instruction |
| `c` | Continue |
| `p VALUE` | Print value |
| `di VALUE` | Auto-display at breakpoints |
| `s FILE` | Dump Z80 RAM to file |
| `q` | Quit |

Z80 printable registers: `a`, `b`, `c`, `d`, `e`, `h`, `l`, `af`, `bc`, `de`, `hl`, `ix`, `iy`, `sp`, `ba` (bank register). Append `'` for alternate register set (e.g., `af'`, `bc'`). Sub-registers: `ixh`, `ixl`, `iyh`, `iyl`. Special: `cy` (cycle), `im` (interrupt mode), `iff1`, `iff2`, `in` (int_cycle).

## Mega Drive Memory Map

| Address Range | Description |
|---------------|-------------|
| `$000000-$3FFFFF` | Cartridge ROM (up to 4MB) |
| `$400000-$7FFFFF` | Extra cart space / bank-switched mapper |
| `$A00000-$A01FFF` | Z80 RAM (8KB) |
| `$A02000-$A0FFFF` | Z80 address space (mirrors, YM2612, etc.) |
| `$A04000-$A04003` | YM2612 FM synth registers |
| `$A10000-$A1001F` | I/O registers (gamepads, serial, etc.) |
| `$A11100` | Z80 bus request |
| `$A11200` | Z80 reset |
| `$C00000-$C00003` | VDP data port |
| `$C00004-$C00007` | VDP control port (also status on read) |
| `$C00008-$C0000F` | VDP HV counter |
| `$C00011` | PSG (SN76489) |
| `$FF0000-$FFFFFF` | 68K Work RAM (64KB, mirrored) |

## ROM Header ($000100-$0001FF)

| Offset | Size | Field |
|--------|------|-------|
| `$100` | 16 | System type ("SEGA MEGA DRIVE" / "SEGA GENESIS") |
| `$110` | 16 | Copyright / release date |
| `$120` | 48 | Domestic (Japanese) title |
| `$150` | 48 | Overseas title |
| `$180` | 14 | Serial number / version |
| `$190` | 2 | Checksum |
| `$1A0` | 16 | I/O device support codes |
| `$1A4` | 4 | ROM start address |
| `$1A8` | 4 | ROM end address |
| `$1B0` | 12 | RAM start/end/flags |
| `$1F0` | 16 | Region codes (J=Japan, U=Americas, E=Europe) |

## 68K Vector Table ($000000-$0000FF)

| Address | Vector |
|---------|--------|
| `$000000` | Initial SP (stack pointer) |
| `$000004` | Reset vector (entry point) |
| `$000008` | Bus error |
| `$00000C` | Address error |
| `$000010` | Illegal instruction |
| `$000014` | Division by zero |
| `$000018` | CHK exception |
| `$00001C` | TRAPV |
| `$000020` | Privilege violation |
| `$000024` | Trace |
| `$000060` | Spurious interrupt |
| `$000064` | IRQ 1 |
| `$000068` | IRQ 2 (external interrupt) |
| `$00006C` | IRQ 3 |
| `$000070` | IRQ 4 (HBlank) |
| `$000074` | IRQ 5 |
| `$000078` | IRQ 6 (VBlank) |
| `$00007C` | IRQ 7 (NMI, active high) |

## Offline Disassembly

Use the bundled 68K disassembler for static analysis. **The ROM file comes first**, then options and extra entry points:

```bash
${CLAUDE_SKILL_DIR}/dis <rom-file> [options] [ADDRESS[=LABEL]...]
```

It is a recursive-descent disassembler. It starts from the reset vector and the level 2, 4 and 6 interrupt vectors, follows branches, jumps and calls, and prints every instruction it reached in address order, one `ADDRESS: instruction` line each (hex addresses). Code only reached through jump tables or computed jumps is not found unless you give its address.

### Options

| Argument | Description |
|----------|-------------|
| `ADDRESS` | Extra entry point, in hex (no prefix), e.g. `73206` |
| `ADDRESS=LABEL` | Extra entry point with a label name (used with `-l`) |
| `-f FILE` | Read extra entry points from FILE: one hex address per line, optionally `ADDRESS=LABEL` |
| `-o` | Only start from the addresses given on the command line or with `-f`, not from the vectors |
| `-l` | Assembly output with labels (`ADR_xxxx`, or your label names) instead of `ADDRESS:` prefixes |
| `-a` | With `-l`, add the address as a comment after each instruction |
| `-s LOADADDR` | Address the file is loaded at (default 0). **Not** a start address; leave it alone for cartridge ROMs. Decimal, or hex with a `0x` prefix |
| `-v` | Treat the input as a VOS program module (`-r` only applies to those) |

### Examples

```bash
# Everything reachable from the vectors
${CLAUDE_SKILL_DIR}/dis <rom> | head -100

# Only one routine and what it calls
${CLAUDE_SKILL_DIR}/dis <rom> -o 73206

# The vectors plus extra entry points found at runtime (e.g. with ta)
${CLAUDE_SKILL_DIR}/dis <rom> 19D16 73206

# Named entry points from a file, as labelled assembly with address comments
printf '19D16=vblank_handler\n73206=ym_write\n' > addrs.txt
${CLAUDE_SKILL_DIR}/dis <rom> -l -a -f addrs.txt > disassembly.s68
```

`ta` output (see below) can be passed with `-f` to disassemble every instruction the game actually reached. Pipe through `head` or `grep` to focus on regions of interest. With a wrong argument order `dis` prints its usage and exits with status 1.

## Workflow

1. **Read the ROM header first** to identify the entry point and initial SP:
   ```bash
   timeout 10 bash -c 'printf "p/x \$0.l\np/x \$4.l\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
   ```
   The first value is the initial stack pointer, the second is the entry point (reset vector). The disassembled instruction at the entry point is also shown.

2. **Disassemble the entry point** to understand the boot sequence. Use `dis` or step with `n`.

3. **Read the ROM title** from the header using long reads at $120+ (domestic) or $150+ (overseas). The values are ASCII packed into 32-bit longs.

4. **Set breakpoints** at addresses of interest, `c` to continue, then inspect state when they hit.

5. **Look at the screen** with `fr` + `ss` + Read. Inspect VDP state with `vr` (registers) and `vs` (sprites), and dump VRAM/CRAM/VSRAM with `vd` to understand graphics.

6. **Inspect sound** with `yc` (FM channels) and `yt` (timers) for audio debugging.

7. When the user asks about specific behavior, set breakpoints at the relevant code path, continue execution, and analyze the register/memory state at that point. Explain findings in terms of 68K assembly and Mega Drive hardware.

## Important Notes

- All addresses in debugger commands are in **hexadecimal** (no prefix needed for `b` and `a`)
- The `p` command uses `$` or `0x` prefix for memory reads
- Hitting Enter with no input repeats the last command
- The `co` (command) feature lets you script breakpoint actions: after `co N`, type commands line by line, then `end`. Commands such as `ss` work there too, e.g. to take a screenshot every time a breakpoint is hit
- In non-interactive mode, `co` requires: `co N\ncommand1\ncommand2\nend\n`
- Informational output (ROM info, IO config) goes to stderr; debugger output goes to stdout
