# Work order: the shared scheduler

One facility that every part of vcc runs its threads on, replacing the
private mechanisms of `doc/review/quality-scheduling.md` (finding ids
`Q-SCHED-NN`; its section 2.1 lists the six).  The owner's aim: "improved
task/thread dispatching so we do not waste resources and can scale up and
down to more cpus if we have to".  Names follow `doc/STYLE.md`; the shape is
the review's recommendation 6.2 B (one job queue plus one shared
parallel-for, with a task interface that deques could later sit under).

**Builds on** PR #142 (no cache flush between live process groups,
Q-SCHED-01) and PR #157 (vlink's small parallel loops stay on the caller,
Q-SCHED-03).  Their behaviour is kept: the probe rule of #157 becomes a flag
of the shared loop.

**Owns** `src/port/task.c`, `src/include/port/task.h`,
`src/test/port/task-test.c`, `bench/sched-measure.py`, and in the files it
edits only the threading code: `src/ld/thread-pool.c` (deleted in stage 1),
the batch loop of `src/bin/vpp/vpp.c`, the round loop of `src/codegen/tu.c`
(`emit_tu1`, `worker`), `src/server/{vccd.c,worker.c,work-queue.c,spawn.c,
forward.c}` and the thread-related lines of `vccd-internal.h`.  The driver
and server work order (`WORK-driver-server.md`) owns those server files for
its split and renames; the two orders touch disjoint lines, and whichever
lands second merges.

## 1. Design

### 1.1 Kinds of work

| Kind | Example | Length | How it runs |
|---|---|---|---|
| (a) job | one vccd request; one TU of `vpp --batch` | 0.1 ms to seconds | `vtask_spawn` (vccd) or one item of a `vtask_parallel_for` (`vpp --batch`); never moves to another thread (its context is in thread-locals, Q-SCHED-09) |
| (b) fork-join items that spawn more | emit: one function, object or string; items enqueue items (`claim_ref`) | 10 us to 10 ms | the emitter keeps its own item queue; `vtask_parallel_for(nslots, ...)` runs the queue's drain loop on up to `nslots` threads, slot 0 on the caller |
| (c) index-parallel loop | the linker's 23 phases | most items < 1 us, some loops 100 ms | `vtask_parallel_for(n, width, fn, arg, VTASK_SMALL_ITEMS)`: the caller probes for 30 us and wakes only as many helpers as the rest of the work keeps busy for 60 us each (#157) |
| (d) blocking I/O without CPU | request receive, reply, `waitpid`, the backend socket, a second link waiting for `ldlock`, output writes | | inside `vtask_blocking_begin/end`: the thread gives its budget slot back while it waits (1.4) |
| (e) spawned child | `VCC_DELEGATE`, `--ld PROG` | the child's life | the waiting thread is in a blocking region, so the child uses the core the pool gave back; no thread of the pool runs it |

