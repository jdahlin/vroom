# Quality review: driver, server, shim, shell; environment knobs (2026-11-13, main at 2e6d3e73)

## 1. Summary

The area works and is well tested from outside (vccd-test, vsh-test, vcc-driver), but it has
grown by accretion: the compile job is one 1066-line function, the server is one 2079-line file
with misleading section banners, and nothing is table-driven.  Three things matter most:

1. **There is no option table.**  The compiler's options live in a 333-line `strcmp` chain that
   silently accepts anything it does not know; vccd keeps a separate allow-list (`caps.c`), a
   deny-list sits in the driver, three more lists say which options take a value, and seven
   predicates in `vccd.c` re-scan argv with their own rules.  A C++ front end doubles all of it.
2. **58 environment knobs, no registry, and the server reads its own environment.**  14 are product
   options in disguise (the `-O2` optimizer is `VCC_VIR=bl`); under vccd they come from the
   environment of whichever client autostarted the server.  8 of 58 are in AGENTS.md.
3. **Per-job state is passed three ways** (`ppd_env`, thread-locals such as `ppd_umask`, and
   `getenv`), so "what does a job depend on" has no single answer; this is also where the bugs
   found by reading sit (protocol count overflow, `-target X`, an error path that skips cleanup).

Duplication by token scan is low (vccd 0.7 %); the duplication here is of rules, not of text.

## 2. Inventory

| File | Lines | What it actually does | Better name | Header comment |
|---|---|---|---|---|
| `driver/ppdriver.c` | 1968 | One compile job from a clang command line: option parsing, search path, predefines, `-E`, `-M*`, `-fsyntax-only`, `-S`, `-c` for C/.S/.s, object and depfile writing; also `--cmp` token diff with its hash cache, `--vcc-tokhash`, `--explain` | `driver/job.c` + the split in Q-drv-02 | stale: "one preprocessing job (-E)", "ELF object" (also Mach-O, wasm), lists 1 of its 5 knobs |
| `include/driver/ppdriver.h` | 42 | `ppd_env` (a job's I/O context), `ppd_run`, global phase counters written by `codegen/` | `job.h`; counters to `lib/` | label; `ppd_run`: "one -E job" (stale) |
| `bin/vpp/vpp.c` | 271 | The standalone compiler binary (all modes) plus `--batch` (N jobs on N threads, stats) and `--cmp-files` | see Decision 3 | stale: "the preprocessor alone ... for parity tests" |
| `driver/x86predefines.h` | 27 | x86 feature and CPU macros appended to the predefine text; needs `buf`/`bstr` defined before inclusion | `driver/predef_x86.h` | good |
| `server/vccd.c` | 2079 | Server: accept loop, socket queue, build generations and self-restart, request receive, probes, job classification, compile with captured diagnostics, delegate, forward to a backend, spawn, in-process ld/nm/objcopy, host programs, dispatch, recipe glue, counters | split in Q-drv-04 | good as a tour, stale in places (`VCC_PP`/`VCC_CC`, "shims (vcc, vld)") |
| `bin/vcc/shim.c` | 315 | The client: one request, one status; autostarts vccd; with `-DVSH` it is make's `SHELL` | `vccd/client.c` (the binary is the shim) | good; names 3 of the 7 tools |
| `utils/rsh.c` | 609 | kbuild's recipe subset of sh: tokenize, parse to a command list, run | `vccd/recipe.c` (prefix `rcp_`): "rsh" is the remote shell to every reader | good |
| `include/utils/rsh.h` | 43 | The grammar of the subset and the three callbacks | `recipe.h` | good: the one place the subset is written |
| `server/dstore.c`/`.h` | 384/25 | Diagnostics per TU across a build, dedup set, `--vccd-diagnostics` | fine | good |
| `server/caps.c`/`.h` | 264/32 | Answers to kbuild's probes: accepted frontend/c/ld options, probe sources, version strings | fine (`probe_` prefix, not `cc_`/`ld_`) | stale: "no stack protector, debug info"; "while the backend is still clang" |
| `utils/fixdep.c`/`.h` | 356/10 | kbuild's fixdep in process, CONFIG_ names cached per file in the token cache | fine | good; empty banner at :29 |
| `src/include/protocol.h` | 71 | Request header, statuses, build id, default socket path | `vccd/protocol.h` | stale: "between vroom-cc and vroomd" |

**What the module is for (draft opening comment).**  `driver/` turns one clang-style command line
into one job: it decides the target and mode, builds the include path and the predefine text,
runs frontend/pp/cc/emit/as, and writes each output through a temporary that is renamed when complete.  A
job touches no process state: paths are relative to `ppd_env.cwd`, output goes to the job's fds,
and the job's environment is `ppd_env.envp` (today violated by 8 `getenv` calls and by
`ppdriver.c:1406`).  `vccd/` is a single-user server: one request per connection (argv, env, cwd,
fds 0-2), answered with one int32.  Invariants: the peer has our uid (`vccd.c:1863`); a request
from a new process group is a new build and flushes the file caches when no job runs; a client
that has gone gets no output file; nothing a client sends may crash the server (see Q-drv-05).

## 3. Passes and data flow (L4)

One kernel `-c` request (about 150 arguments), server without a backend:

