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

**Breakpoint with continue**: set a breakpoint, continue, then inspect when it hits:

```bash
timeout 30 bash -c 'printf "b 1234\nc\np/x d0\np/x a0\nbt\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
```

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

Use the bundled 68K disassembler for static analysis:

```bash
${CLAUDE_SKILL_DIR}/dis [options] <rom-file>
```

### Options

| Flag | Description |
|------|-------------|
| `-a` | Show addresses alongside disassembly |
| `-l` | Produce assembly output with labels (can be re-assembled) |
| `-o` | Only disassemble addresses specified with `-s` (don't continue past them) |
| `-s OFFSET` | Start disassembling at hex address OFFSET (can specify multiple times) |
| `-r` | Start from the reset vector (address at `$000004`) instead of `$000000` |
| `-f FILE` | Read start addresses from FILE (one hex address per line; `name $ADDR` assigns a label) |
| `-v` | Treat input as VOS program module format |

### Examples

```bash
# Disassemble from beginning, show addresses
${CLAUDE_SKILL_DIR}/dis -a <rom> | head -200

# Disassemble starting from a specific address
${CLAUDE_SKILL_DIR}/dis -a -s 200 <rom> | head -100

# Start from the reset vector (entry point)
${CLAUDE_SKILL_DIR}/dis -a -r <rom> | head -100

# Only disassemble specific routines (don't follow past them)
${CLAUDE_SKILL_DIR}/dis -a -o -s 200 -s 400 <rom>

# Produce re-assemblable output with labels
${CLAUDE_SKILL_DIR}/dis -l -r <rom> > disassembly.s68

# Use an address file with named labels
echo -e "main_loop 200\nvblank_handler 78" > addrs.txt
${CLAUDE_SKILL_DIR}/dis -a -f addrs.txt <rom>
```

Pipe through `head` or `grep` to focus on regions of interest.

## Workflow

1. **Read the ROM header first** to identify the entry point and initial SP:
   ```bash
   timeout 10 bash -c 'printf "p/x \$0.l\np/x \$4.l\nq\n" | ${CLAUDE_SKILL_DIR}/blastdbg <rom>'
   ```
   The first value is the initial stack pointer, the second is the entry point (reset vector). The disassembled instruction at the entry point is also shown.

2. **Disassemble the entry point** to understand the boot sequence. Use `dis` or step with `n`.

3. **Read the ROM title** from the header using long reads at $120+ (domestic) or $150+ (overseas). The values are ASCII packed into 32-bit longs.

4. **Set breakpoints** at addresses of interest, `c` to continue, then inspect state when they hit.

5. **Inspect VDP state** with `vr` (registers) and `vs` (sprites) to understand graphics.

6. **Inspect sound** with `yc` (FM channels) and `yt` (timers) for audio debugging.

7. When the user asks about specific behavior, set breakpoints at the relevant code path, continue execution, and analyze the register/memory state at that point. Explain findings in terms of 68K assembly and Mega Drive hardware.

## Important Notes

- All addresses in debugger commands are in **hexadecimal** (no prefix needed for `b` and `a`)
- The `p` command uses `$` or `0x` prefix for memory reads
- Hitting Enter with no input repeats the last command
- The `co` (command) feature lets you script breakpoint actions: after `co N`, type commands line by line, then `end`
- In non-interactive mode, `co` requires: `co N\ncommand1\ncommand2\nend\n`
- Informational output (ROM info, IO config) goes to stderr; debugger output goes to stdout
