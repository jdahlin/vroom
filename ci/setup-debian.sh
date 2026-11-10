#!/bin/sh
# Tools for the premerge jobs in a Debian 13 arm64 container (GitHub's
# ubuntu-24.04-arm runner with `container: debian:trixie`).  The same clang
# major as Homebrew's on the Mac and env/linux/Dockerfile, without what only
# the kernel build and phase 1 need (libclang, bison, flex, libelf ...).
#
#   ci/setup-debian.sh          clang, lld, llvm tools, qemu, python3, ccache
#   ci/setup-debian.sh wasm     ... and emsdk (emcc + node) in $EMSDK
set -eu
LLVM=${LLVM_MAJOR:-23}
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends ca-certificates curl gnupg git
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg
echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/trixie/ llvm-toolchain-trixie-$LLVM main" \
	> /etc/apt/sources.list.d/llvm.list
apt-get update -q
apt-get install -y -q --no-install-recommends \
	clang-$LLVM lld-$LLVM llvm-$LLVM libclang-rt-$LLVM-dev \
	make python3 ccache file procps qemu-system-arm ipxe-qemu xz-utils
# the harnesses find the tools through $LLVM (bench/hostenv.py) and PATH
if [ -n "${GITHUB_ENV:-}" ]; then
	echo "LLVM=/usr/lib/llvm-$LLVM" >> "$GITHUB_ENV"
	echo "/usr/lib/llvm-$LLVM/bin" >> "$GITHUB_PATH"
fi

if [ "${1:-}" = wasm ]; then
	EMSDK=${EMSDK:-/opt/emsdk}
	[ -d "$EMSDK" ] || git clone -q --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK"
	"$EMSDK/emsdk" install ${EMSDK_VERSION:-latest}
	"$EMSDK/emsdk" activate ${EMSDK_VERSION:-latest}
	if [ -n "${GITHUB_ENV:-}" ]; then
		# emcc and node on PATH (emsdk_env.sh's exports, for the later steps).
		# From sh the script cannot find its own directory: source it from there.
		cd "$EMSDK"
		set +u
		. ./emsdk_env.sh >/dev/null
		set -u
		cd - >/dev/null
		echo "EMSDK=$EMSDK" >> "$GITHUB_ENV"
		# emsdk 7 sets no EM_CONFIG (the .emscripten next to emsdk is the default)
		[ -z "${EM_CONFIG:-}" ] || echo "EM_CONFIG=$EM_CONFIG" >> "$GITHUB_ENV"
		[ -z "${EMSDK_NODE:-}" ] || echo "EMSDK_NODE=$EMSDK_NODE" >> "$GITHUB_ENV"
		echo "$PATH" | tr : '\n' | grep "^$EMSDK" >> "$GITHUB_PATH"
	fi
fi
