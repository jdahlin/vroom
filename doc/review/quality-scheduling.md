# Quality review: threads and scheduling, L3 (2026-11-13, main at 2e6d3e73)

## 1. Summary

There is no scheduler.  Six private mechanisms start threads, each with its own
queue, thread count, stack size and lifetime, and they nest without a common
cap.  The survey's inventory holds, with the corrections in 2.1.  The compile
path itself scales well when it is fed from one process (`vpp --batch`: 5.1x on
6 threads, 91 % busy), and the shared caches are lock-free.  What matters most:

1. **Two builds through one vccd destroy each other** (Q-SCHED-01): requests of
   two process groups make `gen_enter` drain and flush the caches at every
   switch: the same 168 TUs took 11.1 s instead of 0.8 s, 5x the CPU, 4.4x the
   memory.  Two builds at once on a shared machine are exactly this case.
2. **Nothing follows the machine or the load** (Q-SCHED-02, -03, -05, -06): 20
   vccd workers whatever make's `-j`; a link wakes all 8 P cores for a 4-object
   `ld -r` (2.3x slower, 5.5x the CPU of one thread); emit threads are created
   and joined per round and leak their scratch; old kernel-build logs show jobs
   off the CPU for 43-72 % of their time in flight at `-j 14` and above.
3. **A job is tied to its thread** (Q-SCHED-04, -07, -09): about 75
   thread-locals carry scratch, pooled memory and the job's context; the stack
   (and so the nesting limit) depends on which tool started the thread; CPU
   workers do all their own I/O.  A pool can only replace the six mechanisms
   after per-job state has one owner.

## 2. Inventory

The area has no files of its own; the rows are the mechanisms, then the locks,
the per-thread state and the blocking calls.  All read in full: `server/vccd.c`,
`codegen/tu.c`, `include/common/arena.h`, `bin/vpp/vpp.c`, `frontend/pp/dircache.c`; the threading
parts and every lock site of the files named below.

### 2.1 Thread mechanisms (verified; corrections to the survey in italics)

| # | Where | Unit of work | Queue | Thread count | Lifetime | Stack | QoS | Per-thread state, teardown |
|---|---|---|---|---|---|---|---|---|
| 1 | `server/vccd.c:1965-1973`, `worker_main` :1784 | one accepted socket: receive the request, run it, answer (`handle` :1350) | ring of fds `queue[4096]`, `qlock`/`qcond`, FIFO; full: the connection is closed (:116-127) | `2 * sysconf(_SC_NPROCESSORS_ONLN)` = 20 here (:1889); *`-j N` overrides (:1902)* | process; never joined; SIGTERM drains by polling (:1846) | 16 MB, 2 MB under TSan (:1777-1782) | none | two mmaps per worker (:1789); every thread-local of the compiler; none freed |
| 2 | `codegen/tu.c:404-525`, `worker` :208 | one function, object or string (`do_item` :186); items enqueue more items (`arch/a64/codegen/bind.c:22,57`, `codegen/prepass.c:629`) | array `E->q`, `E->mu`/`E->cv`, FIFO, `busy` count for the end (:213-232) | `VCC_THREADS` *of the process, not of the request* (`driver/ppdriver.c:1845` uses `getenv`, not `ppd_getenv`), default 1, cap 64 (:489) | *per `emit_tu1` call: up to 8 rounds per TU (:794) and `VCC_EMIT_REPEAT`*; joined (:525); the caller is worker 0 (:523) | the caller's size (:499-518) | none | `ecx` regions from `ecxpool` (:94, given back :150); optimizer and VIR scratch thread-local, leaked at exit (2.3) |
| 3 | `ld/ld.c:239-313` `pfor` | index `i` of `[0, n)` | shared atomic index `job.next` (:259); one global `job` slot, `plock`/`pcv`/`pdone` | macOS `hw.perflevel0.physicalcpu` = 8 (:5761), else `_SC_NPROCESSORS_ONLN` (:5765), else 4; cap 64; `--threads N` (:3874) | started at the first `pfor` with n > 1 (:294); never joined; *size frozen by the first link (`pool_started`), a later larger `--threads` is ignored* | default (512 KB on Darwin) | none | `__thread` arena cursor reset by generation (:174-204); `count_locals` scratch (:3477) never freed |
| 4 | `frontend/lex/lex.c:1982-2033` `io_thread` | one file: `open`+`fstat`+`read` (`read_src` :1604) | `ioq`, grows by doubling, `ioq_mu`/`ioq_cv`, FIFO (:2047-2062) | 3, `VLEX_IO_THREADS` 1..32 (:2024) | `pthread_once` at the first `vlex_prefetch` (:2063); detached | default | `QOS_CLASS_USER_INTERACTIVE` (:1992) | the file bytes live in the I/O thread's arena (`ar_cur` :1543) for the process' life |
| 5 | `bin/vpp/vpp.c:56-95` | one TU (`ppd_run`) | atomic index `nextjob` | 6, `-j N`, cap 64 (:143, :251) | per pass (1 + `--warm`); joined | default (512 KB on Darwin) | none | everything of a compile job; leaked at each pass' end |
| 6 | `bin/lextest/lextest.c:131-168` | one file | atomic index | `-j N`, up to 256 | per run; joined | default | `USER_INTERACTIVE` (:133, :290) | test tool only |

