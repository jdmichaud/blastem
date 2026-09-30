#!/bin/bash
set -euo pipefail

SKILL_DIR="${HOME}/.claude/skills/debug-megadrive"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Building blastdbg-static and dis-static..."
make -C "$SCRIPT_DIR" blastdbg-static dis-static

echo "Installing to ${SKILL_DIR}..."
mkdir -p "$SKILL_DIR"

# Copy to a temporary name and rename over the target: a plain cp fails with
# "Text file busy" while an agent is running the installed binary, and rename
# lets running processes keep the old file.
install_file() {
	local src="$1" dst="$2" strip_it="${3:-}"
	cp "$src" "$dst.new.$$"
	if [ -n "$strip_it" ]; then
		strip "$dst.new.$$"
	fi
	mv -f "$dst.new.$$" "$dst"
}

# binaries first, so the docs never describe a newer binary than the one installed
install_file "$SCRIPT_DIR/blastdbg-static" "$SKILL_DIR/blastdbg" strip
install_file "$SCRIPT_DIR/dis-static" "$SKILL_DIR/dis" strip
# blastdbg looks for its config and ROM database next to the executable
for f in default.cfg systems.cfg rom.db; do
	install_file "$SCRIPT_DIR/$f" "$SKILL_DIR/$f"
done
for f in bdtrace.py TRACE_FORMAT.md SKILL.md; do
	install_file "$SCRIPT_DIR/debug-megadrive/$f" "$SKILL_DIR/$f"
done

echo "Installed:"
ls -lh "$SKILL_DIR/"
echo ""
echo "Skill ready. Use /debug-megadrive <rom-file> in Claude Code."
