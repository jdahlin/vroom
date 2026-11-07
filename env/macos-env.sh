# source this: GNU-verktyg först i PATH för kärnbyggen på macOS
B=/opt/homebrew/opt
export PATH="$B/make/libexec/gnubin:$B/gnu-sed/libexec/gnubin:$B/findutils/libexec/gnubin:$B/coreutils/libexec/gnubin:$B/grep/libexec/gnubin:$B/bison/bin:$B/flex/bin:$B/llvm/bin:$B/lld/bin:$PATH"
VROOM_ENV="$(cd "$(dirname "${BASH_SOURCE[0]:-${(%):-%x}}")" && pwd)"
kmake() { make ARCH=arm64 LLVM=1 HOSTCFLAGS="-I$VROOM_ENV/host-include -include $VROOM_ENV/host-include/vroom-prelude.h" "$@"; }
export KBUILD_BUILD_TIMESTAMP="2026-11-01 00:00:00" KBUILD_BUILD_USER=vroom KBUILD_BUILD_HOST=vroom