*23 `pfor` call sites, not 24* (`ld.c:2375`...`6371`).  `ld_macho`
(`platform/darwin/ldmacho.c`), `ld/wasmld.c`, `ld/objcopy.c`, `utils/ar.c`,
`utils/rsh.c`, `utils/fixdep.c` start no threads.  `rsh.c` starts no process:
a recipe's tools run as nested `handle()` calls in the same worker
(`vccd.c:1665-1721`).  `posix_spawn` (`vccd.c:1051`) is reached only with
`VCC_DELEGATE` or `--ld PROG`; a kernel build logs "0 spawned".

### 2.2 Locks

| Lock | Protects | Exists because of file-scope state? |
|---|---|---|
| `vccd.c:113` `qlock`/`qcond` | the socket ring, `n_active` | queue |
| `vccd.c:138` `gen_mu`/`gen_cv` | build generation: `gen_pgrp`, `gen_running`, `gen_flushing` | policy (Q-SCHED-01) |
| `vccd.c:1188` objcopy `lock` | all of `objcopy_main`/`nm_main` | **yes: three globals**, `oerr`, `oerrors`, `prog` (`ld/objcopy.c:45-47`, 22 uses) |
| `ld.c:6542` `ldlock` | all of `run()` and `free_arenas()` (:6674) | **yes: 87 file-scope statics** reset by `reset_state`; the single `job` slot and `blocks` list too |
| `ldmacho.c:744` `lock` | all of `link_macho` | **yes: 20 file-scope statics** (:66-448) |
| `ld.c:86` `errlock` | `errf` and `prev_multi` across `fprintf` to the client's stderr | yes (`errf`) |
| `ld.c:173` `blocklock` | the list of arena blocks | yes |
| `ld.c:245` `plock`/`pcv`/`pdone` | the `pfor` job slot | yes: one slot, so `pfor` cannot nest or serve two links |
| `lex.c:1083` `lock` | first-use build of the dispatch table | once-init |
| `lex.c:1983` `ioq_mu`/`ioq_cv` | read-ahead queue | queue |
| `frontend/pp/hcache.c:21` `sig_mu` | signature strings, linear search of up to 65536 under the lock (:31) | table |
| `frontend/c/replay.c:576` `tc_mu` | insert into the tag-content table (readers lock-free) | table |
| `server/dstore.c:46` `dmu` | diagnostics store; held while `dstore_query` writes all of it to the client (:336-380) | table |
| `driver/ppdriver.c:519` `obj_mu` | free list of assembler/object states | pool |
| `include/common/arena.h:186` `vmpool.mu` x7 (`pppool`, `ccpool`, `bufpool`, `ecxpool`, `rpool`, `erpool`, `mfpool`) | free list of region sets | pool |
| `arch/wasm/codegen/wasm.c:7` `wpool_mu` | free list of wasm states | pool |
| `emitimpl.h:201-222` `E->mu`/`cv`, `cmu`, `emu`, `dmu` | per round: item queue; chunk list (`a64/emit/func.c:10`); diagnostics (`tu.c:25`, `call.c:10`); `.file` table (`stmt.c:1003`); initialised, never destroyed (`tu.c:427-431`) | queue and shared tables |
| `codegen/vir/try.c:49`, `arena.h:76` | `VCC_VIR_LIST` read once; debug table | debug |

Twelve `pthread_once` sites.  The shared caches take no lock: CAS publication,
the loser frees its copy (`frontend/pp/hcentry.c:866`, `frontend/pp/macro.c:74`,
`frontend/pp/include.c:317`, `driver/ccsnap.c:401-425`, `lex.c:1707-1741`,
`frontend/pp/dircache.c:79-98`, `common/intern.c:169-180`).  Two wait by spinning:
`vlex_get` (`lex.c:1948-1973`) and `gintern` (`ld.c:535`).

### 2.3 Per-thread state (about 75 definitions in 36 files, no destructor anywhere)

