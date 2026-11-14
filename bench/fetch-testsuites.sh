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
#
#   bench/fetch-testsuites.sh llvm-mingw   # not part of the default set
#
# llvm-mingw is not a suite but the MinGW-w64 sysroot of doc/PLAN-win64.md
# (headers, UCRT CRT, import libraries, compiler-rt builtins): the pinned
# release tarball, checked against its SHA-256 and unpacked into
# work/llvm-mingw/ (work/ of the main checkout when run in a git worktree);
# nothing is done when it is already there.
set -eu
cd "$(dirname "$0")/.."

MINGW_REL=20261104
MINGW_NAME=llvm-mingw-$MINGW_REL-ucrt-ubuntu-22.04-x86_64
MINGW_URL=https://github.com/mstorsjo/llvm-mingw/releases/download/$MINGW_REL/$MINGW_NAME.tar.xz
MINGW_SHA256=919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926

if [ "$*" = llvm-mingw ]; then
  w=work
  if [ ! -d work ] && c=$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null); then
    [ -d "$(dirname "$c")/work" ] && w="$(dirname "$c")/work"
  fi
  d="$w/llvm-mingw"
  if [ -d "$d/$MINGW_NAME/x86_64-w64-mingw32/lib" ]; then
    echo "llvm-mingw $MINGW_REL: $d/$MINGW_NAME (present)"
    exit 0
  fi
  mkdir -p "$d"
  t="$d/$MINGW_NAME.tar.xz"
  if ! echo "$MINGW_SHA256  $t" | sha256sum -c --status 2>/dev/null; then
    curl -fsSL --retry 3 -o "$t.part" "$MINGW_URL"
    echo "$MINGW_SHA256  $t.part" | sha256sum -c --status || { echo "llvm-mingw: SHA-256 mismatch" >&2; rm -f "$t.part"; exit 1; }
    mv "$t.part" "$t"
  fi
  rm -rf "$d/$MINGW_NAME.tmp" && mkdir "$d/$MINGW_NAME.tmp"
  tar -xJf "$t" -C "$d/$MINGW_NAME.tmp"
  mv "$d/$MINGW_NAME.tmp/$MINGW_NAME" "$d/$MINGW_NAME" && rmdir "$d/$MINGW_NAME.tmp"
  echo "llvm-mingw $MINGW_REL: $d/$MINGW_NAME"
  exit 0
fi

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
kefir|https://git:public@git.protopopov.lv/kefir.git|7c59547068eaa0723b4db83d3fb70f448b0f0cb1|
TSVC_2|https://github.com/UoB-HPC/TSVC_2.git|911fc639b933df38a7728477ce4b2e68e9c8eeff|
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
done > "$DST/MANIFEST.new"
cat "$DST/MANIFEST.new"
# keep the lines of suites not fetched this time
cp "$DST/MANIFEST.new" "$DST/MANIFEST.all"
if [ -f "$DST/MANIFEST" ]; then
  awk 'NR==FNR{s[$1]=1;next} !($1 in s)' "$DST/MANIFEST.new" "$DST/MANIFEST" >>"$DST/MANIFEST.all"
fi
sort -o "$DST/MANIFEST" "$DST/MANIFEST.all"
rm "$DST/MANIFEST.new" "$DST/MANIFEST.all"
