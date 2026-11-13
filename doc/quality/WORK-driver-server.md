# Work order: driver and server

The module pass of `PLAN-quality.md` for `src/driver/` (the compile job) and
`src/server/` (vccd).  The evidence is `doc/review/quality-driver-server.md`
(finding ids `Q-drv-NN`; its line numbers are those of `main` at 2e6d3e73,
which are also today's for both files).  Names follow `doc/STYLE.md`.

**Owns** `src/driver/ppdriver.c` (gone after step 1), `src/server/vccd.c`
(stays: it keeps `main`), the new files under `src/driver/` and
`src/server/`, and the `driver`/`server` lists in `src/Makefile`.  Not
`ccsnap.c`, `caps.c`, `dstore.c`, `utils/`, `bin/vcc/shim.c`,
`include/protocol.h` or `include/driver/*.h` (changes there come in the
later steps that name them).

## What done looks like

- One job per file, each opening with a sentence saying the job; one
  internal header per directory (`driver-internal.h`, `vccd-internal.h`).
- No function above about 300 lines; `ppd_run1` is a short sequence of
  calls.
- One option table per tool; an unknown option is an error.
- Product knobs travel with the job; test aids are in one registry.
- The wire protocol is validated in one place.
- Names follow STYLE.md (`driver_` and `vccd_` prefixes, full words).

## Steps

Each step is one PR.  Proof for every step: `make -C src -j 6 vcc` (unity
and `VCC_UNITY=`), `bench/vcc-driver.py`, `bench/vccd-test.py`,
`bench/vcc-stress.py --vccd`, `bench/vcc-ctests.py --suite vcc`,
`bench/vcc-diag.py`, `bench/vcc-same.py --base` byte-identical, and where
the step touches the hot path `bench/vbench.py gate`.

### 1. Split (moves only; this PR)

Both directories become unity modules (`driver_SRC`, `server_SRC` in
`SPLIT_MODS`, one TU each as `cc`, `pp`, `codegen`), so a shared function
stays `static` through `VPRIV`; every file also compiles alone
(`VCC_UNITY=`).  State that two files share is a global declared in the
internal header and defined in one file.  No function or variable is
renamed; the new helper functions are `driver_*`.

`ppd_run1` (1066 lines, about 110 locals) is cut by moving its locals into
`struct driver_job` (`driver-internal.h`) and each piece into a function
that takes `struct driver_job *job`.  A piece returns nonzero where the
original did `goto out`, with the status in `job->status`; the strcmp chain
of the option loop is unchanged.  The one bare `return 1` of the loop (the
`-ftls-model=` error, Q-drv-07) keeps its behaviour as return code 2.

`src/driver/` (line numbers of today's `ppdriver.c`):

| Lines | File | Job |
|---|---|---|
| 54-97, 340-345 (`buf`, `bput`, `bstr`, `bfree`), 100-110 | `job-buffer.c` | the job's growing text buffer |
| 111-318 | `tokcmp.c` | `ppd_compare`, the token-stream hash and its cache (`--cmp`); `--vcc-tokhash` (1786-1824) as `driver_tokhash_run` |
| 347-439, 496-596 | `output.c` | atomic output files, the client's umask, object state pool, `write_obj` |
| 441-494, 1484-1501 | `depfile.c` | the make rule; the depfile's name and target |
| 598-631 | `target-a64.c` | LSE from `-march`/`-mcpu` |
| 651-733, 1301-1334 | `target-x64.c` | x86 CPU and feature resolution; x86 switches rejected on arm64 |
| 735-776, 1604-1728 | `predef.c` | the predefine text of a job, and the tables it is built from (`ppdata-*.inc`) |
| 778-803, 1511-1576 | `searchpath.c` | `-I` lists, `CPATH`, the SDK and resource directories |
| 817-853, 1390-1418 | `input.c` | the main file: stdin, devices |
| 855-891 | `explain.c` | `--explain` |
| 968-1300 | `cmdline.c` | the option loop, `src_suffix` |
| 1335-1389 | `warning-groups.c` | `-Werror=unknown-warning-option` |
| 1429-1474, 1927-1945, 1577-1603, 1729-1757, 1825-1953, 893-966, 1954-1975 | `job.c` | `ppd_run`, `ppd_run1`, assemble, compile, preprocess, `pp_opts` |
| | `driver-internal.h` | `struct driver_job`, the shared prototypes |

`src/server/` (line numbers of today's `vccd.c`):

| Lines | File | Job |
|---|---|---|
| 108-127, 296-306 | `work-queue.c` | accepted sockets waiting for a worker |
| 131-294 | `build-generation.c` | build generations, memory restart |
| 308-448, 1071-1093, 1618-1635 | `request.c` | receive, validate, split a request; `say`, `req_env`, the request log |
| 450-719 | `probe.c` | toolchain probes |
| 721-873 | `classify.c` | what a request is: pp, compile, host link |
| 875-940, 1095-1112 | `compile.c` | compile here with captured diagnostics, delegate, fixdep after |
| 942-1017 | `forward.c` | forward to a backend |
| 1019-1067 | `spawn.c` | run a program |
| 1114-1211 | `tools.c` | ld, nm, objcopy in process |
| 1213-1348 | `host-programs.c` | compile and link the host programs |
| 1350-1614 | `dispatch.c` | `handle`, `handle1`: the server's dispatch order |
| 1637-1742 | `recipe.c` | recipe callbacks for rsh |
| 98-106 (counters), 1744-1762 | `counters.c` | request counters and their printout |
| 1764-1861 | `worker.c` | worker threads, drain |
| 1863-2079, 1-47, 98-106 (options) | `vccd.c` | the tour comment, peer check, `main`, accept loop per OS |
| | `vccd-internal.h` | request and worker types, shared state, prototypes |

Left in place, with the reason: `handle1` (248 lines) and `host_cc` are
cut by Q-drv-18 in step 4, not here; the `kqueue` and `epoll` loops share
an inner block across `#ifdef` in `main` and stay in `vccd.c`.
(Anything else that could not be cut is recorded below.)

What step 1 did differently from the maps above (line numbers in the
review had drifted by a few lines; the files and jobs are as listed):

- `driver/`: `ppd_run1` is about 120 lines.  `-ftls-model=` with a bad value still
  returns 1 without the job's cleanup (code 2 of `driver_cmdline_parse`;
  Q-drv-07 fixes it in step 2).  `--cmp` stays in the job's `ppd_compare`
  (`tokcmp.c`); making `tokcmp.c` a `vpp`-only TU is Q-drv-12.
  `explain.c` is the old `explain` and its table; `ppd_getenv` and
  `intern_full` stay in `job.c`.  `-S`'s output name, the PIE/CFI defaults
  and the `objmode` switch stay in `ppd_run1` (three to ten lines each).
- `server/`: the request log (`log_req`), `now_ms` and `req_env` went to
  `request.c` (they read a request), not to the recipe file.  There is no
  `stats.c`: the counters and `print_counts` are `counters.c`.
- Shared variables of the server are globals declared in `vccd-internal.h`
  (external symbols even in the unity build, as `kwmap` in `ccimpl.h`); the
  driver's few (`bufpool`, `bufr`, `tl_as`, `tl_obj`, `tl_objbuf`,
  `tl_wasm_obj`) likewise.

Outside the owned files and left for their owners: `contrib/wasm/build.sh`
lists `driver/ppdriver.c`; `AGENTS.md` names `ppdriver.c`.

### 2. One option table per tool (changes behaviour)

`driver/options.def`: one row per option (spelling, form, field, value
check, targets that accept it, probe answer), parsed into
`struct driver_cmdline`; `caps.c`, the deny list (`gcconly`), the
value-taking lists in `vccd.c` and `--help` read the same table
(Q-drv-01, 08, 21).  An unknown option is an error, as clang's (listed in
the PR: options accepted today that fail then).  `-target TRIPLE` is read
(Q-drv-06).  Small tools (`vlink`, `vasm`, `objcopy`, ...) get a short
`argv` iterator in `common/` from `quality-lib`.  Proof adds `vcc-driver.py`
cases, a kernel build, `vcc-diff.py cmp/dm/deps`.