| Class | Variables | A pooled worker between jobs | When a thread exits |
|---|---|---|---|
| Job context passed through TLS | `vjob_jb`, `vjob_fail_fd` (`obj/obj.c:15-17`), `vjob_escape_fd` (`common/intern.c:139`), `ppd_umask`, `ppd_conn` (`ppdriver.c:373`), `kwmap` (`frontend/c/token.c:6`), `ppr` (`frontend/pp/setup.c:59`), `ccr` (`frontend/c/tu.c:8`), `bufr`, `tl_as`/`tl_obj`/`tl_objbuf`/`tl_os`, `tl_wasm_obj` (`ppdriver.c:63,512-559`), `pp_diag_pc`, `pp_diag_next_id` (`frontend/diag/diag.c:1353`), `const_eval_path` (`frontend/c/const.c:141`), `vlex_no_intern`, `as_dtype`, `var_prev` (`as/as.c:778,2991`), `sort_o` (`platform/darwin/macho.c:311`), `tl_ctx` (`vir/vir.c:10`), `cap_fd`, `sh_raw`, `sh_strs` (`vccd.c:884,1662`) | must be set at job start and cleared at job end; today each is reset by hand (`ppdriver.c:1953`, `vccd.c:1827-1829`); a job cannot move to or be helped by another thread | nothing lost, but an emit worker (thread 1..n) never has them: `vjob_jb` is NULL there, so `vjob_fail` would `abort()` (not reachable today: only the job thread assembles) |
| Lazily grown scratch | `arch/a64/codegen/opt.c:205` `W` (13 arrays), :445 `SL`, :616 `XF`, :1735; `vir/alloc.c:32`, `vir/loops.c:16`, `codegen/vir/a64.c:76`, `arch/x64/codegen/dead.c:13`; `codegen/tu.c:290-293` (60 GB reserved), :650, :681; `arch/a64/codegen/obj.c:466`; `ppdriver.c:254,448,782,820,907`; `driver/ccsnap.c:251`; `lex.c:1572`; `ld.c:3477` | nothing to reset (written before read); it stays as big as the largest job | **leaked**: malloc and mmap memory with no owner (Q-SCHED-05) |
| Recycled buffers | `out_spare` (`frontend/pp/text.c:7`), `tk_spare`, `sb_spare`, `rec_spare` (`frontend/pp/hcentry.c:421`): the last job's buffers kept for the next | nothing | leaked |
| Arena cursors | `lex.c:1543` `ar_cur`/`ar_end` (immortal cache data), `ld.c:174` `acur`/`aend`/`my_gen` | lex: nothing; ld: reset by `arena_gen` | lex: the unused tail of a 64 MB block is abandoned |
| Caches | `vintern_local` 64 KB (`common/intern.c:328`), `vstack_lim` (:339), `tl_fscr` (`frontend/c/record.c:4`, reset `frontend/c/tu.c:58`) | nothing | the 64 KB reservation is leaked |
| Flags and counters | `ppd_ph` (`ppdriver.c:498`), `leakchk`, `vw_mode`, `m` (`vec.inc:1480`), `FK` (`a64/emit/func.c:139`), `rawp`, `retreads` (`opt.c:39`) | `ppd_ph` is read as a delta (`vpp.c:71-79`): work an emit thread did is not counted | nothing |

Three ways to keep a job's memory for the next exist side by side: `vmpool`
(process-wide sets, written for the per-pass threads of `vpp --batch`,
`arena.h:11-14`), the same pattern by hand twice (`ppdriver.c:517-556`,
`wasm.c:7-62`), and the two thread-local kinds above.

### 2.4 Blocking calls on CPU workers

| Where | Call | When |
|---|---|---|
| `vccd.c:375`, :408 | `poll` up to 2 s, `recvmsg`, `read` of the request | every request; a silent client holds a worker for 2 s |
| `vccd.c:1834` | `write` of the status to the client | every request |
| `vccd.c:250-287` | `pthread_cond_wait` until every running job has ended | a request of another process group (Q-SCHED-01) |
| `lex.c:1610-1621` via `vlex_get` :1941 | `open`, `fstat`, `read`, `close` of a source or header | every file no I/O thread reached first (the main file always) |
| `lex.c:1948-1973` | spin, then `sched_yield()` without end, while another thread reads or lexes the file | concurrent first use of a header |
| `frontend/pp/dircache.c:105` | `stat` of a directory | once per directory |
| `ppdriver.c:399-423` `write_file` | `stat`, `open`, `fchmod`, `write`, `close`, `rename` | every `.o`, `.d`, `-E` output |
| `ppdriver.c:1858-1886`, `codegen/tu.c:698` | `open`, `writev`, `close`, `rename` | `-S` |
| `utils/fixdep.c:295-354` | read the `.d`, write the `.cmd`, `unlink` | every kernel compile |
| `vccd.c:906-917` | `pread`/`write` of captured diagnostics to the client, `ftruncate` | a job with diagnostics |
| `ld.c:1726-1734`, :4040 | `openat`+`fstat`+`mmap` per input, serial on the linking worker | every link (about 900 opens for vmlinux.o: inferred from the TU count) |
| `ld.c:4140-4147`, :5514 | `unlinkat`, `openat`, one `write` of the image; the stubs list | every link |
| `ld.c:6674` | `ldlock` held for the whole link (I/O included); a second link waits on a worker | concurrent links |
| `utils/ar.c:683-942`, `rsh.c:457-551`, `ld/objcopy.c` | archive, redirection, objcopy I/O | those requests |
| `vccd.c:1051-1063` | `posix_spawn` + `waitpid` | `VCC_DELEGATE`, `--ld PROG` |
| `vccd.c:957-978` | `connect`, `sendmsg`, `read` from the backend | `--backend-sock` |
| `dstore.c:336-380`, `ld.c:104` | lock held across writes to the client's fd | `--vccd-diagnostics`, link errors |

## 3. Passes and data flow (L4)

### 3.1 The path of one job through the threads

A kernel compile: make starts `vsh` or `vcc` (a process), the shim sends the
request (`shim.c:229`), the accept thread only accepts and queues the fd
(`vccd.c:2052-2074`).  One worker then does everything, start to end: receive,
`gen_enter`, classify (`handle1`), preprocess + parse (reading and lexing files
inline, queueing the unconditional includes of each new file to the 3 I/O
threads, `frontend/pp/include.c:492,509`), emit (itself as worker 0, plus
`VCC_THREADS - 1` new threads per round), assemble, lay out, write `.o` and
`.cmd`, answer.  A link: the same worker takes `ldlock`, maps the inputs
serially, then runs about 15 `pfor` phases on its own pool.  So work is handed
over at three points only (fd to worker, path to I/O thread, item to emit
thread); nothing is ever handed back to a common pool, and a worker that
blocks holds its 16 MB stack and its thread-local memory.

