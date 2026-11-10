#!/bin/sh
# fetch-testsuites.sh: download the external C compiler test suites into
# work/testsuites/NAME (git-ignored; too large to vendor like gcc-torture).
#
#   bench/fetch-testsuites.sh [NAME...]    # default: all
#
# Each is a shallow (sparse where large) clone at the commit pinned below;
# REV=HEAD fetches the upstream tip instead (then update the pins).
# work/testsuites/MANIFEST lists name, url, commit and file counts.
# Nothing downloaded is built or run by this script.
set -eu
cd "$(dirname "$0")/.."
DST=work/testsuites
mkdir -p "$DST"

# name | url | pinned commit | sparse paths (empty: whole repo)
SUITES='
llvm|https://github.com/llvm/llvm-project.git|bb04dac04a0f9e9b99ffc2e28b4806d4f98e7966|clang/test/C clang/test/Sema clang/test/Preprocessor clang/test/Lexer clang/test/Parser clang/utils/ABITest clang/LICENSE.TXT
gcc|https://github.com/gcc-mirror/gcc.git|917da9c6e019407c9d469455ad4c50c062470d6c|gcc/testsuite/gcc.c-torture gcc/testsuite/gcc.dg gcc/testsuite/c-c++-common gcc/testsuite/gcc.target/aarch64 gcc/testsuite/lib COPYING3
c-testsuite|https://github.com/c-testsuite/c-testsuite.git|5c7275656d751de0e68b2d340a95b5681858ed07|
llvm-test-suite|https://github.com/llvm/llvm-test-suite.git|2ecf20c22c4c25051026fa450e0b0beb7d922209|SingleSource LICENSE.TXT
csmith|https://github.com/csmith-project/csmith.git|0cdc710315cfee9035e22ef4363ca479270d1934|
yarpgen|https://github.com/intel/yarpgen.git|1adb290f453505838f3aa33dc571f292b0c1810f|
libc-test|https://repo.or.cz/libc-test.git|63e36e934e8f2bedf645303a90b64098e2af6c8f|
chibicc|https://github.com/rui314/chibicc.git|90d1f7f199cc55b13c7fdb5839d1409806633fdb|
cproc|https://github.com/michaelforney/cproc.git|d1c53ddf56571573a7025324c8dd5c6d547a4d1f|
'

pin() {  # the pinned commit, or HEAD
  case "${REV:-}" in HEAD) echo HEAD; return;; esac
  echo "$1"
}

fetch() {  # name url rev paths
  name=$1 url=$2 rev=$3 paths=$4 d="$DST/$1"
  if [ ! -d "$d/.git" ]; then
    rm -rf "$d"
    if [ -n "$paths" ]; then
      git clone -q --filter=blob:none --no-checkout --depth 1 "$url" "$d"
      git -C "$d" sparse-checkout set --no-cone $(for p in $paths; do printf '/%s ' "$p"; done)
    else
      git clone -q --no-checkout --depth 1 "$url" "$d"
    fi
  fi
  if [ "$rev" != HEAD ]; then
    git -C "$d" fetch -q --depth 1 origin "$rev"
    git -C "$d" checkout -q --detach "$rev"
  else
    git -C "$d" checkout -q --detach origin/HEAD 2>/dev/null || git -C "$d" checkout -q
  fi
  c=$(git -C "$d" rev-parse HEAD)
  date=$(git -C "$d" log -1 --format=%cs)
  n=$(find "$d" -path "$d/.git" -prune -o -type f -name '*.c' -print | wc -l | tr -d ' ')
  echo "$name $url $c $date c-files=$n"
}

want=" $* "
echo "$SUITES" | while IFS='|' read -r name url p paths; do
  [ -n "$name" ] || continue
  [ "$want" = "  " ] || case "$want" in *" $name "*) ;; *) continue;; esac
  fetch "$name" "$url" "$(pin "$p")" "$paths"
done | tee "$DST/MANIFEST.new"
# keep the lines of suites not fetched this time
[ -f "$DST/MANIFEST" ] && awk 'NR==FNR{s[$1]=1;next} !($1 in s)' "$DST/MANIFEST.new" "$DST/MANIFEST" >>"$DST/MANIFEST.new" || true
sort -o "$DST/MANIFEST" "$DST/MANIFEST.new"; rm "$DST/MANIFEST.new"
