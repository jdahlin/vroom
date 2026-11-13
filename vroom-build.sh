#!/bin/zsh
# Kallt kärnbygge från början till slut, inklusive serverstart.
#
#   ./vroom-build.sh [utkatalog]          (default work/build-cold)
#
# 1. stoppar ev. gammal vroomd och startar en ny (ingen state överlever)
# 2. tar bort utkatalogen, kopierar env/vroom.config som .config
# 3. bygger Image genom vroom-cc -> vroomd (zygote, delad PCH, -O0), länkar via vroom-ld, -j14
# 4. skriver total tid
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$ROOT/work/build-cold}"
SOCK=/tmp/vroomd.sock

TRY=()
[[ -n "${VROOM_TRYOPT:-}" ]] && TRY=(--try-opt "$VROOM_TRYOPT")
T0=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
pkill -f "$ROOT/build/src/vroomd" 2>/dev/null || true
rm -f "$SOCK"
"$ROOT/build/src/vroomd" --sock "$SOCK" --opt "${VROOM_OPT:--O0}" "${TRY[@]}" </dev/null >/tmp/vroomd.log 2>&1 &
VPID=$!
# vccd: tar emot vcc/vld, svarar på prober från kapabilitetstabellen och
# vidarebefordrar kompileringar till vroomd och länkningar till vroom-ld.
pkill -f "$ROOT/build/src/vccd" 2>/dev/null || true
export VCC_SOCK=/tmp/vccd.sock
rm -f "$VCC_SOCK"
"$ROOT/build/src/vccd" --sock "$VCC_SOCK" --backend-sock "$SOCK" --ld "$ROOT/contrib/phase1/vroom-ld" \
  </dev/null >/dev/null 2>/tmp/vccd.log &
CPID=$!
trap 'kill $VPID $CPID 2>/dev/null || true' EXIT
while [[ ! -S "$SOCK" || ! -S "$VCC_SOCK" ]]; do sleep 0.01; done

RC=0
python3 "$ROOT/bench/kbuild.py" --dotconfig "$ROOT/env/vroom.config" --out "$OUT" \
  --cc "$ROOT/build/src/vroom-cc" -j 14 -- CC="$ROOT/build/src/vcc" LD="$ROOT/build/src/vld" ${=VROOM_MAKEARGS:-SHELL=/bin/dash CONFIG_SHELL=/bin/dash} || RC=$?
T1=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
printf 'TOTAL (inkl. serverstart): %.1f s\n' $((T1 - T0))
exit $RC
