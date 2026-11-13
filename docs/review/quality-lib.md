# Quality review: lib and its private variants, L2 (2026-11-13, main at 2e6d3e73)

## 1. Summary

`lib/` (19 files, 2.6 k lines without the generated tables) is small and mostly
well commented, but it is a shelf of things lifted out of single components,
not a library the tree is built on: `vec.h`, `mapfile.h`, `tbuf.h` and
`bigint.h` have one including file each, `attr.h` none outside `lib/`, and
`vregion_push` has no caller at all.  Outside `lib/` the same facilities are
written again: 7 vector macros and 6 copies of one `grow()` function, 10
private byte buffers, FNV-1a in 18 files, about 11 hand-written name tables,
16 LEB128 routines, 7 whole-file mappings and 11 write loops.

The three things that matter most:
1. **No out-of-memory policy.**  Growing an array has six behaviours in the
   tree (unchecked NULL at about 30 sites, silent abort, message and abort,
   message and `exit(1)`, a `false`/flag, a job-level escape), and "fail the
   job" itself exists twice (`vjob_escape` in `lib/intern.c`, `vjob_fail` in
   `obj/obj.c`).  The five buffers of `buf.h` and the dead `vregion_push`
   are consequences.  Decide this first (D1); the vector and the buffer follow.
2. **The vector and the name table are the big counts**: about 330 call
   sites of private vector code and 51 probe loops in 15 files.
3. **`lib/` mixes portable and host code inside files** (`arena.h`,
   `intern.c`, `defer.h`), so PR #134's `common/`/`port/` split puts
   `port/` definitions in `common/intern.c`.

## 2. Inventory

Users = files outside `lib/` and tests that include it.