| Step | Where | Walks |
|---|---|---|
| receive, split strings | `vccd.c:358-439` | payload once |
| `VCC_UMASK` | `vccd.c:1355` | envp |
| `VCC_DELEGATE`, `VCC_DIAG_DEDUP`, `VCC_FIXDEP_D/T/O/CMD` | `vccd.c:1380, 888, 1097-1103` | envp, up to 6 more times (`req_env`) |
| `--explain`/`--fix` | `vccd.c:1456` | argv |
| is it a probe | `cc_probe_kind` `vccd.c:473` | argv, each option against `caps.c`'s lists (9 + 75 `strcmp`, 13 prefixes, a `bsearch`) |
| is it `-E` | `cc_is_pp` `vccd.c:724` | argv |
| is it a compile | `cc_is_compile` `vccd.c:794` + `cc_input` `:761` | argv twice |
| `--explain` again | `ppdriver.c:861` | argv |
| options | `ppdriver.c:968-1300` | argv, up to about 150 `strcmp` per argument |
| x86 switches on arm64 | `ppdriver.c:1305-1318` | argv; each `-m` option against 130 names (`include/arch/x64/x86features.h:13`) |
| `-W` groups | `ppdriver.c:1356-1388` | `wargs` twice, linear over 1196 names (the same test is a `bsearch` at `caps.c:121`) |
| fixdep | `vccd.c:1095` | reads back and parses the `.d` file the job wrote a moment ago |

Eight to ten passes over argv and seven over envp.  Cost is inferred to be small against a TU
(not measured); what it costs is seven rule sets for "which argument is the input and which
option eats the next one".  The one walk worth fusing for speed is the last: `write_deps`
(`ppdriver.c:445`) formats the dependency list from `pp_files`, writes it through a temporary and
a rename, then `vfixdep` opens, reads, parses and unlinks it (`fixdep.c:321, 333, 354`): six
system calls and a 40 KB round trip per TU that a call with the name list would avoid.

