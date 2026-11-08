#!/bin/sh
# Run a command in the Debian arm64 container (Docker Desktop: Apple
# Virtualization.framework, native arm64, no emulation).
#
#   env/linux/run.sh make -C ccs -j 8 vcc
#   env/linux/run.sh bench/vcc-ctests.py -j 6
#   env/linux/run.sh bench/vcc-diff.py cmp -j 6 --kernel
#
# The repo is bind-mounted at /vroom; that is Docker Desktop's host share
# (virtiofs), slow for many small files, so only sources are read from it.
# build/linux (vcc objects, binaries, ctests scratch; see ccs/Makefile and
# bench/hostenv.py) is the ext4 volume vroom-build, and /tmp is tmpfs.  The kernel trees live in the Docker
# volume vroom-work (ext4 in the VM: Docker Desktop cannot share the
# work.sparsebundle mount), at the Mac's absolute work/ path because kbuild's
# .cmd files name it.  Seed it once with  env/linux/run.sh --seed-work, then
# configure and build the Linux tree (work/build-linux, host tools for glibc):
#   env/linux/run.sh python3 bench/kbuild.py --dotconfig env/vroom.config \
#       --out /vroom/work/build-linux -j 8
#   env/linux/run.sh python3 bench/vcc-diff.py ref -j 6     # -> work/ppref-linux
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
W=${VROOM_WORK:-$(git -C "$R" rev-parse --path-format=absolute --git-common-dir | sed 's,/\.git$,,')/work}
IMG=vroom-linux
docker image inspect $IMG >/dev/null 2>&1 || docker build -t $IMG "$R/env/linux"
if [ "$1" = --seed-work ]; then
	docker volume create vroom-work >/dev/null
	COPYFILE_DISABLE=1 tar -C "$W" --no-xattrs --exclude=linux/.git -cf - linux build-o0 ppref |
		docker run --rm -i -v vroom-work:"$W" $IMG tar -xf - -C "$W"
	exit
fi
# work/ in the container: the volume at $W.  In the main checkout work/ is
# the sparsebundle's mount point (an empty dir to Docker), so mount over it;
# in a worktree, work is a symlink to $W, which resolves on both sides.
if [ "$(cd "$R" && pwd -P)/work" = "$W" ]; then
	WMNT="-v vroom-work:/vroom/work"
else
	[ -e "$R/work" ] || ln -s "$W" "$R/work"
	WMNT=
fi
exec docker run --rm -i $([ -t 0 ] && echo -t) -v "$R":/vroom \
	-v vroom-work:"$W" $WMNT -v vroom-build:/vroom/build/linux \
	--tmpfs /tmp:exec,size=8g $IMG "$@"
