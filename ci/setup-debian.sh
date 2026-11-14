#!/bin/sh
# Tools for the premerge jobs in a Debian 13 container, arm64 or x86-64
# (GitHub's ubuntu-24.04-arm or ubuntu-24.04 runner with `container:
# debian:trixie`).  The same clang
# major as Homebrew's on the Mac and env/linux/Dockerfile, without what only
# the kernel build and phase 1 need (libclang, bison, flex, libelf ...).
#
#   ci/setup-debian.sh          clang, lld, llvm tools, qemu, python3, ccache
#   ci/setup-debian.sh wasm     ... and emsdk (emcc + node) in $EMSDK
#   ci/setup-debian.sh win64    ... and Wine (Debian's) and the pinned llvm-mingw
#                               sysroot in work/llvm-mingw (x86-64 only)
#
# Under Actions it also sets the step output `toolfp`: a hash of the versions
# of every tool whose output the harnesses' caches (clang references, passed
# objects, ccache) depend on, for the cache keys in .github/workflows/.
set -eu
LLVM=${LLVM_MAJOR:-23}
export DEBIAN_FRONTEND=noninteractive
# a throwaway container: no fsync per unpacked file, no docs
echo force-unsafe-io > /etc/dpkg/dpkg.cfg.d/unsafe-io
printf 'path-exclude /usr/share/doc/*\npath-exclude /usr/share/man/*\npath-exclude /usr/share/locale/*\n' \
	> /etc/dpkg/dpkg.cfg.d/nodoc
apt-get update -q
apt-get install -y -q --no-install-recommends ca-certificates curl gnupg git
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg
echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/trixie/ llvm-toolchain-trixie-$LLVM main" \
	> /etc/apt/sources.list.d/llvm.list
apt-get update -q
apt-get install -y -q --no-install-recommends \
	clang-$LLVM lld-$LLVM llvm-$LLVM libclang-rt-$LLVM-dev \
	make python3 ccache file procps qemu-system-arm qemu-user ipxe-qemu xz-utils
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

if [ "${1:-}" = win64 ]; then
	# Debian 13's Wine (10.0; WineHQ's 11.0 runs on the Mac and the dev
	# hosts): its 64-bit loader is enough, the test programs are x86-64 only,
	# so no i386 architecture.  The harnesses take it from PATH (/usr/bin/wine).
	apt-get install -y -q --no-install-recommends wine wine64 xz-utils
	# the MinGW-w64 sysroot, SHA-256 checked; CI restores the tarball from its
	# cache (keyed on the pin) so that only the unpacking runs
	sh "$(dirname "$0")/../bench/fetch-testsuites.sh" llvm-mingw
fi

if [ -n "${GITHUB_OUTPUT:-}" ]; then
	B=/usr/lib/llvm-$LLVM/bin
	fp=$( {
		cat "$0"
		"$B/clang" --version; "$B/ld.lld" --version; qemu-system-aarch64 --version
		qemu-x86_64 --version
		if [ "${1:-}" = wasm ]; then emcc --version; node --version; fi
		if [ "${1:-}" = win64 ]; then wine --version; grep '^MINGW_' "$(dirname "$0")/../bench/fetch-testsuites.sh"; fi
	} 2>&1 | sha256sum | cut -c1-16)
	echo "toolfp=$fp" >> "$GITHUB_OUTPUT"
	echo "tool fingerprint $fp"
fi