| File | Lines | What it does | Users | Better name | Header comment |
|---|---|---|---|---|---|
| `arena.h` | 268 | `vmreg`/`vmpool` (regions reused between jobs, with a debug mode), `vreserve` (mmap or abort), `vregion` (bump by offset), `varena` (bump by pointer) | 7 | split: `vmreg.h` (host) and `region.h` | good for vmreg; says nothing of the other three |
| `buf.h` | 185 | five byte buffers: `vbuf`, `vsbuf`, `vzbuf`, `vfixbuf`, `vfdbuf` | 13 (`vsbuf`, `vzbuf` 1 each; `vfixbuf`+`vfdbuf` `cc/layout.c` only) | | good |
| `tbuf.h` | 149 | text buffer over a fixed reservation; decimal, hex and 8-byte-name formatters | 1 (`emit/emitimpl.h`; 902 calls in 23 files) | | good |
| `vec.h` | 24 | `VVEC(T)`, `VVEC_PUSH(a, x, alloc)`: doubling into an arena, old array left | 1 (`ld/ld.c`: 51 vectors, 86 pushes) | | good |
| `hash.h` | 48 | two word-at-a-time string hashes | 2 | | good, says they should be one |
| `mapfile.h` | 52 | `vmap_file`/`vunmap_file`: a file mapped read-only | 1 (`vccd/ar.c`, 3 calls) | | label |
| `ckd.h` | 28 | `vckd_range/add/mul` | 3 (9 calls, 7 in `wasmld.c`) | | good |
| `attr.h` | 21 | `VUSE`, `VNONNULL`, `VRETNONNULL` | 0 (only `arena.h`, `buf.h`, `mapfile.h`) | | good |
| `defer.h` | 87 | `cleanup` scopes: `VAUTOFREE`, `VAUTOCLOSE`, `VAUTOUNMAP`, `VLOCK`, `VTAKE` | 7 (54 uses) | | good, with rules |
| `vstack.h` | 22 | stack-depth guard, declaration only | 6 (19 calls) | | good |
| `hostcrc.h` | 17 | CRC32C: ACLE on arm64, a dispatch table on x86-64 | 1 + `intern.h` | | label |
| `intern.h` | 158 | identifier table API, the per-thread front cache, **the C keyword list**, `vjob_escape` | 12 | | good for the table; line 6 stale |
| `intern.c` | 361 | x86 CRC dispatch (12-64), the table (66-333), `vjob_escape` (132-160), `vstack_init` (336-361) | | four files' worth | none |
| `ldhash.h/.c` | 11+391 | XXH3-64, BLAKE3 (NEON), SHA-256 | 3 | `xxh3.c`, `blake3.c`, `sha256.c`, or `digest.c` | .c header omits SHA-256 |
| `f128.h` | 656 | binary128 and x87-80 soft float on `u128`; a decimal parser with its own bignum (`qbig`, 411-499) | 5 (202 calls in 19 files) | `softfloat.h` | good |
| `bigint.h` | 35 | fixed-width 32-bit-limb integer arithmetic (`vbi_*`) | 1 (`cc/bigint.c`) | | label; one line per function |
| `vperf.h` | 45 | instructions and cycles of this thread (XNU; zeros elsewhere) | 4 | | good |
| `x86features.h` | 44 | x86 feature sets and CPU names around a generated table | 2 | belongs to `arch/x64/` (PR #134 moves it) | stale: says "Generated", is hand-written |
| `x86native.h` | 86 | `-march=native`: CPUID to a CPU name and features | 1 | | good |

**What the module is for (draft opening comment).**  `lib/` holds what more
than one component needs and no component owns: memory (reservations,
regions, per-job regions), byte and text buffers, growable arrays, hashes,
the identifier table, soft float, checked arithmetic, scoped cleanup, and the
host probes (file mapping, counters, stack bottom, CRC).  It depends on no
other module.  Invariants a newcomer must know:
- Everything is header-only except `intern.c` and `ldhash.c`, and these have
  no Makefile variable of their own: `intern.c` is in `LEX_SRC`, `ldhash.c`
  in `PP_SRC` and `LD_SRC` (`ccs/Makefile:86,87,112`).  A tool that uses
  `vstack.h` or `hostcrc.h` on x86 must link `intern.c`.
- Reservations are address space (`MAP_NORESERVE`), never freed; running past
  one aborts or escapes the job, it never returns NULL.
- A `vmreg` comes back zero unless it was put as `VMREG_SCRATCH`.
- A string given to `vintern*` must be readable 16 (or 8) bytes past its
  length and, except through `vintern_copy`, must live for ever.
- Ids from `vintern` are below `1 << VINTERN_ID_BITS`; ids 1..`KW_COUNT` are
  the keywords in `intern.h`'s order.
- `defer.h` cleanups do not run across `longjmp`; the job escapes are longjmps.

## 3. Passes and data flow (L4)

`lib/` holds no pass.  Three repeated walks inside it: the identifier probe
loop is written three times (`intern.c:186-192`, `222-228`, `248-254`:
insert, lookup, find; the clone scan reports 80 tokens exact); `f128.h`
computes "limb i of d << k" three times (`443-447`, `463-466`, `474-477`)
and parses a decimal exponent twice (`538-546`, `573-582`); `vmreg_put`
zeroes what the job dirtied, which is the one per-job walk the library
causes (by design, measured in the header).

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-lib-01 | L9 | error policy | see "Out-of-memory" below; `lib/intern.c:141` `vjob_escape` and `obj/obj.c:19` `vjob_fail` are the same thing twice (message to the job's fd, leave the job, abort without one), with two fds set together at `driver/ppdriver.c:1335,1959` | every facility picks its own failure; five buffer types; about 30 unchecked `realloc` | one `common/fail.h`: `vjob_fail(msg)` (escape or abort) and `voom()`; every allocation helper below ends in one of them | M | medium: which failures may kill vccd (D1) | `vcc-stress.py --vccd`, `vccd-test.py`, `vlink-bad.py`, ASan suite |
| Q-lib-02 | L2 | pattern | 7 macros: `cc/replay.c:38-49` (about 34 uses), `ld/wasmld.c:33` (26), `arch/wasm/as/as.c:35` (8; `wasmld`'s verbatim), `emit/vir/build.c:56` (33, with `vec.inc`), `vir/parse.c:20` (30), `platform/darwin/ldmacho.c:90` (4), `vir/vir.c:770` (3).  6 copies of `grow(p, &cap, n, size)`: `arch/a64/emit/opt.c:452`, `emit/vir/a64.c:84`, `emit/rec/slots.c:6`, `arch/wasm/emit/slots.c:10`, `arch/x64/emit/vir.c:28`, `cc/replay.c:1089` (40 calls).  About 45 open-coded doublings; 95 `realloc` calls in 41 files | one idiom in 60 spellings; growth factors 2x, 1.5x+16, 1.5x+1024, (n+k)*2+32, and +1 per push | `common/vec.h`, see below | M | low | `vcc-same.py --base`, `vbench.py gate`, unity and file-by-file builds |
| Q-lib-03 | L2 | pattern | "string to index" tables, each with its own hash, probe and rehash: `obj/obj.c:90-128`, `ld/objcopy.c:205-262`, `platform/darwin/ldmacho.c:177-205`, `arch/wasm/as/as.c:90-115`, `ld/wasmld.c:368-396`, `vccd/dstore.c:108-135`, `vir/parse.c:42-80`, `vccd/fixdep.c:51`; 51 probe loops in 15 files outside `lib/` (`cc/replay.c` 13, `ld/ld.c` 9, `emit/vir/build.c` 6) | a table costs 40 lines and a rehash bug each time; `wasmld`'s never grows (65536 fixed chains, `:375`) | `common/idx.h`: an index table (slots hold id + 1, entries stay in the caller's array) | M | low to medium (hot in `obj`) | as Q-lib-02, plus `vas-obj.py`, `vobjcopy-cmp.sh`, `VCC_LDCMP=1` |
| Q-lib-04 | L2 | duplicate | FNV-1a in 18 files: 7 functions for 32-bit string hashes (`ld/wasmld.c:359`, `arch/wasm/as/as.c:81` (same text), `platform/darwin/ldmacho.c:170`, `vccd/fixdep.c:41`, `vccd/dstore.c:66`, `vir/parse.c:42`, `arch/a64/as/enc.c:40` case-folded), 10 inline 64-bit byte loops (`pp/diag.c:1759`, `driver/ppdriver.c:1797`, `driver/ccsnap.c:382`, `cc/replay.c:581`, `cc/token.c:354`, `pp/setup.c:1025`, `lex/lextest.c:237`, `as/vasm.c:64`, `as/as.c:261,304`), 3 word mixers (`cc/type.c:191`, `cc/replay.c:1366`, `pp/macro.c:10`).  `as/asimpl.h:171-183` `hsv` = `lib/hash.h:14-26` `vhash_obj` but for the last line | the offset basis has three spellings; 7 files carry `1469598103934665603`, FNV's constant with its last digit lost (harmless, and shows copies of copies) | `common/hash.h`: `vhash_str` (one word-at-a-time string hash), `vhash_bytes64` (fingerprints), `vhash_mix` | S | low; a fingerprint stored on disk changes value (`refhash`, ccsnap: caches rebuild) | `vcc-same.py`, gate (should gain) |
| Q-lib-05 | L2 | duplicate | byte buffers outside `lib/`: `ld/wasmld.c:73-94` `obuf` (= `vbuf_put`, 69 tokens exact; about 80 uses), `platform/darwin/ldmacho.c:670-687` `Buf` (exit on OOM; about 110 uses with `b16/b32/b64`), `driver/ppdriver.c:55-96` `buf` (about 75 uses with `bstr`), `as/as.c:3724-3734` `DBuf` (unchecked; 40 uses), `pp/ppimpl.h:973-986` `out_put` (= `tb_put`/`tb_c`), `cc/conv.c:815` `artb`, `vir/parse.c:199` `sput`, `pp/macro.c:196`, `vccd/dstore.c:143` `put`, `as/as.c:1174` `Buf8` | ten "pointer, length, capacity" types; four spellings of "append a C string" in `lib/` alone (`vbuf_s`, `vsbuf_str`, `vfixbuf_puts`, `tb_s`) | one `vbuf` (D1 decides its failure mode) and `tbuf`; see below | M | low | `vcc-same.py`, `vcc-wasm.py --objcmp`, `var-cmp.py`, `vsh-test.py` |
| Q-lib-06 | L2 | duplicate | LEB128, 15 functions and one inline loop: encoders `ld/wasmld.c:96,106`, `platform/wasm/wasm.c:19,29`, `arch/wasm/as/as.c:47,57`, `as/as.c:2775,2776`, `platform/linux/elf.c:200`; fixed-width `wasmld.c:1264,1272` = `wasm.c:52,60`; readers `wasmld.c:143,158`; `as/as.c:3044` `leb_len` | the wasm writer, assembler and linker each keep the format | `common/leb.h` on a byte pointer (`vleb_u(p, v)` returns the length), one line per buffer type on top | S | low | `vcc-wasm.py --objcmp`, `vas-obj.py`, `vcc-cfi.py`, `vcc-dwarf.py` |
| Q-lib-07 | L2 | duplicate | whole files: 7 read-only mappings beside `vmap_file` (`ld/ld.c:1724`, `ld/wasmld.c:417`, `ld/objcopy.c:171,1359`, `as/vasm.c:26`, `driver/ppdriver.c:277`, `driver/vpp.c:236`; the last four do not check `MAP_FAILED`), 3 read-whole (`platform/darwin/ldmacho.c:223`, `vir/virtest.c:25` = `vir/viralloc.c:33`), 11 write-until-done loops (`driver/ppdriver.c:356`, `vccd/vccd.c:341`, `vccd/rsh.c:454`, `vccd/ar.c:683,704,942`, `vccd/dstore.c:301`, `ld/ld.c:4147`, `ld/objcopy.c:512`, `platform/linux/elf.c:382`, `platform/darwin/ldmacho.c:1449`; six retry `EINTR`, five do not), 12 `"%s/%s"` joins of a job's cwd and a path | error handling differs per copy | `port/file.h`: `vmap_file_at(dirfd, path, &m)`, `vread_file`, `vwrite_all(fd, p, n)`, `vpath_at(cwd, path, buf, cap)` | S | low | `vlink-bad.py`, `vlink-opts.py`, `vccd-test.py`, `vobjcopy-cmp.sh` |
| Q-lib-08 | L2 | duplicate | unaligned and byte-order access: `rd32/wr32/wr64` at `ld/ld.c:70-72` = `platform/darwin/ldmacho.c:93-95` (about 83 calls with the `.inc` files), `lib/ldhash.c:12-13`; `put/p16/p32/p64` at `obj/objimpl.h:40` + `platform/linux/elf.c:264-266` and again `platform/darwin/macho.c:226,496-497`; `vccd/ar.c:313-315` `r16/r32/r64`; `be32` three times with three signatures (`vccd/ar.c:413`, `ld/wasmld.c:854`, `ldmacho.c:96`); `arch/a64/obj/fixup.c:17` `get32`.  Alignment: `alignup` at `ld/ld.c:73` and `ldmacho.c:98` (differ for a = 0), `cc/layout.c:7` `align_to`, `alignskew` at `ld/ld.c:75` and `ld/objcopy.c:73` (only the second guards a = 0), about 75 open-coded `(x + a - 1) & ~(a - 1)` | 20 one-line functions; the two `alignup` disagree on a = 0 | `common/bytes.h`: `vrd16/32/64`, `vwr16/32/64`, `..._be`, `valign_up`, `valign_skew` | S | low | `vcc-same.py`, `VCC_LDCMP=1` |
| Q-lib-09 | L7 | mixed file | `lib/intern.c`: CRC dispatch for `hostcrc.h` (12-64), the identifier table, `vjob_escape` and `vtables_full` (132-160, declared in `intern.h:153-157`), `vstack_init` (336-361).  `intern.h:17-44` is the C keyword list | the stack guard and the job escape cannot be found; PR #134 leaves `port/vstack.h` and `port/hostcrc.h` implemented in `common/intern.c` (checked on its branch); a C++ front end must edit the shared library to add a keyword | `port/hostcrc.c`, `port/vstack.c`, `common/fail.c`; keywords to the front end, registered at start | S | low | builds, `vcc-diff.py batch --cmp`, fuzz-lex |
| Q-lib-10 | L2 | near-duplicate | regions: `vregion_push` (`lib/arena.h:238`) has no caller; its three users re-implement it for another failure: `obj/objimpl.h:28` `rpush` and `as/asimpl.h:136` `rget` (67 calls; `vjob_fail`), `platform/darwin/macho.c:79` `zalloc` (error flag, NULL).  `vreserve` is wrapped four times as `reserve()` (`lib/intern.c:86`, `emit/tu.c:7`, `obj/objimpl.h:27`, `as/asimpl.h:135`), copied as `vm()` (`pp/setup.c:7-16`) and as `vmreg_map` (`lib/arena.h:60-69`).  Bump allocators: `ld/ld.c:198`, `lex/lex.c:1545`, `pp/file.c:10` `salloc` and `pp/dircache.c:53` `dalloc` (same atomic bump), `cc/tu.c:10` `halloc`, `lib/intern.c:259-278`, `vir/vir.c:745` | six "align, check, bump" bodies, each with its own message | `vregion_push` takes the failure from Q-lib-01; `varena` becomes a `vregion` view (pointer form kept only if the gate shows a cost); an atomic `vregion_push_shared` for `salloc`/`dalloc`/`copy_buf` | M | medium (pp's hot path) | gate, `vcc-diff.py batch --cmp`, TSan batch |
| Q-lib-11 | L5 | misnamed | `lib/arena.h` opens with 34 lines on `vmreg` only; `varena_alloc` reports "preprocessor arena full" (`:261`) whoever calls it; the debug table and its `atexit` are `static` in the header, so each including TU has its own (`:73-97`) | a reader looking for `vregion` finds a file about something else; `VCC_VMREG_STATS` prints one report per TU | split into `vmreg.h` and `region.h`; the debug state into a `.c` | S | low | builds |
| Q-lib-12 | L2 | pattern | timing: `now()` as seconds at `driver/ppdriver.c:117`, `driver/vpp.c:48`, `emit/tu.c:9`, `ld/ld.c:148`, `vir/viralloc.c:75`, `as/vasm.c:31`, with a clock argument at `lex/lextest.c:34`, in ms at `vccd/vccd.c:1071` | 8 copies | `port/clock.h`: `vnow_sec()`, next to `vperf_now` | S | none | builds |
| Q-lib-13 | L2 | pattern | sizes from input files: `ckd.h` is used by `ld/wasmld.c` (7), `ld/ld.c:1866`, `ld/objcopy.c:124`.  By hand with widening casts: `platform/darwin/ldmacho.c:383,396,405,422,426,565`; plain sums: `vccd/ar.c:239,337,372,730`, `ld/wasmld.c:934`, `ld/ld.c:2460,2470` | each hand check must be re-proved against wrap-around | `vckd_range` at those 13 sites | S | low | `vlink-bad.py`, `var-cmp.py` |
| Q-lib-14 | L2 | duplicate | small text helpers: decimal at `pp/ppimpl.h:831` `utoa`, `as/as.c:1779` `utoa` (other signature), `cc/exprop.c:899`, `lib/tbuf.h:62-85`; hex digit value at `lex/lex.c:664`, `vir/parse.c:194`, `arch/a64/emit/obj.c:381` (lower case only), `lib/f128.h:522-525`; `line_of` at `driver/ppdriver.c:157` = `pp/diagout.c:698` | 4 + 4 + 2 copies | `common/fmt.h`: `vfmt_u(buf, v)`, `vhexval(c)`; `tb_u` calls it | S | none | `vcc-same.py` |
| Q-lib-15 | L1 | duplicate | `arch/a64/emit/fp.c:77-87` `ld_of_d` = `lib/f128.h:236-257` `q_of_d` (77 tokens; `fp.c` includes `f128.h`) | a second double-to-binary128 | call `q_of_d` | S | none | `vcc-ldconst.py`, `vcc-same.py` |
| Q-lib-16 | L2 | near-duplicate | bignums: `lib/bigint.h` (`vbi_*`, fixed width) and `lib/f128.h:411-499` (`qbig`, growing), both little-endian 32-bit limbs, each with multiply, shift and compare; `f128.h` does not check `calloc`/`realloc` (`583,611,625,631,636`) | two limb libraries in one directory | `qbig` on `vbi_*` plus a length; not compared with `cc/bigint.c`, `cc/bigfloat.c` | M | medium (rounding) | `vcc-ldconst.py`, `vcc-f80.sh` |
| Q-lib-17 | L7 | misplaced | `defer.h:33` includes `mapfile.h` (and `pthread.h`, `unistd.h`) for all 7 users, for `VAUTOUNMAP`, which only `vccd/ar.c` can use | after PR #134 `common/defer.h` includes `port/mapfile.h` | `VAUTOUNMAP` moves to `mapfile.h` | S | none | builds |
| Q-lib-18 | L9 | error policy | `tbuf.h:142-146`: `tb_f` adds `vsnprintf`'s untruncated length, so a line longer than the room left puts `n` past `cap` (the next `tb_need` aborts).  `buf.h:59-66`: `vbuf_f` cuts at 1023 bytes without a sign.  `buf.h:163-164`: `vfdbuf_flush` drops the data on a write error.  `ldhash.c:272`, `arena.h:103`: unchecked `malloc` | silent truncation of diagnostics or `-S` text | clamp and fail through Q-lib-01; `vbuf_f` formats into the buffer | S | low | `vcc-diag.py`, ASan suite |
| Q-lib-19 | L8 | global state | grow-only per-thread scratch, about 20 of the 103 thread-locals: `lex/lex.c:1572`, `driver/ccsnap.c:251`, `driver/ppdriver.c:449,514,559,821,908`, `arch/a64/emit/opt.c:1736` (and the `W`, `XF`, `SL` tables its 17 `realloc` lines grow), `emit/tu.c:650,681`, `cc/record.c:4`, `ld/ld.c:3478`, `pp/text.c:8`, `emit/vir/a64.c`, `emit/vir/vec.inc` | never freed, sized by the largest job a thread ever ran; `vmreg` exists for exactly this and 64 call sites use it | scratch as `vmreg` regions of the job's set; see `quality-scheduling.md` for lifetime | M | medium | gate, `vccd-cc.py` |
| Q-lib-20 | L10 | dead | `attr.h` has no user outside three `lib/` headers; `ldhash.h:7` spells `warn_unused_result` out.  `vec.h`, `mapfile.h` have one user (AGENTS.md lists them as shared) | a convention nobody follows reads as a rule | adopt in `common/` headers as they are written, or drop | S | none | builds |
| Q-lib-21 | L6 | stale doc | `intern.h:6` "2^20 64-bit slots" against `intern.c:66` `SLOT_BITS 23`; `ldhash.c:1-8` omits SHA-256; `x86features.h:1` "Generated"; `vec.h:4-5` "From the linker"; AGENTS.md's `lib/` list lacks `bigint.h`, `hostcrc.h`, `vstack.h`, `ldhash.h`, `x86*.h` | | fix with the split | S | none | |

**Q-lib-02, vectors in detail.**  Two families.  (a) Heap, `realloc`: all the
macros and `grow` functions above; single-threaded; element type from
`sizeof *p`; they differ only in growth step and failure.  All can use one
facility as it is, written over three lvalues because nearly every array is
three existing struct fields: `VEC_NEED(v, n, cap, k)` and
`VEC_PUSH(v, n, cap, x)`.  (b) Arena, "allocate double, copy, leave the old":
`VVEC_PUSH` (86), cc's `vpush` (`cc/sym.c:535`, 136 calls), `as/as.c` at 10
sites (`1456, 2192, 2408, 2503, 2676, 2711, 2936, 3255, 3382, 3579`, each
five lines with `rget(&a->defs, ...)`), `pp/diag.c:1007,1441,1515`,
`pp/hcentry.c:590`, `pp/hcache.c:133`.  These need a small extension:
`VEC_PUSH_IN(v, n, cap, x, alloc)`, of which `VVEC_PUSH` becomes the
struct-shaped sugar.  Real reasons to stay private: cc's `vec` (16 inline
elements and `vfreeze` copying 16-byte blocks into `c->lists`,
`cc/ccimpl.h:70-74`, `cc/sym.c:547`), `emit/flow/maskflow.c:15-29` and
`emit/flow/graph.c:23-28` (return `false`, keep old pointers valid), the
SoA tables of `arch/a64/emit/opt.c:219-236` (13 arrays, one capacity: wants
`VEC_NEED` per array or one struct).  Growing by one element per push
(`ldmacho.c:469,479`, `ld/objcopy.c:1161,1342`, `ld/wasmld.c:1638`) is a
quadratic copy that the shared macro removes.

**Q-lib-03, tables in detail.**  Read in full: `obj`, `objcopy`, `ldmacho`,
wasm `as`: the same shape (`uint32_t` slots holding id + 1, entries with
their hash in the caller's array, double at half full), differing in hash
(`vhash_obj`, `vhash_strtab`, FNV, FNV) and allocator (region, `xcalloc`,
`xmalloc`, `calloc`).  All can use one new `idx` facility; `lib/` has
none today.  `wasmld`'s chained table can too.  Real reasons to be private:
`ld/ld.c:520-553` `gintern` and `lib/intern.c` (lock-free, CAS-published
`hash << 32 | id`; the same algorithm twice, worth one concurrent variant
later, L and risky); the `.req` table (`as/asimpl.h:190`,
`arch/a64/as/parse.c:6-20`: case-insensitive, newest first); the tables in
`arch/a64/as/enc.c` built once from static data.  `dstore`, `vir/parse`,
`fixdep` seen at their definitions only; `cc/replay.c`, `cc/type.c`,
`emit/vir/build.c`, `cc/bitcast.c` counted, not read.

**Out-of-memory, what each `lib/` facility does today.**

| Facility | On failure |
|---|---|
| `vreserve`, `vmreg_get`, `vmreg_put`, `tb_init` | `perror`, abort |
| `vmpool_take` | abort, no message |
| `vregion_push` | prints the caller's message, abort (no callers) |
| `varena_alloc` | "vcc: preprocessor arena full", abort |
| `tb_need` and every `tb_*` | "vcc: emitter buffer overflow", abort |
| `vbuf` | abort, no message |
| `vsbuf` | sets `oom`, later puts do nothing |
| `vzbuf` | returns `false` (`VUSE`) |
| `vfixbuf` | truncates silently at 4095 bytes |
| `vfdbuf` | never allocates; write errors ignored |
| `VVEC_PUSH` | none: the allocator must not return NULL |
| `vmap_file` | returns `false` |
| `vintern`, `vintern_copy` | `vjob_escape`: message to the job's fd, sets `vtables_full`, the job's hook (a longjmp), abort without one |
| `vintern_cstr` | abort when the string is 64 bytes or longer |
| `vintern_local` | `perror`, abort |
| `f128.h` `q_parsep`, `ld_blake3` | unchecked `calloc`/`realloc`/`malloc`: NULL dereference |

Outside `lib/`, of the 95 `realloc` sites: about 30 are unchecked (for
example `cc/replay.c:43` behind 34 pushes, `cc/replay.c:1094`,
`ld/objcopy.c:224,245`, `pp/macro.c:199,297,324`, `as/as.c:3730`,
`ld/ld.c:2494,2634`, `cc/stmt.c:960`, `cc/decl.c:126`); abort without a
message in `vir/`, `emit/vir/a64.c`, `emit/rec/`, `arch/wasm/`, `opt.c`;
message and abort in `cc/record.c:15`, `vccd/dstore.c:54`; message and
`exit(1)` in `ldmacho.c:91,675`; `false` or a flag in `emit/flow/`,
`vccd/rsh.c`, `vccd/ar.c`, `vccd/vccd.c:562`, `arch/wasm/emit/flow.c:31`; a
job-level failure in `emit/vir/build.c:61` (`vw_fail`) and `cc/bigint.c:25`
(`fatal`).

**Variants that should merge** (TODO.md "One set of data structures").
- Buffers: `vbuf`, `vsbuf`, `vzbuf` are one type with three failure modes;
  `vzbuf`'s NUL is `vbuf` plus one byte.  One `vbuf` with a sticky failure
  flag (`vsbuf`'s behaviour) covers all three: puts cannot fail, the owner
  checks once.  `vfixbuf` (54 uses, one file) is `vbuf` over a caller's
  array with the flag as truncation; `vfdbuf` is that plus a flush.  `tbuf`
  stays: fixed reservation, bound check only, 902 calls on the printer's hot
  path; `pp`'s `out_put` and `as`'s `Buf8` are the same idea.
- Arenas: `vregion` and `varena` are one bump allocator; `varena` has 47
  callers, all in `pp/`, `vregion` none through the library function.
- Hashes: `vhash_obj`, `vhash_strtab` and `as`'s `hsv` differ in the final
  mix only; no output depends on the value (inferred: symbol and string ids
  follow insertion order).  One function.

**PR #134: common/ or port/.**

| File | Side | Mixed? |
|---|---|---|
| `vec.h`, `hash.h`, `ckd.h`, `attr.h`, `bigint.h`, `f128.h`, `ldhash.c` | common | no |
| `buf.h` | common | `vfdbuf` writes to an fd; small |
| `tbuf.h` | common | includes `sys/mman.h`; only `tb_init`/`tb_reg` need a mapping |
| `arena.h` | both | yes: `vmreg`, `vmpool`, `vreserve` are host code (mmap, `mincore`, a 16 KB page constant, pthread); `vregion`, `varena` are portable.  PR #134 puts all of it in `common/` |
| `defer.h` | common | includes `port/mapfile.h` (Q-lib-17) |
| `intern.c/.h` | both | yes: Q-lib-09 |
| `mapfile.h`, `vperf.h`, `vstack.h`, `hostcrc.h`, `x86native.h` | port | no; two are implemented in `common/intern.c` |
| `x86features.h` | `arch/x64/` | no (PR #134 does this) |

**The proposed target: what `common/` and `port/` offer**, ordered by the
private copies each replaces (definitions; call sites in brackets).

| # | Facility | Contract | Replaces |
|---|---|---|---|
| 1 | `common/vec.h` | `VEC_PUSH(v, n, cap, x)`, `VEC_NEED(v, n, cap, k)`: doubling, never returns on failure; `..._IN(alloc)` for arenas; `VVEC(T)` on top | 7 macros, 6 `grow` functions, about 60 open-coded growths (about 330 sites) |
| 2 | `common/bytes.h` | `vrd16/32/64`, `vwr16/32/64` (+ `_be`), `valign_up(v, a)`, `valign_skew` | 20 functions (about 200 sites), about 75 open-coded alignments |
| 3 | `common/hash.h` | `vhash_str(s, n)`, `vhash_bytes64(p, n, seed)`, `vhash_mix(h, x)` | 20 FNV sites in 18 files, `hsv`, `vhash_strtab` |
| 4 | `common/leb.h` | `vleb_u/s(p, v)`, `vleb_u/s_pad(p, v, n)`, `vleb_rd_u/s(&p, end)`, `vleb_len` | 16 routines in 5 files |
| 5 | `port/file.h` | `vmap_file_at`, `vread_file`, `vwrite_all`, `vpath_at` | 7 mappings, 3 reads, 11 write loops, 12 joins |
| 6 | `common/idx.h` | string-to-index table over the caller's entries: `vidx_find`, `vidx_add`, grows at half full | 8 tables read or seen (up to 15 files) |
| 7 | `common/buf.h` | one `vbuf` (sticky failure, optional fixed storage, optional fd), `tbuf` beside it | 4 of the 5 `lib/` buffers, 6 private ones (about 350 sites) |
| 8 | `common/region.h`, `port/vmreg.h` | `vregion_push` by offset; shared atomic variant; `vreserve` | `rpush`, `rget`, `zalloc`, `halloc`, `salloc`, `dalloc`, 5 `reserve`/`vm` wrappers |
| 9 | `port/clock.h` | `vnow_sec()` | 8 |
| 10 | `common/fmt.h` | `vfmt_u`, `vfmt_hex`, `vhexval` | 9 |
| 11 | `common/fail.h` | `vjob_fail(msg)`, `voom()`: the one way out of a job or the process | `vjob_escape`, `vjob_fail`, `xmalloc`/`xcalloc`/`xrealloc`/`dalloc` (`ldmacho.c:81`, `objcopy.c:62`, `dstore.c:54`, `ccsnap.c:229`) |

Item 11 is decided first and written with item 1; items 2, 3, 4, 9 and 10
are mechanical and make good pilots (P4).

**Repeated static names** (`lists.repeated_static_names`, every name in three
or more files).  Shared helper written again: `now` x6 (Q-lib-12),
`write_file` x3 (Q-lib-07), `sleb` x3 (Q-lib-06), `hexval` x3 (Q-lib-14),
`gfind` x3 (Q-lib-03), `put` x3 (`vccd/dstore.c:143` is a buffer put, the
other two unrelated).  Same word, different job, not duplicates: `worker`
x4 (scheduling review), `err` x4 (diagnostics review), `statement`,
`directive`, `classify`, `binop`, `assign`, `apply`, `mk`, `lookup`,
`resolve`; `idch` x3 has three different character sets (`pp/ppimpl.h:1110`
a table, `arch/a64/emit/darwin.c:17` with `.` and `$`,
`arch/wasm/as/as.c:167` with `@` too).  These want the prefix rule of
STYLE.md, not a helper.  Among the pairs, helpers: `write_all`, `map_file`,
`slurp`, `uleb`, `pad_uleb`, `pad_sleb`, `utoa`, `strhash`, `hname`, `grow`,
`alignskew`, `line_of`, `bput`, `dalloc`, `die`, `error`; real second copies
for other reviews: `find_sep` (`virtest.c` = `viralloc.c`), `sec_begin` and
`rel_addend` (`wasmld.c` against `platform/wasm/wasm.c`; the two
`rel_addend` lists differ), `opt_sig` (`cc/replay.c:1358` against
`driver/ccsnap.c:379`: two signatures of the same options).

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `arena.h` | `lib/` | mostly `vmreg` | `vmreg.h` + `region.h` |
| `ld_xxh3`, `ld_blake3`, `ld_sha256`, `ldhash` | `lib/ldhash.*` | digests; `driver/ppdriver.c` uses them too | `vxxh3`, `vblake3`, `vsha256` |
| `q_*`, `qb_*`, `Q_P80` | `f128.h` | soft float, its bignum | `vf128_*` (202 uses: a scripted rename) |
| `tb_s`, `vbuf_s`, `vsbuf_str`, `vfixbuf_puts` | `tbuf.h`, `buf.h` | append a C string | one verb |
| `reserve()` | `intern.c:86`, `emit/tu.c:7`, `objimpl.h:27`, `asimpl.h:135` | `vreserve` with a message | call `vreserve` |
| `vm()` | `pp/setup.c:7` | `vreserve` again | `vreserve` |
| `alloc()` | `ld/ld.c:198` | the linker's per-thread arena | `ld_alloc` |
| `arena()` | `lex/lex.c:1545` | the lexer's per-thread bump allocator | `lx_alloc` |
| `unlikely_vi`, `VIC_BITS` | `intern.h:92,97` | a private `unlikely`; cache size | `unlikely`; `VINTERN_CACHE_BITS` |
| `__crc32cd`, `__crc32cb` | `hostcrc.h:10-11` | ACLE's reserved names defined by us on x86 | `vcrc32c_u64`, `vcrc32c_u8` on both hosts |
| `x86_*` | `x86features.h`, `x86native.h` | no `v` prefix, unlike the rest of `lib/` | settle in STYLE.md |

Prefix convention: `v` + facility (`vbuf_`, `vmreg_`, `vintern_`, `vckd_`,
`vperf_`), macros `V*`.  It breaks at `tb_` (tbuf), `q_`/`qb_` (f128),
`ld_` (ldhash), `x86_`, `KW_`, and the include guards (`VCC_X_H`, but
`VPERF_H`, `VCC_LIB_BIGINT_H`, `VCC_LIB_HOSTCRC_H`).

Words with two meanings: **arena** (the file about `vmreg`; `varena`;
`ElfObj.arena`, which is a `vregion`; the linker's block list; the lexer's
`arena()`); **buf** (ten struct types, Q-lib-05); **reserve** (map address
space in `vreserve`/`reserve()`; ensure capacity in `out_reserve`,
`st_reserve`); **put** (append bytes; store little-endian at a pointer in
`objimpl.h:40`; emit an instruction record in `a64impl.h:33`); **hash**
(`ldhash` = digests, `hash.h` = table hashes).

## 6. Decisions needed

- **D1. What may fail, and how.**  TODO.md says code inside vccd fails the
  request and never aborts; the tree aborts at about 90 sites.  Options: (a)
  abort everywhere with a message; (b) one `vjob_fail` that longjmps to the
  job and aborts without one, used by vectors, regions and tables; (c) error
  returns throughout.  Recommendation: (b) for limits and allocation in
  compile and link jobs (it is what `fatal`, `efatal`, `vw_fail`,
  `vjob_fail` and `vjob_escape` already do, five ways), a sticky flag for
  `vbuf` (no jump, so `defer.h` scopes stay legal), and (c) only in
  `emit/flow/` where it exists and is tested.  This needs a rule for code
  that holds `VAUTO*`/`VLOCK` resources when a vector push can jump.
- **D2. Vector shape**: macros over three lvalues (recommended: fits the 60
  existing arrays without renaming their fields) or everything becomes a
  `VVEC(T)` struct (cleaner, touches every use of `n` and `cap`).
- **D3. One string hash.**  Word-at-a-time multiply (`vhash_obj`), CRC32C
  (`vintern_hash`, needs the x86 dispatch) or XXH3.  Recommendation:
  `vhash_obj`'s; measure `obj` and the wasm tools with `vbench.py gate`.
  Confirm first that no output byte depends on a table hash (inferred).
- **D4. Table facility**: an index table over the caller's entries
  (recommended), a generic map with callbacks, or only the shared hash.
- **D5. Where the keyword list and the job escape live** (Q-lib-09): the
  front end and `common/fail.c`, or as today.  The C++ front end forces the
  first.
- **D6. `varena` into `vregion`**: one type by offset costs `pp` an add per
  allocation (47 call sites); measure before choosing.
- **D7. Is `common/` a module** with its own source list and a library the
  tools link, or files listed under `LEX_SRC`/`LD_SRC` as today.
- **D8. `attr.h`**: a rule for all public headers, or removed.

## 7. Not covered

- Read in full: every `lib/` file except the generated `x86native-data.inc`
  and `x86features-data.inc` (first lines only) and the licence text.
- The census outside `lib/` is by grep plus reading each definition and the
  lines around it, not whole files.  Counts of open-coded idioms (doublings
  45-64 depending on the pattern, alignments about 75, unchecked `realloc`
  about 30) are from patterns and can miss spellings.
- Tables: 6 read, 4 seen at their definition; `cc/replay.c`, `cc/type.c`,
  `cc/bitcast.c`, `emit/vir/build.c`, `emit/flow/values.c`, `pp/hcache.c`
  counted only.  `die`/`err`/`fatal` helpers and `worker` are left to
  `quality-diag.md` and `quality-scheduling.md`.
- `cc/bigint.c` and `cc/bigfloat.c` were not compared with `bigint.h` and
  `f128.h`'s `qbig`.
- Nothing was built or run except `bench/quality.py`; no speed claim here is
  measured.  PR #134 was checked on its branch for `intern.c`, `defer.h`,
  `tbuf.h` and the file list only.
- Whether `lextest.c:207`'s and `ppdriver.c`'s token hashes must agree with
  anything on disk was not checked.
