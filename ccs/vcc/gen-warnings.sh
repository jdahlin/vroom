#!/bin/sh
# Genererar warnings.inc: varningsgrupper som vcc accepterar.
# Så länge backend är clang måste de vara en delmängd av clangs grupper.
set -e
DIAGTOOL=${DIAGTOOL:-/opt/homebrew/opt/llvm/bin/diagtool}
{
	echo "/* genererad av gen-warnings.sh från \`diagtool tree\`; redigera inte */"
	"$DIAGTOOL" tree 2>/dev/null | sed -E 's/^[ ]*-W//; s/ .*//' | grep -E '^[a-z0-9]' |
		LC_ALL=C sort -u | sed 's/.*/"&",/'
} > warnings.inc
