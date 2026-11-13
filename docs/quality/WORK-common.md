# Work order: the shared library (pilot)

The first module pass of `PLAN-quality.md`.  It is written to be carried out
by a cheaper model and scripts; the evidence behind every line is
`docs/review/quality-lib.md` (finding ids `Q-lib-NN`).  Names follow
`docs/STYLE.md`.

**Starts when** PR #134 (tree reorganisation: `lib/` becomes `src/common/`
and `src/port/`, headers under `src/include/`) is on main.  Paths below are
the reorganised ones.

**Owns** every file under `src/common/`, `src/port/`, `src/include/common/`,
`src/include/port/`, and the `common`/`port` lists in `src/Makefile`.  It
edits other modules only by script, in step 3, and only to follow a rename.
Replacing a module's private vector, table or buffer with the shared one is
**not** this order's work: it belongs to that module's own pass, which then
owns those files.

## What done looks like

- One job per file, each with an opening comment (drafts below).
- Every name follows STYLE.md; old names are gone, with no aliases.
- The library fails in one way (`vjob_fail`, `VBUG`).
- The eleven facilities of the review exist, each with a unit test.
- `port/` holds every use of mmap, pthread, clocks, counters and CPU
  probes; `common/` includes nothing from `port/`.
- `common/` and `port/` are two source lists in the Makefile that every
  tool links, instead of files listed under `LEX_SRC`, `PP_SRC`, `LD_SRC`.

## Steps

Each step is one PR.  Proof for every step unless it says otherwise:
`make -C src -j 6 vcc` both unity and `VCC_UNITY=`, `bench/vcc-same.py
--base` byte-identical against a base built before the step,
`bench/vbench.py gate` within 1 %, `env/linux/run.sh ci/premerge.sh arm64`.

### 1. Split (moves only)

| From | To | Why |
|---|---|---|
| `common/intern.c` lines 12-64 | `port/crc32c.c` | x86 CRC dispatch is host code (Q-lib-09) |
| `common/intern.c` lines 336-361 | `port/stack-guard.c` | `vstack_init` |
| `common/intern.c` lines 132-160, `intern.h` 153-157 | `common/fail.c`, `include/common/fail.h` | the job escape |
| `intern.h` lines 17-44 (C keyword list) | `include/frontend/c/keywords.h`, registered with the table at start-up | a second language must not edit the library |
| `arena.h`: `vmreg`, `vmpool`, `vreserve` | `include/port/job-regions.h`, debug table into `port/job-regions.c` | host code; the debug state is per-TU today (Q-lib-11) |
| `arena.h`: `vregion`, `varena` | `include/common/region.h` | portable |
| `defer.h`: `VAUTOUNMAP` | `include/port/file.h` | removes `common` including `port` (Q-lib-17) |
| `ldhash.c` | `common/digest-xxh3.c`, `digest-blake3.c`, `digest-sha256.c`, one `digest.h` | three algorithms in one file |
| `f128.h` lines 411-499 (`qbig`) | stays; see step 5 | |

The line numbers are today's `lib/` ones; find the same text after #134.

### 2. Rename inside the library (scripted)

Write `src/tools/rename-table.py`: it reads a table of `old new` pairs and
rewrites whole-word identifiers and `#include` paths across `src/`,
`bench/`, `ci/` and the live documents (not status history).  The table for
this step is checked in as `docs/quality/rename-common.tsv`.  Build it from
these rules, then have it reviewed against STYLE.md before applying:

| Old | New |
|---|---|
| files `buf.h`, `tbuf.h`, `vec.h`, `ckd.h`, `defer.h`, `attr.h`, `mapfile.h`, `vperf.h`, `vstack.h`, `hostcrc.h`, `f128.h`, `bigint.h` | `buffer.h`, `text-buffer.h`, `vector.h`, `checked-arithmetic.h`, `scoped-cleanup.h`, `attributes.h`, `file.h`, `performance-counters.h`, `stack-guard.h`, `crc32c.h`, `softfloat.h`, `bigint.h` |
| `vbuf_*`, `vsbuf_*`, `vzbuf_*`, `vfixbuf_*`, `vfdbuf_*` | `vbuffer_*` (merged in step 4; here only the names that survive) |
| `tb_*` | `vtext_*` (902 call sites: script only) |
| `vbuf_s`, `vsbuf_str`, `vfixbuf_puts`, `tb_s` | `..._string_append` |
| `q_*`, `qb_*`, `Q_P80` | `vsoftfloat_*`, `VSOFTFLOAT_X87_80` (202 call sites) |
| `ld_xxh3`, `ld_blake3`, `ld_sha256` | `vdigest_xxh3`, `vdigest_blake3`, `vdigest_sha256` |
| `vhash_obj`, `vhash_strtab` | `vhash_string` (one function, step 4) |
| `vckd_range/add/mul` | `vchecked_range/add/multiply` |
| `__crc32cd`, `__crc32cb` (our x86 definitions) | `vcrc32c_u64`, `vcrc32c_u8` on both hosts |
| `unlikely_vi`, `VIC_BITS` | `unlikely`, `VINTERN_CACHE_BITS` |
| `vmreg_*`, `vmpool_*` | `vjob_region_*`, `vjob_region_pool_*` |
| `vperf_*`, `vstack_*`, `vmap_file`, `vunmap_file` | `vperformance_*`, `vstack_guard_*`, `vfile_map`, `vfile_unmap` |
| include guards | `VCC_COMMON_NAME_H`, `VCC_PORT_NAME_H` |

