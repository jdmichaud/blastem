# Plan: Standalone Headless Mega Drive Debugger (blastdbg)

## Goal
Build `blastdbg` — a standalone, statically-linkable binary that provides BlastEm's 68K/Z80 debugger without any SDL, OpenGL, or audio dependencies. Distribute it alongside a Claude Code skill (`/debug-megadrive`) for LLM-driven ROM debugging.

## Status: DONE

### Completed
- [x] Read and documented full debugger command set from `debug.c` source
- [x] Discovered `-t` flag (undocumented) that prevents xterm spawn on piped stdin
- [x] Discovered `headless` global and how VDP/render handle it
- [x] Created `render_headless.c` — stub implementations of all render + audio + bindings APIs
- [x] Created `blastdbg.c` — minimal main() with ROM loading, save setup, debugger entry
- [x] Added `blastdbg` and `blastdbg-static` targets to Makefile
- [x] Built and tested both dynamic (604KB) and static (1.6MB stripped) binaries
- [x] Tested with Streets of Rage ROM: stepping, memory reads, VDP register dump all work
- [x] Created `debug-megadrive/SKILL.md` — Claude Code skill with full command reference
- [x] Skill uses `${CLAUDE_SKILL_DIR}/blastdbg` so binary can be co-located with skill

### To distribute
Copy `debug-megadrive/` to `~/.claude/skills/` and place `blastdbg` + `dis` inside it:
```
~/.claude/skills/debug-megadrive/
├── SKILL.md
├── blastdbg          (or blastdbg-static)
└── dis
```

## Files Created/Modified
- `render_headless.c` — NEW: headless stubs for render, audio, bindings
- `blastdbg.c` — NEW: minimal main() with ROM loading + debugger
- `Makefile` — MODIFIED: added blastdbg and blastdbg-static targets
- `debug-megadrive/SKILL.md` — NEW: Claude Code skill definition
