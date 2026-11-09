#!/bin/sh
# run.sh T.c [VPP]: build T.c with clang and with vcc (vpp -S), run both under qemu, diff the UART output
# VCC_O: vcc's -O level (default -O2; -O0 checks the unoptimised code generator)
set -e
CL=/opt/homebrew/opt/llvm/bin/clang
D=$(cd "$(dirname "$0")" && pwd)
VPP=${2:-$D/../../../../build/ccs/vpp}
T=$1; B=$(basename "$T" .c)
O=${TMPDIR:-/tmp}/vcc-emit-$B; mkdir -p "$O"
FL="--target=aarch64-linux-gnu -ffreestanding -fno-builtin -nostdinc -fno-PIE -fno-common -mgeneral-regs-only -fno-asynchronous-unwind-tables -fno-stack-protector"
$CL $FL -O1 -c "$D/rt.c" -o "$O/rt.o"
$CL $FL -c "$D/start.S" -o "$O/start.o"
$CL $FL -O2 -c "$T" -o "$O/ref.o"
$VPP $FL ${VCC_O:--O2} -S -o "$O/vcc.s" "$T"
$CL --target=aarch64-linux-gnu -c "$O/vcc.s" -o "$O/vcc.o"
for k in ref vcc; do
	ld.lld -T "$D/link.ld" "$O/start.o" "$O/rt.o" "$O/$k.o" -o "$O/$k.elf"
	timeout 20 qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -semihosting -kernel "$O/$k.elf" > "$O/$k.out" 2>&1 || true
done
if cmp -s "$O/ref.out" "$O/vcc.out" && grep -q '^done' "$O/vcc.out"; then echo "ok $B ($(wc -l < "$O/vcc.out") lines)"; else echo "FAIL $B"; diff "$O/ref.out" "$O/vcc.out" | head -20; exit 1; fi