Struct fields with one-letter or clipped names in these headers get words in
the same table.  `x86features.h` and `x86native.h` are `arch/x64/` and
`port/` code: `x86_*` becomes `x64_feature_*` and `vhost_x86_*`.

Proof adds: `git grep -w` for every old name returns nothing outside
`docs/` status sections.

### 3. One way to fail (changes behaviour on failure paths only)

`common/fail.h`: `vjob_fail(message)` leaves the job through the job's hook
and, without one, prints and exits; `VBUG(message)` for an internal
inconsistency; `vout_of_memory()`.  `obj/obj.c:19`'s `vjob_fail` and
`intern.c`'s `vjob_escape` become this one (their two fds are already set
together at `driver/ppdriver.c:1335,1959`).  Inside the library replace:
every `abort()` with or without a message, the `perror` paths, `vsbuf`'s
flag and `vfixbuf`'s silent truncation as the table in the review lists
them (Q-lib-01, Q-lib-18).  Fix on the way: `tb_f` running `n` past `cap`,
`vbuf_f` cutting at 1023 bytes, `vfdbuf_flush` dropping data on a write
error, unchecked `malloc` at `ldhash.c:272` and `arena.h:103`, unchecked
`calloc`/`realloc` in `f128.h` 583-636.

Rule to write into `scoped-cleanup.h`: a scope that holds a `VAUTO*` or
`VLOCK` resource must not call anything that can `vjob_fail`; the library's
buffer therefore fails by sticky flag, not by jump.

Proof adds: `bench/vcc-stress.py --vccd`, `bench/vccd-test.py`,
`bench/vlink-bad.py`, the ASan suite; a new stress case per failure path
that can be reached from input.

### 4. Merge the variants (identical output)

- **Buffers**: `vbuf`, `vsbuf`, `vzbuf`, `vfixbuf`, `vfdbuf` become one
  `vbuffer` with a sticky failure flag, optional caller storage and
  optional file descriptor (Q-lib-05).  `vtext` (the emitter's fixed
  reservation) stays separate and says why in its comment.
- **Hashes**: one `vhash_string`; first confirm no output byte depends on a
  table hash by building with a deliberately different mix and running
  `vcc-same.py`.
- **Regions**: `vregion_push` takes its failure from step 3 and gets its
  three re-implementers back as callers in their modules' passes; `varena`
  becomes a view of `vregion` only if `vbench.py gate` shows 0 % on the
  preprocessor (47 call sites), else it stays with a comment saying so.
- `arch/a64/codegen/fp.c:77-87` `ld_of_d` is `q_of_d`: call the library
  (Q-lib-15; one line outside the owned files, by exception).

### 5. Add the missing facilities

Each is a header (and a `.c` where needed) with an opening comment and a
unit test under `src/test/common/`, run by a new `bench/common-test.py`
(fast, checkout only, added to `ci/premerge.sh`).  Contracts are the
review's table "The proposed target"; names by STYLE.md:

| Header | Offers |
|---|---|
| `common/vector.h` | `VVECTOR_APPEND(v, n, cap, x)`, `VVECTOR_NEED(v, n, cap, k)` over three lvalues, doubling, `vout_of_memory` on failure; `..._IN(alloc)` for regions; `VVECTOR(T)` on top |
| `common/bytes.h` | `vbytes_load_u16/32/64`, `vbytes_store_u16/32/64`, `_be` forms, `valign_up`, `valign_skew` (defined for alignment 0) |
| `common/leb128.h` | `vleb128_unsigned_store`, `_signed_store`, padded forms, `_load`, `_length` |
| `common/string-index.h` | string to index over the caller's entries: `vstring_index_find`, `_add`, grows at half full |
| `common/format.h` | `vformat_unsigned`, `vformat_hex`, `vhex_digit_value` |
| `port/file.h` | `vfile_map_at(dirfd, path, &m)`, `vfile_read`, `vfile_write_all` (retries `EINTR`), `vpath_join` |
| `port/clock.h` | `vclock_seconds()` |

No caller outside the library changes in this step.  `qbig` in
`softfloat.h` moves onto `bigint.h`'s limbs only if `bench/vcc-ldconst.py`
and `bench/vcc-f80.sh` stay bit-identical; otherwise it is left and noted.

### 6. Comments, Makefile, gate

- Opening comment on every file; the module overview in
  `include/common/README` is not wanted: it is the opening comment of
  `include/common/common.h`, a header that only documents and includes
  nothing.  Its draft is section 2 of the review ("What the module is
  for"), corrected for what steps 1-5 changed.
- `src/Makefile`: `common_SRC` and `port_SRC`, linked by every tool.
- Fix the stale lines of Q-lib-21 and the `lib/` paragraph of AGENTS.md.
- `bench/quality.py`: bless the baseline; add `common/` and `port/` to the
  gated set (their own duplicates, sizes, comments) and the rule that
  `common/` includes nothing from `port/`.

## Not in this order

- Adoption of the new facilities by other modules (about 330 vector sites,
  11 tables, 18 FNV hashes, 16 LEB128 routines, 7 file mappings): each
  module's own work order lists its sites from the review.
- The lock-free identifier table and the linker's `gintern` as one
  concurrent table: later, measured.
- Per-thread scratch buffers onto job regions (Q-lib-19): with the
  scheduler plan.

## Report

Per step: the PR, the proof commands with their results, anything the order
got wrong.  The designer checks the result against STYLE.md and this
"done" list, not the diff line by line.