`ppd_run1` itself is a straight line: options, checks, input, search path, predefines, `pp_open`,
then one of five tails (tokhash, C via `cc_run`, `-E` text, `--cmp`, `.S` via `as_run`), each
ending in its own output and depfile calls (`write_deps` is called from four places).

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-drv-01 | L2 | pattern | `ppdriver.c:968-1300`; allow-list `caps.c:35-76`; deny-list `ppdriver.c:1088-1094`; value lists `caps.c:35`, `vccd.c:748`, `vccd.c:1218`, `ARG()` `ppdriver.c:970` | Five lists to keep in step; unknown options accepted silently; C++ adds a second chain | `driver/options.def` (one row per option: spelling, form, field, probe answer) parsed into `struct cmdline`; `caps.c` reads the same table | L | medium | `vcc-driver.py`, `vccd-test.py`, `vcc-diff.py cmp/dm/deps`, kernel build, `vcc-same.py` |
| Q-drv-02 | L4 | long function | `ppd_run1` `ppdriver.c:903-1968` | 1066 lines, about 110 locals; helpers carry `noinline, cold` to protect its code generation (`:640, 736, 755, 895`) | split below | M | medium (`vbench.py gate`: the function is layout sensitive) | `vcc-same.py`, `vbench.py gate`, `vcc-driver.py` |
| Q-drv-03 | L8 | global state | table in 4.1; `shim.c:138` | 58 knobs, 8 in AGENTS.md; server reads its own environment; product behaviour invisible to make | registry + three rules (Decision 1) | M | low | a premerge check: every `getenv` name is in the registry |
| Q-drv-04 | L5 | mixed file | `vccd.c`, map below | 12 jobs in one file; banners at `:108, 1069, 1213, 1616` label the wrong code | split below (moves only) | M | low | `vccd-test.py`, `vsh-test.py`, `vccd-kbuild.sh` + boot |
| Q-drv-05 | L9 | error policy | `vccd.c:405-406`, `:426`, `:424` | Counts are added in 32 bits: `argc = 0xffffffff, envc = n + 1` passes both checks, `r->argc` becomes -1 and `cc_is_pp` reads `argv[-2]` (`:727`).  The payload's last byte is not required to be NUL: `strlen` runs into the previous request's bytes.  `pad` is not checked (by reading, not run; `vccd-test.py:351` uses `1<<30` twice, which does not wrap) | 64-bit sum, `argc <= INT_MAX`, last byte NUL, `pad == 0`; a `protocol_check()` next to the struct | S | low | new cases in `vccd-test.py garbage_requests`, ASan build |
| Q-drv-06 | L9 | error policy | `ppdriver.c:1195-1204` | `-target TRIPLE` (two arguments) searches the string `"-target"` for the triple and never reads the next argument: the job compiles for aarch64 Linux (by reading, not run) | falls out of Q-drv-01; fix alone is 3 lines | S | low | `vcc-driver.py` case |
| Q-drv-07 | L9 | error policy | `ppdriver.c:1233-1237` | The one `return` inside the option loop: skips `out:` (the `bufpool` regions are never given back) and prints to the server's stderr, not `errfd` | `goto out` with `dprintf(errfd, ...)` | S | low | `vcc-driver.py` case under vccd |
| Q-drv-08 | L1 | near-duplicate | `cc_probe_kind` `vccd.c:473`, `cc_is_pp` `:724`, `cc_input` `:761`, `cc_is_compile` `:794`, `host_cc_link` `:830`, `dg_is_compile` `:855`, `host_cc` `:1249-1264` | Seven hand loops answer "mode, input, target, output" with different rules: suffix test at `:780` against `strstr(last, ".c")` at `:731`; `-imacros` in `takes_value` but not in `host_with_arg`, so its file becomes a link input in `host_cc` (`:1313`) | one `classify(argv) -> {mode, lang, input, out, target}` from Q-drv-01's table, used by vccd and by the driver | M | medium | `vccd-test.py`, kernel build counts in `print_counts` unchanged |
| Q-drv-09 | L1 | duplicate | list in 4.2 | 5 write-all loops, 4 read-whole-file, 4 clocks in 6 files of this area | `lib/` (file I/O header) | S | low | `vcc-same.py` |
| Q-drv-10 | L8 | global state | `ppd_umask`, `ppd_conn` `ppdriver.c:373-374` set at `vccd.c:1354, 1827`; `bufr` `:63`; `tl_as/tl_obj/tl_objbuf/tl_os` `:512-518`; `cap_fd` `vccd.c:884` | Job context split between `ppd_env` and thread-locals; the client's umask reaches only `write_file`: `fixdep.c:345` and `vccd.c:692` create 0644, `rsh.c:543` 0666 and `rsh.c:502` 0777 under the server's umask | fields of the job context; one `job_create_output()` used by driver, fixdep, probes and recipes | M | low | `vccd-test.py` umask case extended to `.cmd` and recipe outputs |
| Q-drv-11 | L4 | redundant walk | `ppdriver.c:1893`, `vccd.c:1095-1108`, `fixdep.c:321-354` | depfile written, renamed, read back, parsed, unlinked per TU | `vfixdep_names(names, n, ...)` called with `pp_files`' list when `VCC_FIXDEP_D` is set | M | medium (byte-identical `.cmd`) | `vsh-test.py`, kernel build: `.cmd` files identical |
| Q-drv-12 | L5 | mixed file | `--cmp` and its hash cache `ppdriver.c:125-319`; `--vcc-tokhash` `:1786-1824`; `VPP_TIME` `:1894, 1913` | Test and benchmark code inside the product job; the benchmark loop is why the function is layout sensitive | `driver/tokcmp.c`, linked into `vpp` only | S | low (`vbench.py` baseline shifts once) | `vcc-diff.py cmp`, `vbench.py gate` |
| Q-drv-13 | L7 | misplaced | `ppdriver.c:1352` includes `../vccd/warnings.inc`; `codegen/emitimpl.h:70` includes `include/driver/ppdriver.h`, `codegen/tu.c:773` redeclares `ppd_ns_emit` by hand; `frontend/c/ccimpl.h:35` includes `include/driver/ccsnap.h` | driver <-> vccd and frontend/c/emit -> driver cycles | `warnings.inc` and one `warning_group_known()` to `driver/` (two users: `caps.c:104`, `ppdriver.c:1356`); phase counters to `include/port/vperf.h` | S | low | builds, `quality.py`: mutual pairs 4 -> 3 |
| Q-drv-14 | L2 | pattern | `at()` `ppdriver.c:348`, `:1437`, `:1852-1855`; `fixdep.c:160-163, 291-294, 341-344`; `vccd.c:688-691, 1340`; `dstore.c:207-212` | "path against the client's cwd" written 9 times; forgotten at `ppdriver.c:1406-1407` (`stat`/`open` of `main_file` against the server's cwd).  ld, ar, objcopy and rsh use a `dirfd` and `*at()` calls instead | one rule: a job carries a `dirfd`; or one `job_path()` | M | low | `vccd-test.py` (relative device input) |
| Q-drv-15 | L6 | misnamed | `kind` strings: `vccd.c:1360` (5 `strcmp` decide fixdep), `:1625` `kind[0] == 'b'` guards an uninitialized `struct req`, `:1393` `r->tool[4] == 'n'`, `:1461` `argv[i][2] == 'e'`; 14 parallel counters `:102` | Request class is a string compared by letters; adding a class means four edits | `enum req_kind` + table {name, counter, wants fixdep} | S | low | `vccd-test.py`, log format unchanged |
| Q-drv-16 | L1 | near-duplicate | request assembly: `shim.c:207-233`, `forward_argv` `vccd.c:985-1016`, `sh_run_tool` `vccd.c:1689-1713`; SCM_RIGHTS send: `shim.c:238-249` = `vccd.c:964-973`; `req_env` `vccd.c:1079` = `ppd_getenv` `ppdriver.c:805` | Three writers of the wire format and one reader, none shared | `vroom_req_put()`/`vroom_req_send()` in `protocol.h` | S | low | `vccd-test.py` |
| Q-drv-17 | L1 | near-duplicate | `ppdriver.c:1673-1680` = `:1681-1689`; target as four ints (`darwin`, `x86`, `wasm`, `lnx`) with the same 5-way choice at `:1585-1591`, `:1607-1608`, `:1630`, `:1458`/`:1931` | A target is added in a dozen places | `driver/target.c`: one row per target (predefine text, `-dD` order, ELF machine, `char` default, wchar) | M | low | `vcc-diff.py dm`, `vcc-driver.py` |
| Q-drv-18 | L4 | long function | `handle1` `vccd.c:1367-1614` (248 lines, the backend link case inline at `:1485-1553`); `host_cc` `:1239-1348` (argv three times); `main` `:1878-2079` (kqueue and epoll loops sharing an inner loop across `#ifdef`, braces closed at `:2076-2078`) | The dispatch order is the server's contract and cannot be read at a glance | dispatch table by tool name; `accept_loop()` per platform in `platform/` | M | low | `vccd-test.py` |
| Q-drv-19 | L2 | pattern | private `buf` `ppdriver.c:55-97, 337-345` beside the `include/common/buf.h` it includes (`:46`); vectors `rsh.c:38, 178`; `xrealloc`, FNV, interner, hash set `dstore.c:54-141`; chained hash `fixdep.c:33-77`; inline FNV `ppdriver.c:1788-1797` = `frontend/diag/diag.c:1757-1759` | for `quality-lib.md` | `lib/` | S | low | `vcc-same.py` |
| Q-drv-20 | L8 | global state | uncached `getenv` on a per-function or per-item path: `VCC_VIR_CHECK` `codegen/vir/a64.c:2412`, `arch/x64/codegen/vir.c:1048`; `VCC_VIR_ROT` `arch/x64/codegen/vir.c:1081`; `VPP_HC_DEBUG` `frontend/pp/hcentry.c:840`; `VCC_DIAG_DOCS` `frontend/diag/diag.c:1673`.  Per TU: 10 more (`ppdriver.c:1845` twice, `:1894`, `frontend/pp/setup.c:95`, `frontend/c/tu.c:303`, `codegen/tu.c:439-448, 731, 782`) | a linear scan of `environ` each time (not measured); unsynchronized statics at `codegen/vir/a64.c:452, 2206, 2383` | registry accessor, read once | S | low | `vbench.py gate` |
| Q-drv-21 | L10 | dead | `vccd.c:498-501` (unreachable: `:488` takes `--param`); `--lld` `vccd.c:1900`; `(void)hosted` `ppdriver.c:1557`; `VPP_WARM` `vpp.c:228, 258-269` (`--warm` exists); doubled comment `ppdriver.c:441-444`, orphan comment `:371-372`; `shim.c:25-30` includes twice; empty banners `rsh.c:21`, `fixdep.c:29` | noise | delete | S | none | builds |
| Q-drv-22 | L10 | dead | backend path: `forward` `vccd.c:945-1016`, `:1485-1553`, `:1563-1592`, `:616-618`, `VCC_PP`/`VCC_CC` `:1921-1922`, `final` in `cc_own` `:886-921` | about 300 lines for a clang fallback no harness starts (`--backend-sock` appears in one comment in `bench/`) | Decision 4 | M | medium (Linux host programs) | `vccd-kbuild.sh` on Linux |
| Q-drv-23 | L6 | stale doc | `caps.c:8-13, 42, 173-181`: one target (`--target=aarch64-linux-gnu`) though x86-64 and wasm exist; version and resource directory are Homebrew literals; `frontend/pp/ppdata-predef.inc:3355` bakes `/opt/homebrew/Cellar/llvm/23.1.4/...` into the search path; `-isysroot` is in `takes_value` and unknown to the driver | probes answer for one target; an installed vcc looks in the build machine's paths | per-target capability rows in Q-drv-01's table; PLAN-release | M | medium | kernel `.config` unchanged |

**Q-drv-02, the split of `ppd_run1`.**  What it holds, by line:

| Lines | Job | Proposed home |
|---|---|---|
| 905-966 | about 110 locals: the parsed command line as loose variables | `struct cmdline` (`driver/cmdline.h`) |
| 968-1300 | option loop in clang's syntax (diagnostics, language, `-M*`, search path, target, codegen, hardening) | `driver/cmdline.c` `cmdline_parse()`, from the table of Q-drv-01 |
| 598-631, 651-733, 1301-1334 | arm64 LSE from `-march/-mcpu`; x86 CPU and feature resolution; x86 switches rejected on arm64 | `driver/target_a64.c`, `driver/target_x64.c` |
| 1348-1389 | `-Werror=unknown-warning-option` | with `warning_group_known()` (Q-drv-13) |
| 1390-1418, 817-853 | input: stdin, devices | `driver/input.c` |
| 1429-1474, 1927-1945 | `.s`/`.S` to the assembler, twice | `job_assemble()` in `driver/job.c` |
| 1484-1501, 441-494 | depfile name and target; the make rule | `driver/depfile.c` |
| 1511-1576 | search path: `-iquote`, `-I`, `CPATH`, `-isystem`, SDK, resource dir, `-idirafter` | `driver/searchpath.c` |
| 1604-1728, 735-776, `x86predefines.h` | predefine text | `driver/predef.c` |
| 1577-1603, 1729-1757 | `pp_opts` filled in two halves | `job_pp_opts()` |
| 1786-1824, 125-319 | `--vcc-tokhash`, `--cmp` | `driver/tokcmp.c` (Q-drv-12) |
| 1825-1897 | C: `cc_opts`, `-S` output file, `cc_run`, object, depfile | `job_compile()` in `driver/job.c` |
| 1898-1953 | `-E`, `-dM`, `-M` | `job_preprocess()` |
| 347-439, 496-596 | atomic output, object state pool, `write_obj` | `driver/output.c` |
| 855-891 | `--explain` | `driver/explain.c` |

`ppd_run` then reads: parse, classify, open, run one of four modes, write.  Do it in two commits
per piece (move, then change), each under `vbench.py gate`.

**Q-drv-04, the map of `vccd.c`.**

| Lines | Job | Proposed file |
|---|---|---|
| 108-127, 296-306 | socket queue | `vccd/server.c` (with the scheduler's outcome) |
| 131-294 | build generations (`gen_*`), memory restart | `vccd/build.c` |
| 310-448 | request: receive, validate, split; `say` | `vccd/request.c` |
| 450-719 | probes (cc, as, source, link, ld) | `vccd/probe.c` (next to `caps.c`) |
| 721-873 | classification: pp, compile, host link, delegate | `vccd/classify.c` (Q-drv-08) |
| 875-940, 1089-1109 | compile here with captured diagnostics; delegate; fixdep after | `vccd/compile.c` |
| 942-1067 | forward to a backend; spawn a program | `vccd/forward.c`, `vccd/spawn.c` |
| 1111-1211 | ld, nm, objcopy in process | `vccd/tools.c` |
| 1213-1348 | host programs: compile each source, link | `vccd/hostcc.c` |
| 1350-1614 | dispatch (`handle`, `handle1`) | `vccd/dispatch.c` |
| 1616-1742 | request log; recipe callbacks for rsh | `vccd/recipe_glue.c` |
| 1744-1770 | counters | `vccd/stats.c` |
| 1772-2079 | worker loop, drain, peer check, `main`, accept loop per OS | `vccd/server.c`, `vccd/main.c` |

**Q-drv-01, the option table.**  There is no shared facility: `ppdriver.c` has 155 lines that
compare an option string, `vccd.c` 60, `wasmld.c` 16, `ld.c` 10 plus its own `parse_args`
(`ld/ld.c:3780`), `ldmacho.c` 9, `objcopy.c` 8, `vasm.c` 7, `viralloc.c` 8, `vpp.c` 7; `ar.c` parses
ar's letter syntax.  Two different needs: (a) clang's driver syntax for the compiler, large and
order sensitive, which wants a generated table (X-macro rows: spelling, form flag/joined/separate/
either, target field, value check, which targets accept it, probe answer); (b) small tools, for
which a 40-line `lib/argv.h` iterator (`--name`, `--name=V`, `--name V`, positional) is enough.
GNU ld's and objcopy's syntaxes are their own tables in the linkers' review.  (a) is the one
that matters before C++.

**Q-drv-05, the protocol.**  Version: the magic `"roo2"` is bumped by hand and doubles as the
version; in practice the build id (size and mtime of the vccd next to the shim, `protocol.h:36`)
catches every mismatch and is answered `VROOM_STATUS_MISMATCH` (`vccd.c:1808`); a client sending
`build = 0` opts out.  Field by field on the server: magic checked; `payload_len` capped at 8 MB;
string count compared with `argc + envc` but in 32 bits (the bug above); `argc >= 1` checked,
upper bound not; fds exactly three; peer uid checked before reading; `pgrp` trusted (a client that
varies it forces a cache flush per request, same user only); `pad` ignored; `cwd` unchecked (may
be relative or empty).  Six more request fields travel as environment variables and are not in
`protocol.h`: `VCC_UMASK`, `VCC_FIXDEP_D/T/O/CMD`, `VCC_DELEGATE`, `VCC_DELEGATE_SKIP`,
`VCC_DIAG_DEDUP`.  The reply shares one int32 between exit statuses and three control codes.

**rsh and shim.**  The subset is written down once, as a grammar, in `rsh.h:9-21`; `bail_char`
(`rsh.c:60`) is its executable form and `bench/vsh-test.py` its test against dash.  Drift from
the grammar, all on the safe side: `/bin/mkdir` is accepted (`rsh.c:386`), `trap` with `EXIT` or
`0` declines (`:331-334`), an empty `xargs` input declines (`:308`), `2>file` declines (`:126`).
`shim.c` is one source and two binaries (`vcc`; `vsh/sh` with `-DVSH`); `vld`, `vas`, `var`,
`vcc-nm`, `vcc-objcopy` are symlinks to `vcc` (`src/Makefile:181`) and the server picks the tool
from `argv[0]` in two separate name lists (`vccd.c:1371-1397` and `sh_is_tool` `:1644-1645`).
Every shim error says `vcc:` whatever the tool.

### 4.1 Environment knobs (L8): the 58 names

Class: **P** product option (belongs on a command line or in vccd's options), **T** test or debug
aid (keep, in one registry), **X** experiment that is over (remove), **D** dead.  "Read": once =
cached for the process or thread; TU = `getenv` per job; fn = per function; other as said.
"Doc": A = AGENTS.md, d = a file in `doc/`/`TODO.md`, s = a source comment only, b = a harness
only, - = nowhere.  "vccd": what happens when the job runs in the server.

| Name | Read at | Effect | Read | Doc | Class | vccd |
|---|---|---|---|---|---|---|
| `NOINTERN` | `bin/lextest/lextest.c:203` | lextest: lex without interning | once | - | T (a lextest option; unprefixed) | n/a |
| `PMC` | `bin/lextest/lextest.c:204` | lextest: Apple PMU counters to read | once | - | T (unprefixed) | n/a |
| `PREFETCH` | `bin/lextest/lextest.c:257` | lextest: read through the I/O threads | once | - | T (unprefixed) | n/a |
| `SDKROOT` | `platform/darwin/ldmacho.c:215` | SDK whose libSystem `.tbd` a Mach-O link reads | per link | - | P (Apple's variable; also wants `-isysroot`) | **bug**: the server's, not the client's |
| `SYM64_THRESHOLD` | `utils/ar.c:470, 599` | archive offset at which the symbol table goes 64-bit | per archive | d, b | T (llvm-ar's own test knob) | server's by design (`ar.c:598`); should be the request's |
| `TMPDIR` | `server/vccd.c:591, 1487` (and `protocol.h:56`) | temporary files; default socket directory | per use | d | P (standard) | server's: right |
| `VCC_CC` | `server/vccd.c:1922` | `0`: `-c` jobs to the backend | start | s, d | X (only with `--backend-sock`; no harness; the name is also the Makefile's compiler variable) | server option |
| `VCC_CCMP` | `codegen/vir/build.c:3618` | `off`/`all`: conditional-compare joining | once/thread | d | X (measured, default chosen: PLAN-vir.md:2056-2067) | server's |
| `VCC_CRC` | `common/intern.c:56` | x86 host: `software` CRC32C | constructor | d, b | T | server's |
| `VCC_DC_MAX_MB` | `driver/ccsnap.c:433` | declaration cache cap (256) | once | d | P (a `vccd` option) | server's: right |
| `VCC_DIAG_DOCS` | `frontend/diag/diag.c:1673` | base of the link behind a diagnostic id | **per diagnostic** | d | P (`-fdiagnostics-docs=`) | **bug**: the client's terminal, the server's variable |
| `VCC_DR` | `frontend/c/replay.c:3381`, `frontend/pp/setup.c:205` | declaration replay on | once | d, b | open experiment (Decision 5) | server's |
| `VCC_DRCHECK` | `frontend/c/replay.c:3382` | replay and parse, compare | once | d, b | T (with `VCC_DR`) | server's |
| `VCC_DRCOV` | `frontend/c/tu.c:216`, `frontend/pp/setup.c:205` | coverage log of header-cache hits | once | d | X (the measurement is done: RESEARCH-preprocessor.md:745) | server's |
| `VCC_DRREC` | `frontend/c/replay.c:3381`, `frontend/pp/setup.c:205` | record without replaying | once | d, b | T (with `VCC_DR`) | server's |
| `VCC_DR_GAIN` | `frontend/c/replay.c:3383-3384` | replay threshold | once | d, b | T (with `VCC_DR`) | server's |
| `VCC_DR_MAX_MB` | `frontend/c/replay.c:3379` | replay store cap (512) | once | - | P if `VCC_DR` stays | server's: right |
| `VCC_EAGER` | `frontend/c/lazy.c:37` | parse static bodies eagerly | once | d | T (no harness sets it) | server's |
| `VCC_EMIT_REPEAT` | `codegen/tu.c:782` | generate N times (profiling) | TU | b | T | server's |
| `VCC_EMIT_STATS` | `codegen/tu.c:445, 731` | emitter statistics to stderr | TU, twice | b | T | server's stderr |
| `VCC_LEAKCHK` | `arch/a64/codegen/stmt.c:1066`, `value.c:22` | report leaked temporary registers | once; error path | - | T (no harness: make it an assert of the sanitizer builds) | server's |
| `VCC_LEX_SIMD` | `frontend/lex/lex.c:472` | x86 host: cap the SIMD level | once | d, b | T | server's |
| `VCC_LOG` | `server/vccd.c:1909` | one log line per request | start | s, d, b | P (`vccd --log`) | server option |
| `VCC_LREGS` | `codegen/tu.c:439` | registers for locals (direct emitter) | TU | d, b | T | server's |
| `VCC_NODC` | `driver/ccsnap.c:436` | declaration cache off | once | d, b | T | server's |
| `VCC_NOOPT` | `codegen/tu.c:446` | record optimizer off | TU | d, b | T (a release gate runs with it) | server's |
| `VCC_NO_AUTOSTART` | `bin/vcc/shim.c:115` | the shim starts no vccd | per shim | d | P (the shim has no other channel) | client: right |
| `VCC_OBJ_TEXT` | `codegen/tu.c:448` | objects through text and the assembler | TU | d | T (no harness sets it now) | server's |
| `VCC_PP` | `server/vccd.c:1921` | `0`: `-E` jobs to the backend | start | s, d | X (as `VCC_CC`) | server option |
| `VCC_RESTART_MB` | `server/vccd.c:182` | resident size at which vccd re-execs (6144) | per build boundary | d, b | P (a `vccd` option) | server's: right |
| `VCC_SOCK` | `bin/vcc/shim.c:155` | socket path | per shim | s, d, b | P | client: right |
| `VCC_TEST_DELAYED_BUG` | `frontend/c/tu.c:303` | test hook: raise an internal error | TU | d, b | T (not in a release build) | server's |
| `VCC_THREADS` | `driver/ppdriver.c:1845` | emit threads per TU (1) | TU, twice | d, b | P (the scheduler's `-j`) | server's; nests under the workers |
| `VCC_TRACK_DIAG` | `frontend/pp/setup.c:95` | diagnostics record where vcc raised them | TU | A, d | T | server's |
| `VCC_VIR` | `codegen/vir/try.c:26` | `-O2` through VIR: `b` build, `l` lower, `v d s` checks | once/thread | A, d, b | **P** for `b`/`l` (the optimizer switch), T for `v d s` | **bug**: the same command line gives other code per server |
| `VCC_VIR_CHECK` | `codegen/vir/a64.c:2412`, `arch/x64/codegen/vir.c:1048` | run the allocation checker | **fn**, uncached | s, d, b | T | server's |
| `VCC_VIR_LIST` | `codegen/vir/try.c:54` | lower only the listed functions | once | s | T | server's |
| `VCC_VIR_ROT` | `codegen/vir/a64.c:2210`; `arch/x64/codegen/vir.c:1081` | `0`: no loop rotation | once; **fn** on x86-64 | A, d | T | server's |
| `VCC_VIR_SHRINK` | `codegen/vir/a64.c:2387` | `0`: no shrink-wrapping | once | A, d | T | server's |
| `VCC_VIR_VEC` | `codegen/vir/vec.inc:1483` | `0` off, `r` report, `n` unhoisted | once/thread | A, d, b | T (off is `-fno-vectorize`) | server's |
| `VCC_VL_PROBE` | `codegen/vir/a64.c:455` | nop after allocator edits | once | A, d | T | server's |
| `VCC_VMREG_CHECK` | `include/common/arena.h:92` | verify regions at every put | once | s | T | server's |
| `VCC_VMREG_STATS` | `include/common/arena.h:92` | region statistics at exit | once | s | T | server's |
| `VLD_REPEAT` | `ld/ld.c:6706` | link N times (profiling) | once (vlink's `main`) | s | T | n/a |
| `VLD_TIME` | `ld/ld.c:6539` | phase times | per link | - | T | server's |
| `VLEX_IO_THREADS` | `frontend/lex/lex.c:2024` | read-ahead threads (3) | once | d | P (the scheduler) | server's: right |
| `VPP_BATCH_ERR` | `bin/vpp/vpp.c:64` | `--batch`: diagnostics to stderr | **per job** | b | T (a `--batch` flag) | n/a |
| `VPP_CMP_NOCACHE` | `driver/ppdriver.c:269` | `--cmp`: ignore `REF.i.th` | per compare | A | T | n/a |
| `VPP_HC_DEBUG` | `frontend/pp/hcentry.c:415`, `:840`, `frontend/pp/setup.c:987` | header cache: why a miss; more statistics | once; **per uncacheable region** at `:840` | d | T | server's |
| `VPP_HC_MAX_MB` | `frontend/pp/setup.c:201` | header cache cap (8192) | once | d | P (a `vccd` option) | server's: right |
| `VPP_HC_PUB` | `frontend/pp/hcentry.c:662` | one line per stored entry | once | d | X (the profile it served is done: PLAN-fas2.md:337) | server's |
| `VPP_NODIRCACHE` | `frontend/pp/include.c:29` | directory cache off | once | b (fuzz) | T | server's |
| `VPP_NOHC` | `frontend/pp/setup.c:204` | header cache off | once | d, b | T | server's |
| `VPP_NOPREFETCH` | `frontend/pp/include.c:608` | include prefetch off | once | - | X (no document, no harness) | server's |
| `VPP_TIME` | `driver/ppdriver.c:1894, 1913` | job time to the job's stderr | TU | s, d | T | server's variable, every client's stderr |
| `VPP_TOKCOUNT` | `driver/ppdriver.c:1794` | `--vcc-tokhash` counts only | TU | b | T (`--vcc-tokcount`) | n/a |
| `VPP_WARM` | `bin/vpp/vpp.c:228` | N untimed passes first | once | s | D (`--warm` exists; no user) | n/a |
| `VSH_REAL` | `bin/vcc/shim.c:53` | the shell vsh falls back to | per vsh | A, s, d, b | P | client: right |

Totals: 14 P, 36 T, 6 X, 1 D, 1 open experiment.  Not in the scan because they are not read
with `getenv` in `src`: `CPATH`, `C_INCLUDE_PATH`, `SOURCE_DATE_EPOCH` (`ppd_getenv`,
`ppdriver.c:909, 1595`: the client's, as it should be), `XDG_RUNTIME_DIR` (`protocol.h:56`), and
the six request fields named under Q-drv-05.

**Server or client.**  The driver already has the right mechanism, `ppd_getenv`, and uses it for
three names; everything else calls `getenv` and gets the server's environment.  With autostart
that is the environment of the first client (`shim.c:138` passes `environ`), kept across
restarts (`execv`, `vccd.c:239`).  For the server's own resources that is right (`TMPDIR`, the
cache caps, `VLEX_IO_THREADS`).  It is a bug for `SDKROOT`, `VCC_DIAG_DOCS` and `VCC_VIR`, and a
trap for every T knob: `VCC_NOOPT=1 make` does nothing, while a server started from a shell that
had `VPP_TIME` set writes a line into every client's stderr.

### 4.2 Small I/O helpers in these files that exist elsewhere (Q-drv-09)

| Helper | Copies |
|---|---|
| write all of a buffer to an fd | `write_all` `ppdriver.c:356` (int), `write_full` `vccd.c:341`, `write_all` `rsh.c:454`, `wr` `dstore.c:298` (no EINTR), `write_all_fd` `ar.c:704`, `vfdbuf_flush` `include/common/buf.h:156`; single unchecked `write` at `fixdep.c:346` |
| write a file | `write_file` `ppdriver.c:407` (temporary + rename + umask), `ld/objcopy.c:499`, `frontend/diag/diagout.c:574`; plain `open`/`write` at `fixdep.c:345`, `vccd.c:692`, `rsh.c:543`, `ppdriver.c:239` |
| read all of an fd or file | `read_full` `vccd.c:326`, `read_stdin` `vccd.c:531`, `read_all` `vccd.c:553`, `read_stdin_all` `ppdriver.c:818`, `read_at` `fixdep.c:285`, `vpp.c:109`, `ppdriver.c:1448`; elsewhere `read_whole` `ldmacho.c:223`, `slurp` `virtest.c:25` = `viralloc.c:33`, `read_file` `frontend/lex/lex.c:1581`, `vmap_file` `include/port/mapfile.h` |
| monotonic clock | `now` `ppdriver.c:117`, `nsnow` `ppdriver.c:500`, `now` `vpp.c:48`, `now_ms` `vccd.c:1071`; elsewhere `codegen/tu.c:9`, `ld/ld.c:148`, `bin/vasm/vasm.c:31`, `viralloc.c:75`, `lextest.c:34` |
| `st_mtime` in ns | `ST_MTIME_NS` `ppdriver.c:49-53`, `protocol.h:42-46` |
| look up NAME in an envp | `req_env` `vccd.c:1079`, `ppd_getenv` `ppdriver.c:805`, loop at `vccd.c:1355` |
| is it a source file | `src_suffix` `ppdriver.c:323`, `is_src` `vccd.c:1226`, `vccd.c:780`, `vccd.c:731`, `vpp.c:212` |
| `xrealloc`, FNV-1a, error line | `dstore.c:54, 66`; `fixdep.c:41`; `say` `vccd.c:441`, `err` `rsh.c:469`, `die` `shim.c:64` |

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `ppd_` / `ppdriver` | `driver/` | the compile job, all modes | `job_` / `driver/job.c` |
| `ppd_env` | `ppdriver.h:3` | the job's context: cwd, fds, environment, diagnostics sink | `job_ctx` (and `envp` is the only "env" in it) |
| `ppd_run1` | `ppdriver.c:903` | the job; the `1` only separates `--explain` | gone after the split |
| `syntax`, `emit`, `objmode` | `ppdriver.c:961` | "run the C front end"; 0 none, 1 `-S`, 2 `-c`; "an object is wanted" | one `enum job_mode` |
| `opt`, `olevel`, `os` | `:947` | three views of `-O` | `olevel`, `osize` |
| `fmath` | `:964` | six bits written as 1, 2, 4, 8, 16, 32; bit 8 is `-pthread` | named flags; `-pthread` out |
| `lnx` | `:943` | aarch64 Linux only (x86-64 Linux is `x86`) | the target enum |
| `md`, `mdeps`, `tri`, `xcf`, `pfe_n`, `sprot`, `sp_gs`, `fast_root4` | `:925-965` | option state | fields named after the option |
| `strict` | `:965` and `:1354` | ISO mode; shadowed by "unknown warnings are errors" | rename the inner |
| `cc_*` | `vccd.c:473-886`, `caps.h` (external linkage) | request predicates and probe answers | `req_is_*`, `probe_*`: `cc_` is the front end's prefix |
| `ld_option`, `ld_rejects`, `ld_run`, `ld_probe` | `caps.c`, `vccd.c` | probe answers and glue | `probe_ld_*`, `run_ld`: `ld_` is the linker's |
| `gen_*` | `vccd.c:138-294` | a build (one make invocation), not code generation | `build_*` |
| `dg_is_compile`, `own_cc`, `own_pp`, `cap_fd` | `vccd.c` | delegate; "vcc does it here"; captured stderr | spelled out |
| `HOST_CC`, `host_cc`, `host_cc_link` | `vccd.c:789, 1239, 830` | a macro, a job and a predicate; `HOST_CC` is also the Makefile's host compiler | `hostprog_*` |
| `handle`, `handle1`, `spawn`, `forward`, `say`, `queue` | `vccd.c` | unprefixed file statics | prefix per new file |
| `T_WORD`, `T_SEMI`, ... | `rsh.c:25` | recipe tokens; `T_` is the C lexer's token prefix | `RT_` |
| `VROOM_`, `vroom_` | `protocol.h` | phase 1's name on vcc's protocol | `VCCD_` at a rename window |

Prefix conventions: `ppd_` holds in the driver's externals only; `vccd.c` has none; `dstore_`,
`rsh_`, `vfixdep` hold.  Words with two meanings: **probe** (a kbuild capability test; a nop
that counts allocator edits, `VCC_VL_PROBE`; a directory lookup, `vdir_probes`), **backend**
(the clang server; `--ld PROG`; the object backend of `codegen/`), **host** (the build machine's
programs; host SIMD), **job** (`ppd` job, `dstore_job`, fixdep's `job`, vpp's `job`), **kind**
(a request class; a probe kind; a token kind), **env** (`ppd_env` the context; the environment).

## 6. Decisions needed

1. **Knob policy.**  Options: (a) leave `getenv` where it is and document the list; (b) a registry
   `lib/knobs.def` (name, class, one-line help) with one accessor that takes the job context, read
   once, plus a premerge check that no other `getenv` exists and that AGENTS.md's list is the
   generated one.  Then three rules: product knobs become options (`vccd --log`, `--restart-mb`,
   `--hc-max-mb`; `-fvcc-vir=`, `-fdiagnostics-docs=`); test aids are read from the **job's**
   environment (`ppd_env.envp`), so `VCC_NOOPT=1 make` works under vccd and the server's own
   environment stops mattering; X and D are deleted.  Recommendation: (b).  It changes what a
   knob set in the server's shell does: the harnesses that put a job knob in a private vccd's
   environment must move it to the client (`var-cmp.py:113`; `vbench.py` and `vcc-stress.py`
   mention both and need a look).
2. **Option table and unknown options.**  Table-driven parsing (Q-drv-01) is recommended either
   way.  The behaviour question is separate: today an unknown option is ignored by the compiler
   and rejected by vccd's probes.  Options: keep; warn; error as clang.  Recommendation: error as
   clang, after one run that collects every option the kernel, PostgreSQL, SQLite, zlib and ffmpeg
   builds pass (each must then be a row, possibly "accepted, no effect").
3. **What is `vcc`.**  The standalone compiler binary is `vpp` and its header says "the
   preprocessor alone"; `vcc` is a client that needs a server.  Options: keep; `vpp` becomes
   `vcc` and the shim `vcc-client` (or `vcc` tries the server and falls back to compiling in
   process).  Belongs with PLAN-release; the file renames (`job.c`, `client.c`, `recipe.c`) do not
   wait for it.
4. **The clang backend in vccd** (Q-drv-22).  Delete `--backend-sock`, `forward`, `VCC_PP`,
   `VCC_CC` and the capture-then-hide logic of `cc_own`, or keep them for Linux host programs
   (`host_cc` refuses there, `vccd.c:1245-1248`)?  Recommendation: delete once vld links ELF host
   programs; until then mark the path as phase 1 in one file (`vccd/forward.c`).
5. **Declaration replay** (`VCC_DR` and five satellites): opt-in since 2026-11-09 with
   byte-identical objects reported.  Turn it on by default, or remove it; either way the six
   names go.
6. **Request fields in the environment.**  `VCC_UMASK`, `VCC_FIXDEP_*`, `VCC_DELEGATE*`,
   `VCC_DIAG_DEDUP`: move the shim-made one (`VCC_UMASK`) into the header (the `pad` field is
   free) and document the user-made ones in `protocol.h`, or keep all as they are but written
   down.  Recommendation: the first.
7. **Paths: `dirfd` or joined strings** (Q-drv-14).  The linkers, ar and rsh use a directory fd;
   the driver, pp and fixdep join strings.  One rule for a job is needed before more code is
   moved into the server.

## 7. Not covered

- Read in full: every file listed in section 2.  Not read: `driver/ccsnap.[ch]`, `utils/ar.c`
  (other reviews), the generated `explain.inc`, `x86macros-data.inc`, `warnings.inc`.
- The knob table's non-driver rows come from the `getenv` call and 10-20 lines around it, not
  from reading those files; "effect" for `VCC_DR*`, `VCC_CCMP`, `VCC_LREGS` and `VCC_OBJ_TEXT` is
  taken from the plans they are documented in.  "No harness sets it" is a word search over
  `bench/`, `ci/`, `env/`, `src/Makefile`, `src/test/fuzz`.
- Nothing was built or run.  Q-drv-05, Q-drv-06, Q-drv-07 and the `host_cc`/`-imacros` case in
  Q-drv-08 are from reading the code; each needs a test before it is called a bug.
- No measurement: the cost of the argv and envp passes, of the per-function `getenv`, and of the
  depfile round trip (Q-drv-11) is inferred.
- The threading design of vccd (queue, worker count, `gen_*` locking, restart) is described as
  jobs only; `quality-scheduling.md` has it.  `contrib/phase1/` (the other user of `protocol.h`) was
  not read beyond confirming it uses the same header.
- Option parsing inside `ld.c`, `wasmld.c`, `objcopy.c`, `vasm.c` was counted, not read.