Who decides how many compile jobs run at once: **make's `-j`**, capped by
vccd's worker count (20).  vccd does not read `MAKEFLAGS` or a jobserver (no
occurrence of either under `src`); its own `-j` is given by no build
script (`bench/vccd-kbuild.sh:26`; only `bench/vccd-cc.py:20` passes `-j 6`).
make and vccd therefore count twice: make's `-j 14` processes (shims, mostly
asleep) and vccd's threads.  PLAN-fas2.md:160-173 (one CPU pool, work
stealing, P/E by QoS, I/O threads for reads and writes, the accept thread
dispatching jobs) is not what the code does; only the read-ahead exists.

### 3.2 Measurements (M1 Pro, load average 5.3-8.1 from other sessions; one run each unless a range is given)

Scripts and outputs: scratchpad `sched/` (`mkjobs.py`, `vccdm.py`, `logan.py`).
CPU is user + sys of `/usr/bin/time -l`.

**A. `vpp --batch`, 210 kernel TUs (every 4th), `-c`, cold process**
(`build/src/vpp --batch jobs -j N -C work/build-o0`):

| `-j` | wall | CPU | busy = CPU / (wall x j) | instructions | cycles |
|---|---|---|---|---|---|
| 1 | 3.22-3.52 s | 3.18 s | 90-99 % | 29.99 G | 9.73 G |
| 2 | 1.59 s | 3.17 s | 100 % | 29.99 G | 9.74 G |
| 4 | 0.92 s | 3.49 s | 95 % | 30.61 G | 10.43 G |
| 6 | 0.68-0.71 s | 3.79-3.88 s | 91-93 % | 31.23 G | 11.47 G |

Fed from one process the job loop scales (5.1x on 6).  The cost of running
together: +4 % instructions (several threads miss the same cache entry and
each does the work, 2.2) and +18 % cycles.

**B. Emit threads inside 6 batch threads** (`VCC_THREADS=T`, same jobs):
T=1 0.68 s, 806 MB, 6.3 k involuntary switches; T=4 0.73 s, 877 MB, 10.2 k;
T=8 0.71 s, 944 MB, 11.6 k; sys time 0.41 to 0.54 s.  No wall gain, +17 %
memory.  With `--warm` passes the resident size grows per pass: T=1 1067 MB
after 3 passes, 1124 MB after 7 (+14 MB a pass, the six threads of each pass);
T=4 1207 to 1407 MB (+50 MB a pass): the leak of 2.3.

**C. One large TU alone** (`work/sqlite/st/sqlite3.c`, `-O2 -c`, 1916
functions; objects byte-identical for T=1 and 8):

| `VCC_THREADS` | 1 | 2 | 4 | 6 | 8 |
|---|---|---|---|---|---|
| direct emitter: emit phase | 59.5 ms | 31.0 | 16.9 | 13.2 | 10.0 |
| ... whole compile, wall | 0.16 s | 0.12 | 0.11 | 0.11 | 0.10 |
| `VCC_VIR=1`: emit phase | 219 ms | | 64.9 | | 37.7 |
| ... whole compile, wall / max RSS | 0.32 s / 256 MB | | 0.16 / 332 | | 0.15 / 388 |

Per-function work scales 5.8x on 8 threads, and with VIR it is two thirds of
the compile: worth having when cores are free, worthless (B) when they are not.

**D. vccd, 168 TUs through the shim, 6 clients** (`vccdm.py`, as `bench/vccd-cc.py`):

| vccd workers | clients | groups | wall | vccd CPU | max RSS | cache flushes |
|---|---|---|---|---|---|---|
| 6 | 6 | 1 | 0.81-0.92 s | 3.05-3.15 s | 721-724 MB | 0 |
| 2 | 6 | 1 | 1.32 s | 2.63 s | 532 MB | 0 |
| 20 (default) | 6 | 1 | 0.75 s | 3.05 s | 889 MB | 0 |
| 6 | 3 | 1 | 1.26 s | 2.68 s | 639 MB | 0 |
| 6, `VCC_THREADS=4` | 6 | 1 | 0.78 s | 3.21 s | 777 MB | 0 |
| 6 | 6 | **2** | **11.07 s** | **15.68 s** | **3205 MB** | **105** |

With 6 clients the server is busy 55-65 % of 6 cores: the clients (process
start, shim) feed it, as in A it would be 91 %.  Twenty workers for six jobs
cost +23 % memory: `pthread_cond_signal` rotates through all of them, and each
builds its own thread-local memory.  The last row is Q-SCHED-01: the same
compiles, alternating between two process groups as two makes of 3 jobs each
would send them (median compile 197 ms instead of 17 ms).

**E. vlink** (`build/src/vlink`, inputs from `work/build-o0`, `VLD_REPEAT`):

