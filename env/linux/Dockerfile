# Debian 13 arm64 environment for building and testing vroom on Linux aarch64.
# Docker Desktop on Apple Silicon runs it natively (a lightweight VM, no
# emulation); any arm64 Linux host or qemu-system-aarch64 guest works too.
# clang comes from apt.llvm.org at the major version Homebrew has on the Mac
# (the reference outputs and generated tables are clang-version sensitive).
FROM debian:trixie
ARG LLVM=23
RUN apt-get update && apt-get install -y --no-install-recommends \
	ca-certificates curl gnupg \
	&& curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
	&& echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/trixie/ llvm-toolchain-trixie-$LLVM main" \
		> /etc/apt/sources.list.d/llvm.list \
	&& apt-get update && apt-get install -y --no-install-recommends \
	clang-$LLVM lld-$LLVM llvm-$LLVM llvm-$LLVM-dev libclang-$LLVM-dev libclang-cpp$LLVM-dev \
	make python3 git ccache file \
	bc bison flex libssl-dev libelf-dev cpio kmod rsync \
	qemu-system-arm ipxe-qemu \
	&& rm -rf /var/lib/apt/lists/*
ENV LLVM=/usr/lib/llvm-$LLVM
ENV PATH=$LLVM/bin:$PATH
WORKDIR /vroom