The lexer's read-ahead threads (`frontend/lex/lex.c`, `io_thread`) stay as
they are in this order: they never use the CPU budget (they sleep in
`open`/`read`), and moving them is a change of who owns the file bytes (each
I/O thread's arena), not of scheduling.  See 3.

### 1.2 The API (`src/include/port/task.h`)

```c
/* the machine and the budget */
typedef struct vtask_cpus {
	int performance;	/* P cores (macOS hw.perflevel0); all granted cores elsewhere */
	int efficiency;		/* E cores (macOS hw.perflevel1); 0 elsewhere */
	int granted;		/* cores this process may use: affinity, cgroup quota, online */
} vtask_cpus;
vtask_cpus vtask_cpus_probe(void);	/* once; cached */
int  vtask_budget(void);		/* CPU threads that may run at once */
void vtask_budget_set(int n);		/* any time; n <= 0: the default (1.3) */
int  vtask_jobs_from_environment(int fallback);	/* VCC_JOBS, else fallback */
void vtask_stack_size_set(size_t bytes);	/* before the first thread; 0: as main's */

/* work */
typedef void vtask_item_fn(void *arg, size_t index);
typedef void vtask_fn(void *arg);
enum { VTASK_SMALL_ITEMS = 1 };	/* probe first; wake helpers by measured cost */
void vtask_parallel_for(size_t n, int width, vtask_item_fn *fn, void *arg, unsigned flags);
void vtask_spawn(vtask_fn *fn, void *arg);	/* a job, FIFO, run when the budget allows */
void vtask_wait_idle(void);			/* until nothing spawned is queued or running */

/* blocking */
void vtask_blocking_begin(void);	/* about to wait: the slot goes to queued work */
void vtask_blocking_end(void);

/* the calling thread */
int  vtask_worker_index(void);		/* 0..threads-1 on a pool thread, -1 elsewhere */
```

Rules the header's opening comment states:

- `vtask_parallel_for` calls `fn(arg, i)` exactly once for each `i` in
  `[0, n)`, on the caller and on at most `width - 1` helpers (`width <= 0`:
  the budget), and returns when all have returned.  Items are handed out by
  one atomic index in increasing order, so a loop whose items write
  disjoint outputs gives the same bytes whatever the thread count.
- Helpers are only idle pool threads, and only while the number of threads
  running CPU work is below the budget: **one budget, never a pool inside a
  pool**.  A loop inside a job inside a loop (an emit round inside a `vpp
  --batch` TU, a link phase inside a vccd job) therefore gets helpers only
  when cores are free, and runs on its caller alone when they are not (the
  review's measurement B: nesting gains nothing on a full machine).
- A loop never waits for a helper that has not started: the caller takes
  every item no helper took.  So a loop always finishes, even with no pool
  thread at all, and a loop may be called from inside another loop's item.
- `vtask_spawn` queues a job; a pool thread runs it when the running count
  is below the budget.  Jobs run in FIFO order of spawning.  Used by vccd
  only (stage 3).
- Threads are created lazily, up to the budget plus the number of threads
  currently inside a blocking region, all with one stack size; they live as
  long as the process and are never joined.  Idle threads sleep on a
  condition variable (no polling, no tick) and are woken LIFO, so few
  threads stay warm and the memory of the rest is not touched (Q-SCHED-10).

### 1.3 How many threads: the budget

`vtask_cpus_probe` (one place, replacing `vccd.c`'s `2 * _SC_NPROCESSORS_ONLN`
and `ld.c`'s perflevel probe):

- **macOS**: `performance` = `hw.perflevel0.physicalcpu`, `efficiency` =
  `hw.perflevel1.physicalcpu` (0 when the sysctl is missing: Intel, one
  level), `granted` = `hw.activecpu`.
- **Linux**: `granted` = the smaller of `CPU_COUNT(sched_getaffinity)` and
  the cgroup quota rounded up (`/sys/fs/cgroup/cpu.max` for v2,
  `cpu.cfs_quota_us / cpu.cfs_period_us` for v1, the first that exists);
  `performance` = `granted`, `efficiency` = 0.  A CI container with a quota
  of 4 on a 64-core host gets 4.
- Anything else, or a probe that fails: `sysconf(_SC_NPROCESSORS_ONLN)`,
  else 4.

The **budget** is the number of threads that may run CPU work at once.
Default `granted`; an explicit `-j N` of a tool, else `VCC_JOBS=N` in the
environment, overrides it (`vtask_budget_set`).  Per tool:

| Tool | Budget | Why |
|---|---|---|
| `vpp --batch` | `-j N`, else `VCC_JOBS`, else 6 (unchanged default: the harnesses' numbers stay comparable) | the TUs and any nested emit share it |
| `vlink` | `--threads N`, else `VCC_JOBS`, else P cores (unchanged) | the review measured no gain past 4 on the big links; the small-items rule keeps small links serial |
| `vpp` (one TU) | `VCC_THREADS` for the emitter's width, as today | |
| vccd, old mode | as today (2 x online workers, or `-j`); the pool serves only links and emit helpers, budget `granted` | |
| vccd, `--shared-pool` | `-j N`, else `VCC_JOBS` (read at server start), else `granted` | compile jobs, emit items and link phases all draw from it |

**Shared machine rule**: the budget is fixed at what the process is granted
(affinity, quota) or told (`-j`, `VCC_JOBS`); vcc does not poll the load
average.  Scaling down is by structure: nested work (emit helpers, link
helpers) takes only idle cores, so on a full machine it shrinks to zero
first; idle threads sleep; the budget can be lowered at run time.  Making
the OS prefer P or E cores per kind of work (QoS classes, Q-SCHED-11) needs
a quiet machine to measure and is left for later (3).

### 1.4 Blocking work

A job's context lives in thread-locals (Q-SCHED-09: about 75 of them), so a
job cannot hand a blocking call to another thread and continue elsewhere.
What leaves the CPU thread is therefore its **budget slot**, not the call:

```c
vtask_blocking_begin();	/* running--; wake an idle thread (or create one) if a job is queued */
n = waitpid(...);
vtask_blocking_end();	/* running++; may exceed the budget for a moment */
```

A thread that comes back from a blocking region while the budget is full
continues (it holds a job's memory and is part way through); the overshoot
corrects itself because no pool thread takes a new job while running >=
budget.  This is the hand-off of Go's scheduler and TBB's arenas, and it is
what makes a pool of exactly the core count possible (Q-SCHED-06): today's
2 x cores workers exist only to hide blocked ones.

Wrapped in stage 3, in the shared-pool mode only: the request receive
(`poll` up to 2 s and `recvmsg`, a silent client), the status reply and
`close`, `spawn`'s `waitpid` (`VCC_DELEGATE`, `--ld`), the backend
`connect`/`read` (`--backend-sock`), the wait for `ldlock` and the Mach-O
link lock (a second link while one runs), and the build-generation wait.
Not wrapped: the lexer's own `open`/`read` of a file no I/O thread reached
(12-40 us; a wake costs as much) and output writes into the page cache;
both are short, and their share of a job is the measurement the review
lists as missing (`bench/sched-measure.py` reports per-job wall against
CPU so that it can be seen).

### 1.5 Per-thread state

Pooled threads outlive a job, where `vpp --batch` and the emitter created
threads per pass or round.  Consequences, by the review's classes (2.3):

- **Job context through TLS** (`vjob_jb`, `ppd_conn`, `cap_fd`, ...): set
  at the job's start and cleared at its end by the code that runs the job,
  as today (`ppd_run`, vccd's worker loop).  The pool adds nothing here and
  runs no job on a thread that has not finished the previous one.  An emit
  or link helper never has a job context (as today): helpers must not call
  `vjob_fail`.
- **Lazily grown scratch** (`opt.c`'s `W`, VIR's allocator, `ld`'s
  `count_locals`): written before read, so nothing to reset; with threads
  that live for the process it is reused instead of leaked per round
  (Q-SCHED-05 fixed by the lifetime, not by a destructor).  The memory a
  thread keeps is bounded by the number of threads, which is the budget.
- **Recycled buffers and arena cursors**: unchanged; `ld`'s arena cursor
  is already reset by generation, so a link helper that ran for an earlier
  link starts on the new arena.
- **Counters**: `ppd_ph` is per thread and read as a delta per job: work
  an emit helper does is not counted in the job (as today, Q-SCHED-15).

The pool provides `vtask_worker_index()` so a caller can keep per-slot state
in an array instead of a thread-local when that is cleaner.

### 1.6 Nested parallelism

One budget, counted in one atomic: threads currently running CPU work
(pool threads in an item or job, plus outside callers inside
`vtask_parallel_for`).  An outside caller (vlink's main thread, vccd's old
workers) always runs its own loop, so the count can exceed the budget by
the number of outside callers; pool threads never do.  A pool thread that
calls `vtask_parallel_for` is already counted and asks only for idle
helpers.  There is no second pool anywhere: after stage 3 the only thread
creation in `src/` outside `port/task.c` is the lexer's read-ahead (3),
`lextest` (a benchmark of exactly that) and vccd's old mode.

### 1.7 Output

Nothing in this order may change an output byte.  The emitter already
sorts its chunks (`tu.c` `chunk_cmp`) and the linker's loops write disjoint
ranges; the proofs below compare outputs across thread counts and against
the base.

## 2. Stages

Each stage is one PR against main, usable alone.  Every stage's proof
includes `bench/vccd-test.py`, `bench/vcc-stress.py --vccd`,
`bench/vccd-cc.py`, the same three with the TSan build for stages 1-3
(`make -C src -j 3 vcc-tsan`; `--bin-dir build/src-tsan`), and
measurements before and after with `bench/sched-measure.py`.

### Stage 0: this document

Proof: review against `quality-scheduling.md` 6.1 (every requirement is
covered here or in 3 with a reason).

### Stage 1: the facility; `vpp --batch` and vlink on it

- `src/port/task.c`, `src/include/port/task.h` as in 1.2-1.3 (spawn and
  blocking regions included, used from stage 3 on); unit test
  `src/test/port/task-test.c` (`make -C src task-test`, run by
  `ci/premerge.sh`): every index once, nested loops, loops from many
  outside threads at once, width 1, budget 1, budget changes at run time,
  spawn ordering, blocking hand-off, the cpu probe's invariants.
- `vpp --batch`: its private threads go; the TUs are one
  `vtask_parallel_for(njobs, j, ...)` with budget `j` (`-j`, else
  `VCC_JOBS`, else 6).  The stack rule (a worker's stack = main's) moves
  into the pool (Q-SCHED-07).
- vlink: `src/ld/thread-pool.c` goes; `pfor` becomes
  `vtask_parallel_for(n, nthreads, fn, NULL, VTASK_SMALL_ITEMS)`; the
  thread-count probe moves to `vtask_cpus_probe` (P cores, unchanged
  default).  `--threads N` now works for every link of a process (the
  frozen pool size of 2.1 row 3 is gone).

Proof: `bench/vcc-sema.py batch`, `bench/vcc-diff.py batch --cmp --kernel`,
`bench/vlink-opts.py`, `VCC_LDCMP=1 bench/vcc-ctests.py --no-cache`, TSan
`bench/vcc-cbatch.py`, the unit test (also under TSan); measurements A
(`sched-measure.py batch`) and E (`sched-measure.py link`) against the base.

### Stage 2: the emitter on the pool

`emit_tu1` creates no threads: its drain loop runs as
`vtask_parallel_for(nslots, nslots, ...)` (slot `i` uses `ctx[i]`; an
unclaimed slot finds the queue empty and returns), so a round costs no
`pthread_create`/`join`, and the optimizer's and VIR's thread-local scratch
lives with the pool's threads instead of leaking per round (Q-SCHED-05).
`VCC_THREADS` keeps its meaning (the number of slots); under vccd and
`vpp --batch` the slots beyond what is idle run on the caller.

Proof: the `vcc-threads-same` cases of `bench/vcc-ctests.py` (`-S` with
`VCC_THREADS=1` and `8` byte-identical), `bench/vcc-ctests.py --no-cache`
with `VCC_THREADS=8` against `=1`, the TSan suite (`ci/premerge.sh tsan`'s
threaded emitter tests), measurements B (`sched-measure.py batch --emit
4`: resident size per `--warm` pass must stay flat) and C
(`sched-measure.py emit`: one large TU at 1/2/4/8).

### Stage 3: vccd on one budget, behind one switch

`vccd --shared-pool` or `VCCD_SHARED_POOL=1` in vccd's environment at
start (off by default in this stage; the default flips in a later PR after
the kernel A/B):

- no `worker_main` threads: the accept thread `vtask_spawn`s each accepted
  socket as a job; budget `-j`, else `VCC_JOBS`, else `granted` (not 2 x);
- the job's blocking parts of 1.4 inside blocking regions, so a compile
  waiting for its client, a child or a lock does not hold a core;
- links and emit helpers draw from the same pool (stages 1-2);
- SIGTERM's drain waits on the pool (`vtask_wait_idle`) instead of polling.

Without the switch vccd behaves exactly as before the stage.

Proof: the stage-wide list with and without the switch (`VCCD_ARGS`/the
environment), `bench/vccd-cc.py` with `-j` 2/6/20 against the switch,
measurement D (`sched-measure.py vccd`: 168 TUs, 6 clients, both modes),
and the kernel A/B below.

**Kernel A/B** (needs a quiet machine; each makes its own fresh build tree,
`bench/kbuild.py` removes only the directory it is given):

```
nice -n 10 bench/vccd-kbuild.sh work/build-sched-a -j 6
VCCD_ARGS=--shared-pool nice -n 10 bench/vccd-kbuild.sh work/build-sched-b -j 6
python3 bench/sched-measure.py kbuild-log work/build-sched-a.vccd.log work/build-sched-b.vccd.log
python3 bench/boot-test.py --kernel work/build-sched-b/arch/arm64/boot/Image --timeout 60
```

Repeat at `-j 14` (vccd-kbuild's default) and `-j 20`: the shared pool is
meant to matter most where make's `-j` exceeds the core count.

### Decision for the owner (stage 3)

Should vccd cap concurrent compile jobs below make's `-j`?  (a) yes: the
budget is the core count and what exceeds it queues (the review's 6.3.1
recommendation: make's `-j 14` and above then costs nothing in time in
flight); (b) no: run whatever make sends, as today; (c) a jobserver client
(make's tokens decide).  This order implements (a) behind the switch, so
the A/B above measures it; (c) is left for later (3).

## 3. Left for later, and why

- **Work stealing** (Chase-Lev deques, PLAN-fas2.md 9): the review's
  measurements put the losses in nesting, blocking and lifetime, which this
  shape fixes; the job level already scales (5.1x on 6).  A stolen task
  runs on another thread, which needs the job context out of thread-locals
  first (Q-SCHED-09).  The API (spawn, parallel-for, wait-and-help) is the
  one deques would serve, so the change stays inside `port/task.c`.  Do it
  when a measurement on a larger machine shows the central lock contended.
- **Jobserver client**: make already bounds the requests in flight; a
  jobserver would only bound vccd's nested work, which the budget already
  bounds.  It matters when vccd serves several makes at once with different
  `-j`; decide after the A/B.
- **QoS classes per kind of work** (Q-SCHED-11): needs `powermetrics` or
  `sample` on a quiet machine.
- **The lexer's read-ahead on the facility**: an I/O lane in `port/task.c`
  that it and the request receive share; changes who owns the file bytes.
- **Job context out of TLS** (Q-SCHED-09) and **per-job counters that
  follow helpers** (Q-SCHED-15): the driver/server work order's struct.
- **In-progress cache entries** (Q-SCHED-12), the spin in `vlex_get`
  (Q-SCHED-08), the idle tick of the accept loop (Q-SCHED-13): separate,
  small, measured on their own.

## Report

Per stage: the PR, the proof commands with results, measurements before and
after with the machine's load, and anything this order got wrong.