| `--threads` | vmlinux.o `-r` x20: wall / CPU | vmlinux (script) x10 | 4 small objects `-r` x500: wall / CPU / involuntary switches |
|---|---|---|---|
| 1 | 0.75 s / 0.72 s | 0.31 / 0.30 | 0.15 s / 0.15 s / 85 |
| 2 | 0.60 / 0.95 | 0.24 / 0.32 | 0.18 / 0.19 / 7.6 k |
| 4 | 0.48 / 1.33 | 0.21 / 0.38 | |
| 6 | 0.48 / 1.78 | 0.20 / 0.42 | |
| 8 (the default here) | 0.48 / 2.22 | 0.21 / 0.50 | 0.34 / 0.83 / 45.8 k |

The big links stop gaining at 4 threads; the default spends 3.1x the CPU of
one thread for 1.56x the speed.  A small link is 2.3x slower with the pool: 92
switches per link, every `pfor` wakes all seven workers (`ld.c:305-307`).

**F. Requests in flight in earlier kernel builds** (`logan.py` on the
`VCC_LOG` lines of existing `work/build-*.vccd.log`; other days, binaries and
loads, so indicative only):

| Log | make | cc-own | sum of their time in flight | vccd CPU (all work) | max in flight | compile phase: length, mean in flight | nothing in flight |
|---|---|---|---|---|---|---|---|
| `build-tree` (11-13) | `-j 6`* | 925 | 17.3 s (mean 18.7 ms) | 14.7 s | 6 | 4.56 s, 3.5 | 55 % of 12.7 s |
| `build-vrs` (11-10) | `-j 6`* | | | 13.1 s | 6 | 3.35 s, 3.4 | 50 % of 8.6 s |
| `build-fd` (11-08) | `-j 14`* | 884 | 27.9 s (mean 31.5 ms) | 16.0 s | 14 | 3.04 s, 8.3 | 56 % of 8.9 s |
| `build-dash` (11-08) | `-j 14`* | 877 | 36.0 s (mean 41.1 ms) | 18.3 s | 14 | 4.17 s, 7.8 | 31 % of 12.5 s |
| `build-allmod` (11-13) | not recorded | 8386 (+630 links) | 596.8 s (mean 71 ms) | 169.1 s | **20 = the cap** | 31.6 s, 17.0 | 14 % of 42.3 s |

(*inferred from the maximum in flight; `vccd-kbuild.sh` defaults to 14.)  The
compiles' time in flight is 1.2x vccd's whole CPU time at `-j 6`, 1.7-2.0x at
`-j 14` (each compile 1.7-2.2x as long as at `-j 6`, the compile phase no
shorter), 3.5x at the cap of 20: the rest is time off the CPU
(PLAN-fas2.md:1497-1499 sampled it: waiting for a core, not for locks).  Half
of a default build has no request in flight at all: make sets the wall time.

