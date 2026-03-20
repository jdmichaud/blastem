# BlastEm

Sega Genesis / Mega Drive emulator by Michael Pavone. This fork (`libretro` branch) adds libretro core support and the `blastdbg` headless debugger.

## Build

```bash
make                    # builds blastem, dis, zdis, vgmplay, termhelper
make blastdbg           # headless debugger (no SDL dependency)
make blastdbg-static    # fully static headless debugger
make dis-static         # fully static 68K disassembler
make DEBUG=1            # build with -g3 -O0
```

Dependencies for the full build: SDL2, OpenGL/GLEW, zlib (bundled). The `blastdbg` and `dis-static` targets need only libc and the bundled zlib.

## Architecture

The emulator is written in C (gnu99) with x86_64 JIT recompilation for the 68K and Z80 CPUs.

### Key source files

| File | Purpose |
|------|---------|
| `blastem.c` | Main entry point, ROM loading, save management |
| `blastdbg.c` | Headless debugger entry point (no SDL) |
| `genesis.c` | Genesis/Mega Drive system emulation |
| `vdp.c` | Video Display Processor emulation |
| `debug.c` | Interactive 68K/Z80 command-line debugger |
| `m68k_core.c` | Motorola 68000 CPU core |
| `m68k_core_x86.c` | 68K → x86_64 JIT recompiler |
| `z80_to_x86.c` | Z80 → x86_64 JIT recompiler |
| `ym2612.c` | YM2612 FM synthesis chip emulation |
| `psg.c` | SN76489 PSG sound emulation |
| `io.c` | Controller / I/O port emulation |
| `render_sdl.c` | SDL2 rendering backend |
| `render_headless.c` | Headless stub backend (for blastdbg) |
| `render_audio.c` | Audio mixing and output |
| `romdb.c` | ROM database and identification |
| `serialize.c` | Save state serialization |

### Object groups (Makefile)

- `MAINOBJS` — full BlastEm binary
- `DBGOBJS` — headless debugger (replaces render_sdl + render_audio + bindings with render_headless)
- `M68KOBJS` — 68K CPU (68kinst.o, m68k_core.o, m68k_core_x86.o)
- `Z80OBJS` — Z80 CPU (z80inst.o, z80_to_x86.o)
- `TRANSOBJS` — JIT backend (gen.o, backend.o, mem.o, arena.o, tern.o, gen_x86.o, backend_x86.o)
- `AUDIOOBJS` — sound emulation + render_audio
- `CONFIGOBJS` — config, tern, util, paths
- `RENDEROBJS` — render_sdl + controller_info + ppm + zlib/png

### Headless mode

The `headless` global (declared in `blastem.h`) controls headless operation:
- VDP allocates its own framebuffer instead of requesting one from the renderer
- `render_init()` is skipped
- Frame display calls are no-ops

`blastdbg` sets `headless=1` and links against `render_headless.c` (compiled with `-DIS_LIB` to avoid SDL includes from `render.h`).

## Debugger

The built-in debugger (`debug.c`) is GDB-inspired. Launch with `-d` flag (or `blastdbg` which always enters the debugger). Key commands: `b` (breakpoint), `n` (step over), `s` (step into), `c` (continue), `p` (print), `bt` (backtrace), `vr` (VDP registers), `vs` (sprites), `yc` (YM2612 channels).

The `-t` flag (`force_no_terminal()`) prevents BlastEm from spawning an xterm when stdin is not a TTY. This is essential for piped/scripted debugger use with the full `blastem` binary. `blastdbg` handles this automatically.

## Code style

- C gnu99, tabs for indentation
- `-Werror=return-type -Werror=implicit-function-declaration -Werror=pointer-arith`
- No C++ anywhere
- Global state is common (e.g., `config`, `current_system`, `headless`)
- Hardware components communicate through the `genesis_context` struct
- JIT code generation uses `code_info` structs and helper functions in `gen_x86.c`

## Testing

No formal test suite. Test ROMs and manual verification are used. The `test.c` and `test_int_timing.c` files are minimal headless test harnesses. The `dis` and `zdis` tools can be used to verify disassembly correctness.

## License

GPLv3+
