/*
 * The shared library: what more than one component needs and no component
 * owns.  This header only documents it and includes nothing; a component
 * includes the header it uses (`common/buffer.h`, `port/file.h`, ...).
 *
 * `common/` (headers in `include/common/`, files in `common/`) is portable C:
 *
 *   failure         fail.h          vjob_fail, vout_of_memory, VBUG: the one way out of a job or the process
 *   memory          region.h        bump regions over memory the caller reserved (vregion; varena is a view of it)
 *   buffers         buffer.h        vbuffer, the byte buffer of the tools (sticky failure, caller storage, a file)
 *                   text-buffer.h   the emitter's fixed reservation (separate: its appends have no bounds
 *                                   check per byte and take no flag)
 *   arrays          vector.h        growable arrays over three lvalues, on the heap or in a region
 *   tables          string-index.h  string to index over the caller's entries
 *                   hash.h          the string hash of those tables (no output byte depends on it)
 *                   intern.h        the identifier table, shared by all threads (intern.c)
 *   bytes and text  bytes.h         unaligned, byte-order and alignment helpers
 *                   leb128.h        LEB128 (DWARF, wasm)
 *                   format.h        decimal and hex digits without stdio
 *   numbers         softfloat.h     binary128 and x87-80 on the host, for constants folded at the target's precision
 *                   bigint.h        fixed-width multi-limb integers
 *                   checked-arithmetic.h  overflow-checked bounds for sizes that come from input files
 *   digests         digest.h        XXH3-64, BLAKE3, SHA-256 (digest-*.c)
 *   scopes          scoped-cleanup.h  VAUTOFREE, VAUTOCLOSE, VLOCK through `cleanup`
 *   attributes      attributes.h    VUSE, VNONNULL for our own declarations
 *
 * `port/` is what depends on the host: job-regions.h (reservations kept from
 * one job to the next), file.h (mapping, reading, writing, path joins),
 * clock.h, performance-counters.h, stack-guard.h, crc32c.h, x86native.h,
 * task.h (the thread pool and CPU budget), darwin/ and wasm/.  `common/` is
 * meant to include nothing from `port/`; bench/quality.py counts the includes
 * that still do (intern.c and intern.h for the CRC and the job regions,
 * text-buffer.h for a job region).
 *
 * Invariants a newcomer must know:
 * - The library fails in one way (fail.h).  A reservation or a table that
 *   is full ends the job; no function here returns NULL for "out of memory".
 *   The buffers fail by a sticky flag where a scope holds a VAUTO* or VLOCK
 *   resource (scoped-cleanup.h: cleanups do not run across a longjmp, and the
 *   job escape is one).
 * - Reservations are address space (MAP_NORESERVE), never freed; the regions
 *   of a job come back zero unless handed back as VJOB_REGION_SCRATCH.
 * - A string given to vintern* must be readable 16 (or 8) bytes past its
 *   length and, except through vintern_copy, must live for ever.  Ids are
 *   below 1 << VINTERN_ID_BITS; ids 1..n are the names the language registered
 *   with vintern_preset (for C, frontend/c/keywords.h).
 * - Every tool links all of `common_SRC` and `port_SRC` (src/Makefile).
 * - A header-only facility is `static inline`; a name is `v` + facility
 *   (doc/STYLE.md), macros are `V*`.
 *
 * The unit tests are src/test/common/ (bench/common-test.py).
 */
#ifndef VCC_COMMON_COMMON_H
#define VCC_COMMON_COMMON_H
#endif