**G. Small ones.**  An idle vccd: 21 threads, 279 context switches in 10.7 s
(the 50 ms tick, `vccd.c:1991`), 0.01 s CPU.  Nesting: `int f(int a){return
((...a...));}` compiles with `vpp` up to about 72 000 parentheses and fails in
`vpp --batch` at about 2 330 ("expression nested too deeply"): Q-SCHED-07.

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-SCHED-01 | L3 | thread | `vccd.c:250-287`; comment :131-137; 3.2 D last row | two concurrent builds: 13.7x wall, 5x CPU, 4.4x memory; 105 flushes for 168 TUs; memory restart (6 GB, :183) soon after | caches keyed by build (or validated by mtime) instead of one global generation; at least no flush while the previous group still has jobs running | M | medium: cache validity | `bench/vccd-test.py` + a two-group case (`vccdm.py R 6 6 2`: flushes must be 0 or 1) |
| Q-SCHED-02 | L3 | thread | 2.1 column "thread count": six sources; no `MAKEFLAGS`; `_SC_NPROCESSORS_ONLN` ignores cgroup and affinity (`vccd.c:1889`, `ld.c:5765`) | nesting without a cap: 20 workers x `VCC_THREADS` + 7 link threads + 3 I/O; F: compiles are off the CPU 43-72 % of their time in flight above `-j 6`; CI containers get the host's count | one `sched_ncpu()` (P/E on macOS, `sched_getaffinity` and cgroup `cpu.max` on Linux, an override), one budget that all work draws from | M | low | `vbench.py gate`; D and F rerun as an A/B |
| Q-SCHED-03 | L3 | thread | `ld.c:285-313` (broadcast, `job.active = pool_started - 1`), :294-297 (frozen size), 23 call sites; 3.2 E | small link 2.3x slower and 5.5x the CPU; big link 3.1x CPU for 1.56x; 630 links in an allmod build run next to compiles | shared parallel-for with a grain (serial below a threshold) that wakes only as many workers as there are chunks, from the common budget | M | low: phases already index-parallel | `VCC_LDCMP=1 vcc-ctests.py`, `vlink-opts.py`, kernel boot; E rerun |
| Q-SCHED-04 | L3, L8 | global state | `ld.c:6542,6674` (87 statics), `ldmacho.c:744` (20), `vccd.c:1188` for `objcopy.c:45-47` (3) | one link, one Mach-O link, one objcopy at a time; the waiting request holds a worker; `pfor` cannot be shared | a link context struct (objcopy first: three variables); then `ldlock` goes | S objcopy, L `ld.c` | low / medium | `vobjcopy-cmp.sh`, `vlink-bad.py`, `VCC_LDCMP=1`, TSan `vccd-cc.py` |
| Q-SCHED-05 | L3, L8 | thread | `codegen/tu.c:517-525` inside the round loop :794; scratch of 2.3 (`opt.c:205-225`, `vir/vir.c:12-33`, `vir/alloc.c:32`); 3.2 B | a thread create + join per extra thread per round; +36 MB per 210 TUs at 4 threads never returned; in vccd it only ends at the restart | emit items run on pool workers that live as long as the process; scratch owned by the worker context | M | medium: output must not depend on thread count | `vcc-ctests.py` (its VCC_THREADS=1/8 check), `vcc-same.py`; B's RSS per pass flat |
| Q-SCHED-06 | L3 | thread | 2.4 in full; `vccd.c:1886-1888` ("two per core leaves room for the ones that block") | a core's worker sleeps in `open`/`write`/`rename`/`waitpid`; the worker count is doubled to hide it, which is what oversubscribes (F) | I/O off the CPU workers: request receive and reply on the accept side; output writes and fixdep to I/O threads; spawned programs wait outside the pool | L | medium: error paths (client gone, partial writes) | `vccd-test.py`, `vcc-stress.py --vccd`, kernel build + boot |
| Q-SCHED-07 | L3 | thread | stack column of 2.1; `include/port/vstack.h:10-20`; 3.2 G | the same file compiles or not by entry point: the batch harnesses (`vcc-sema.py batch`, `vcc-diff.py batch`, `vcc-cbatch.py`) run with a nesting limit 30x below `vpp`'s; vccd's stack is twice `vpp`'s again | one stack size for every thread that runs compiler code, set where the scheduler creates threads | S | low | G as a test in `test/vcc` or `vcc-stress.py` |
| Q-SCHED-08 | L3 | thread | `lex.c:1948-1973`; `ld.c:535` | waiters burn a core (or a time slice under load) while another thread is in `open`/`read` or lexing; unbounded | wait on the entry (park) or lex another queued file meanwhile | S | low | `vcc-diff.py batch --bin-dir build/src-tsan`; not measured (7) |
| Q-SCHED-09 | L3, L8 | global state | 2.3 first row; set/reset by hand at `ppdriver.c:1760-1784,1953`, `vccd.c:1354,1827` | a job cannot continue on, or be helped by, another thread; every new entry point must know the list | one `vjob` context struct reached from `pp`/`cc`/`ecx`; TLS holds one pointer at most | M | medium: hot paths (`pp_diag_pc`, `kwmap`) | `vcc-same.py`, `vbench.py gate` |
| Q-SCHED-10 | L2, L3 | pattern | `arena.h:185-218` x7 pools; by hand `ppdriver.c:517-556`, `wasm.c:7-62`; `*_spare` `frontend/pp/setup.c:1057-1076`; thread-local scratch | three idioms for "keep this job's memory for the next"; memory follows the number of threads that ever ran a job (D: +23 % for 14 idle workers) | one per-worker context owned by the scheduler, handed to the job; LIFO wake so few workers stay warm | M | low | `VCC_VMREG_CHECK=1` runs, `vcc-same.py`, D's RSS |
| Q-SCHED-11 | L3 | thread | `lex.c:1992` vs none elsewhere; `lextest.c:133` | read-ahead, the only speculative work, has the highest class; nothing puts the waited-on path on P cores or bulk work on E cores | QoS set by the scheduler per kind of work (6.1) | S | low; needs measuring | A/B of a kernel build |
| Q-SCHED-12 | L3 | thread | CAS-and-discard sites of 2.2; 3.2 A (+4 % instructions at 6 threads, cold) | duplicate header/snapshot work at the start of a build, when every job misses the same entries | "in progress" entries a job can wait on or skip (as `dircache.c:107` does), driven by the scheduler | M | medium | `vcc-diff.py batch --cmp`, TSan batch; A's instruction count |
| Q-SCHED-13 | L3 | thread | `vccd.c:1991`, :2033 (50 ms tick), :198, :205, :219 (1 ms), :1859 (5 ms); `shim.c:146,301` (10 ms) | 26 wakeups a second for an idle daemon; restart and drain are polling loops | wake the accept loop by an event (kqueue user event, eventfd); condition variables for drain | S | low | `vccd-test.py` (restart, SIGTERM) |
| Q-SCHED-14 | L3 | thread | `dstore.c:336-380`; `ld.c:104`; `codegen/tu.c:25-27` | a slow reader of `--vccd-diagnostics` stops every job that reports a diagnostic | copy under the lock, write outside | S | low | `vccd-test.py` |
| Q-SCHED-15 | L3, L8 | thread | `ppdriver.c:1845` (`getenv`), `vpp.c:71-79` + `ppd_ph` | `VCC_THREADS` cannot be set per build under vccd; `--stats` and `vbench` do not count instructions of emit threads (B: "emit" fell from 1.87 G to 1.19 G) | the count from the scheduler; counters summed per job | S | low | `vbench.py gate` unchanged at 1 thread |
| Q-SCHED-16 | L3 | stale doc | `vccd.c:4-7` ("pool of I/O worker threads"), :131-137 ("jobs of the same group never wait", but :253 makes them wait during a flush), `codegen/tu.c:81` (a comment left above `dbg_str`), PLAN-fas2.md:160-173 | the text describes another design | rewrite with the scheduler's opening comment | S | none | review |

