#!/bin/zsh
# Genererar env/vroom.config från allnoconfig + vroom-min.config + vroom-trim.config.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/env/macos-env.sh"
O="$ROOT/work/build-cfggen"
rm -rf "$O"
cd "$ROOT/work/linux"
kmake O="$O" RUSTC=/usr/bin/false allnoconfig >/dev/null
./scripts/kconfig/merge_config.sh -m -O "$O" "$O/.config" \
  "$ROOT/env/vroom-min.config" "$ROOT/env/vroom-trim.config" >/dev/null 2>&1
kmake O="$O" RUSTC=/usr/bin/false olddefconfig >/dev/null
cp "$O/.config" "$ROOT/env/vroom.config"
grep -c '=y' "$ROOT/env/vroom.config" | sed 's/^/inbyggda: /'