### 3. Knobs, registry, job context (Q-drv-03, 10, 14, 20)

The 14 product knobs (`VCC_VIR=bl`, `VCC_DIAG_DOCS`, `SDKROOT`, ...) become
options that travel with the job, so a job under vccd does not depend on
the environment of whichever client started the server.  The test aids go
in one registry read from the job's environment, each name documented;
finished experiments (`VCC_CCMP`, ...) are deleted; a premerge check
requires every `getenv` name to be in the registry.  The job context
(`ppd_env`, the thread-locals, `getenv`) becomes one struct with a `dirfd`;
output files take the client's umask in one place.

### 4. Protocol and dispatch (Q-drv-05, 15, 16, 18, 22)

`protocol_check()` beside `struct vroom_req_hdr`: 64-bit count sum,
`argc <= INT_MAX`, last byte NUL, `pad == 0`; new `garbage_requests` cases
in `vccd-test.py`.  Request kind as an enum with a table; `handle1` as a
dispatch table by tool name; one writer of the wire format shared by shim
and server; the backend path (Q-drv-22) is deleted if Johan agrees.

### 5. Rename

Files and file-scope functions to `driver_*` and `vccd_*` in `module_noun_verb`
form, scripted (`src/tools/rename-table.py`), the table reviewed against
STYLE.md.  `ppd_*` public names in `include/driver/ppdriver.h` follow.