Q-SCHED-01 in detail.  `gen_enter` treats a request whose process group
differs from the last one's as a new build: it sets `gen_flushing`, waits for
`gen_running == 0`, then empties the token, directory, diagnostics and include
caches (`:275-278`; old entries stay allocated, so memory only grows).  While
it waits, every other request, of either group, waits at `:253`.  Two makes
that interleave therefore alternate: drain, flush, compile a little cold,
drain, flush.  The harnesses never see it because each starts a private vccd;
the default socket is per user (`protocol.h:54-69`), so two builds started
with the same vcc share one server (a shim of another build of vcc is
refused, `shim.c:306`).

Q-SCHED-06 and Q-SCHED-02 belong together: the worker count is "two per core"
because workers block, and because it is two per core an allmod build has 17
jobs in flight on 10 cores (F).  Moving the blocking calls out is what allows
a CPU pool of exactly the core count.

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `worker` (static, four times) | `codegen/tu.c:208`, `ld.c:263`, `vpp.c:56`, `lextest.c:131`; `worker_main` `vccd.c:1784` | five thread bodies | gone with the mechanisms; the scheduler has the only one |
| `job` | `ld.c:247` (the parallel-for slot), `vpp.c:35` (a TU), `dstore_job`, `vjob_*` (a compile's escape) | four meanings | "job" = one request (compile, link); "task" = a unit the scheduler runs; `pfor` slot becomes internal |
| `pool` | `ld.c:244` (threads), `vmpool`/`ecxpool`/`rpool`/`erpool`/`mfpool`/`pppool`/`ccpool`/`bufpool`/`wpool` (memory sets) | threads and memory | "pool" for threads only; memory sets: "cache" or "regions" |
| `gen` | `vccd.c` `gen_*` (a build), `ld.c:175` `arena_gen`, :253 `job.gen` (a wake-up counter), `W->gen` (wasm bindings), `vir_ctx.gen` | five meanings | `build_*` in vccd; `wake_seq` |
| `E->mu`, `cmu`, `emu`, `dmu`; `dmu` again in `dstore.c:46` | `emitimpl.h:201-222` | queue, chunks, errors, debug files; diagnostics store | `queue_mu`, `chunk_mu`, `diag_mu`, `dbgfile_mu` |
| `replay` | `codegen/tu.c:287` | encodes the items into the object (third meaning, with `frontend/c/replay.c`, `pp` replay) | `encode_items` |
| `handle`/`handle1` | `vccd.c:1350,1367` | fixdep wrapper / classify and run a request | `run_request`, `dispatch` |
| `-j`, `--threads`, `VCC_THREADS`, `VLEX_IO_THREADS` | vccd, vpp, lextest; vlink; emit; lexer | four spellings of one setting | `-j N` and `VCC_JOBS` everywhere, read in one place |
| `nthreads` | `ld.c:242` (pool size wanted), `cc_opts.nthreads` (`include/frontend/c/cc.h:319`) | | the scheduler's |
| "I/O worker threads" | `vccd.c:5` | the compile workers | Q-SCHED-16 |

## 6. Decisions needed

### 6.1 Requirements for one scheduler, from the evidence

1. **Kinds of work.**  (a) a job: one request, 0.1 ms (probe) to 100 ms
   (compile) to seconds (allmod link), 20 MB to 400 MB of working memory;
   (b) fork-join items inside a job that spawn more items (emit: `claim_ref`);
   (c) index-parallel loops inside a job (23 in the linker), most too small to
   split (E); (d) blocking I/O with no CPU: reads ahead of need, output writes,
   fixdep, request receive and reply; (e) child processes
   (`VCC_DELEGATE`, `--ld`, later LTO or plugins) that count against the
   budget while they run but need no thread of the pool.
2. **One budget.**  Runnable CPU threads <= cores granted; (b) and (c) use
   only cores that jobs leave idle (C against B: 5.8x alone, nothing when
   full).  No mechanism may create threads of its own.
3. **Priorities.**  The path make waits on comes first: a serial phase
   (config probes, the final link: half of a build has nothing else in flight,
   F) gets the P cores and may spread over all of them; independent TUs of a
   full build are throughput work; read-ahead and other speculation are below
   both (today they are above, Q-SCHED-11).
4. **Thread count from the machine.**  macOS: P and E counts
   (`hw.perflevel0/1`), QoS class per kind of work rather than pinning.  Linux:
   `sched_getaffinity` and cgroup `cpu.max`, since CI runs in a container.
   One override (`-j`, `VCC_JOBS`).
5. **Scale down.**  The machine is shared (load 5-8 during this review).
   Under vccd the number of jobs is make's choice, so "down" means: nested
   work shrinks to zero first; idle workers sleep (no ticks, Q-SCHED-13) and
   their memory stays with few warm workers (LIFO wake, Q-SCHED-10); the
   budget can be lowered at run time.  Whether to look at system load is a
   decision (6.3).
6. **Per-worker and per-job state** with a defined lifetime (Q-SCHED-09,
   -10), one stack size (Q-SCHED-07), per-job accounting that follows the work
   to other threads (Q-SCHED-15).
7. **Two concurrent builds** must not interfere beyond sharing cores
   (Q-SCHED-01).
8. Deterministic output whatever the thread count (kept today by sorting
   chunks, `tu.c:532`, and by `vcc-ctests.py`'s 1-against-8 check).
9. The same scheduler in `vpp --batch`, `vlink` standalone and vccd, so tests
   run what production runs.

### 6.2 Scheduler shape (PLAN-quality decision 5)

| Shape | For | Against |
|---|---|---|
| **A. One pool, a Chase-Lev deque per worker** (PLAN-fas2.md:160-173) | fits (b) exactly: items spawned by a worker go on its own deque, idle workers steal, so C's speed-up appears only when cores are idle; one budget by construction; jobs, items and loop chunks are all tasks | needs Q-SCHED-09 and -10 done first (a stolen task runs on another thread); blocking calls must all be out (Q-SCHED-06) or a blocked worker strands its deque; the most code to get right under TSan; the job loop already scales (A), so the gain is in nesting, not in jobs |
| **B. One job queue + one shared parallel-for/spawn facility** with a global count of running threads | small: today's vccd queue and `pfor`, made common, plus "help while waiting"; emit becomes a parallel loop over a growing list; a job never migrates, so thread-local job context can stay for now | two disciplines to explain; nested work needs a rule for borrowing idle workers (a central counter, contended at high core counts); no priority between jobs unless added |
| **C. Keep the mechanisms, add one token limit** (a semaphore every mechanism takes before it runs a thread) | days, not weeks; removes oversubscription (F, B) at once; a jobserver client fits here | six mechanisms remain to maintain; a token held by a thread blocked in I/O is wasted; does nothing for lifetime, leaks, stacks or names |

Recommendation: B as the target of the quality plan, built so that its task
interface is A's (spawn, parallel-for, wait-and-help), with C's counter as the
first step and `vpp --batch` as the pilot (PLAN-quality P4 already names it).
Go to deques only if a measurement on a larger machine shows the central queue
contended.  Reason: measurements A to F say the job level is fine and the
losses are in nesting, blocking and lifetime, which shape B fixes; shape A's
extra cost buys the case of measurement C (a lone large TU), which B's helping
also serves.

### 6.3 Other decisions

1. **Does vccd obey make?**  Today make's `-j` alone decides (3.1).  Options:
   (a) vccd keeps its own budget = cores and queues what exceeds it (make's
   `-j` may then be larger than the core count, which F shows costs nothing in
   the compile phase and avoids the 1.7-3.5x in-flight time); (b) vccd becomes
   a jobserver client for nested work only; (c) nothing.  Recommended: (a),
   and measure (b) later; "do not replace make" holds for all three.
2. **Two builds in one vccd** (Q-SCHED-01): a cache per build, validation by
   file identity, or one vccd per build tree.  Needs the owner's view of how
   builds are meant to share a server.  Recommended: no global flush while
   another group has jobs; validate entries by (dev, ino, mtime, size).
3. **Is per-function parallel emit a product feature?**  It is off by default
   and unused in every build script; C says it is worth 2x on a large TU with
   VIR.  Keep it (then it must come from the pool, Q-SCHED-05) or delete the
   queue and the four mutexes.  Recommended: keep, on the pool.
4. **I/O threads: how many and for what?**  Reads only (today), or also
   output writes and fixdep (PLAN-fas2).  Needs the measurement in 7.
5. **Load awareness**: fixed budget = granted cores, or shrink when the
   machine is busy.  Recommended: fixed budget plus QoS classes (the OS then
   does the sharing); no polling of the load average.
6. **Linker default threads**: until the pool exists, 4 instead of all P
   cores, and serial below a size threshold (E).  A change of speed only.

## 7. Not covered

- Not run: a kernel build (not allowed here).  F uses old logs; the plan
  should make it a controlled A/B: `bench/vccd-kbuild.sh` at `-j 6, 10, 14, 20`
  with vccd `-j 10` against the default, recording the compile phase, the sum
  of time in flight against vccd's CPU, and `sample` of blocked workers
  (`bench/vccd-sample.py`).
- Not measured: time lost in `vlex_get`'s spin (Q-SCHED-08), the share of a
  worker's time in the blocking calls of 2.4, and whether writing `.o` on I/O
  threads gains anything (count `write_file` time per job with `vperf`, then
  try it); the effect of QoS classes on P/E placement (needs `powermetrics`
  or `sample`, and a quiet machine).  All timings above were taken at load
  5-8; ratios are safer than absolute numbers.
- Not verified on Linux: thread counts in a container, default stacks (8 MB
  there, so Q-SCHED-07 differs), epoll paths.  wasm builds (`contrib/wasm/`) have
  no threads; not looked at beyond `lex.c:2041`.
- Read by lock and thread site only, not in full: `ld/ld.c`, `frontend/lex/lex.c`
  outside :1525-2064, `driver/ppdriver.c` outside the parts cited,
  `frontend/c/replay.c`, `frontend/pp/hcentry.c`, `utils/ar.c`, `utils/rsh.c`, `ld/objcopy.c`.
  The duplicate scan (`bench/quality.py --clones`) was not run for this area:
  the five thread bodies and three queues are the same idea written
  differently, which it cannot see.
- `contrib/phase1/` (vroomd) and the fuzz drivers were not read.
