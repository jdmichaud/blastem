#!/bin/bash
set -euo pipefail

SKILL_DIR="${HOME}/.claude/skills/debug-megadrive"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Building blastdbg-static and dis-static..."
make -C "$SCRIPT_DIR" blastdbg-static dis-static

echo "Installing to ${SKILL_DIR}..."
mkdir -p "$SKILL_DIR"
cp "$SCRIPT_DIR/debug-megadrive/SKILL.md" "$SKILL_DIR/"
cp "$SCRIPT_DIR/blastdbg-static" "$SKILL_DIR/blastdbg"
cp "$SCRIPT_DIR/dis-static" "$SKILL_DIR/dis"
strip "$SKILL_DIR/blastdbg" "$SKILL_DIR/dis"

echo "Installed:"
ls -lh "$SKILL_DIR/"
echo ""
echo "Skill ready. Use /debug-megadrive <rom-file> in Claude Code."
