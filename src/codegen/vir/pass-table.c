/*
 * The VIR builder's pass table and -fvir-passes (pass-table.h).
 *
 * -fvir-passes=ITEM[,ITEM...], read once per TU (a test aid, not an optimization option):
 *   -NAME          leave pass NAME out (a required one: error)
 *   only=A+B+...   run exactly these, in table order, plus the required ones
 *   +NAME          run pass NAME, a row that is off unless asked for (PASS_OFF_BY_DEFAULT)
 *   verify         after every pass: the pre-IR check (preir-check.c), or vir_verify
 *                  after canon (VIR before canon is not in the layout vir_verify checks,
 *                  SPEC-vir 2.3, so to-vir's output is checked after canon: verify=to-vir
 *                  checks there); a failure prints
 *                  "vcc vir-verify: pass=NAME function=F: MESSAGE" and the function
 *                  falls back (reason verifier_rejected)
 *   verify=A+B     ... after the passes named
 *   analyses       a VIR pass's `keeps` row is checked against a fresh computation
 *                  (vir_pass_context.check_analyses); verify asks for it too
 *   time           per pass: runs, runs that changed something, instructions (vperf,
 *                  read only when set); printed with -fvir-stats (virbuild_statistics_print):
 *                    vcc vir-pass: pass=NAME runs=N changed=N instructions=N [changes=N] [checked=N]
 *                  changed is `-` for a pass that does not say; changes=N (rows of VIR
 *                  passes only) counts the instructions the pass rewrote or removed; the rows @build (the
 *                  walk and every pass, to-vir and canon included) and @lower (the
 *                  target's lowering) are the totals a pass's share is taken of
 * A row with a `vir` entry runs that pass of the registry (vir/pass.h) on V->f.
 * An unknown pass or item fails the job with the list of passes.
 */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"
#include "vectorize.h"
#include "pass-table.h"
#include "port/performance-counters.h"

/* the passes without a "changed" result, and the ones on VIR (failure: virbuild_fail, as virbuild_finish did) */
static bool virbuild_pass_thread(codegen_context *e) { virbuild_empty_blocks_thread(e); return false; }
static bool virbuild_pass_rpo(codegen_context *e) { virbuild_block_order_compute(e); return false; }
static bool virbuild_pass_preds(codegen_context *e) { virbuild_block_predecessors_compute(e); return false; }
static bool virbuild_pass_seal(codegen_context *e) { virbuild_blocks_seal(e); return false; }
static bool virbuild_pass_dead_params(codegen_context *e) { virbuild_parameters_dead_remove(e); return false; }
static bool virbuild_pass_simplify(codegen_context *e) { virbuild_function_simplify(e); return false; }
static bool virbuild_pass_vectorize(codegen_context *e) { virbuild_function_vectorize(e); return false; }
static bool virbuild_pass_split(codegen_context *e) { virbuild_critical_edges_split(e); return false; }
static bool virbuild_pass_slot_uses(codegen_context *e) { virbuild_slot_uses_compute(e); return false; }

static bool virbuild_pass_to_vir(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;

	if (!virbuild_function_to_vir(e))
		virbuild_fail(e, V->failure_reason ? V->failure_reason : VIRBUILD_FAILURE(build_failed));
	return true;
}

static bool virbuild_pass_canon(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;

	if (!vir_canon(&V->f))
		virbuild_fail(e, V->failure_reason ? V->failure_reason : VIRBUILD_FAILURE(build_failed));
	return true;
}

#define O VIRBUILD_FACT_ORDER
#define P VIRBUILD_FACT_PREDS
#define D VIRBUILD_FACT_DOMS
#define U VIRBUILD_PASS_FLAG_CHANGE_UNKNOWN

/* one row per step of the pipeline, in its order (quality-vir-build.md 3.1); the file is the pass's */
static const virbuild_pass virbuild_passes[] = {
	/* name                 run                    needs   makes  keeps      flags, vir (the registry entry of a VIR pass's row: none yet) */
	{ "thread",             virbuild_pass_thread,       0,      0,     0,         U, NULL },				/* cfg.c */
	{ "rpo",                virbuild_pass_rpo,          0,      O,     O,         U | VIRBUILD_PASS_FLAG_REQUIRED, NULL },		/* cfg.c */
	{ "preds",              virbuild_pass_preds,        O,      P,     O | P,     U | VIRBUILD_PASS_FLAG_REQUIRED, NULL },		/* cfg.c */
	{ "seal",               virbuild_pass_seal,         O | P,  0,     O | P | D, U | VIRBUILD_PASS_FLAG_REQUIRED, NULL },		/* pipeline.c */
	{ "promote",            virbuild_slots_promote,            O | P,  0,     O | P | D, 0, NULL },				/* slot-promotion.c */
	{ "trivial",            virbuild_parameters_trivial_remove,            P,      0,     O | P | D, VIRBUILD_PASS_FLAG_FIXED_POINT, NULL },		/* block-parameters.c */
	{ "dead-params",        virbuild_pass_dead_params,  O,      0,     O | P | D, U, NULL },				/* block-parameters.c */
	{ "simplify",           virbuild_pass_simplify,     O | P,  0,     O | P | D, U, NULL },				/* simplify.c */
	/* simplifying can free a slot's address (an inlined call's pointer argument parked as an
	   integer comes back as i2p(p2i $slot)): promote again, then simplify what it forwards */
	{ "aggregate-copy",     virbuild_aggregate_copies_forward, O | P, 0, O | P | D, 0, NULL },
	{ "promote-again",      virbuild_slots_promote,            O | P,  0,     O | P | D, 0, NULL },				/* slot-promotion.c */
	{ "trivial-again",      virbuild_parameters_trivial_remove,            P,      0,     O | P | D, VIRBUILD_PASS_FLAG_FIXED_POINT | VIRBUILD_PASS_FLAG_IF_PREVIOUS_CHANGED, NULL },
	{ "dead-params-again",  virbuild_pass_dead_params,  O,      0,     O | P | D, U | VIRBUILD_PASS_FLAG_IF_PREVIOUS_CHANGED, NULL },
	{ "simplify-again",     virbuild_pass_simplify,     O | P,  0,     O | P | D, U | VIRBUILD_PASS_FLAG_IF_PREVIOUS_CHANGED, NULL },
	{ "condition-merge",    virbuild_conditions_merge,          O | P,  0,     O | P,     VIRBUILD_PASS_FLAG_THEN_TRIVIAL, NULL },		/* condition-merge.c */
	{ "if-conversion",      virbuild_if_convert,             O | P,  0,     O | P,     VIRBUILD_PASS_FLAG_THEN_TRIVIAL, NULL },		/* if-conversion.c */
	/* (loop-constants, virbuild_loop_constants_pin, runs inside vectorize, per loop: a row of its own needs vectorize.c) */
	{ "vectorize",          virbuild_pass_vectorize,    O | P,  0,     O | P,     U | VIRBUILD_PASS_FLAG_VECTOR_GATE, NULL },	/* vectorize.c */
	/* required: VIR's block parameters take no critical edges (left out, the verifier rejects 10 741
	   functions of the suites and 31 vcc tests miscompile, as -O2 does not verify) */
	{ "split",              virbuild_pass_split,        O | P,  0,     O | P,     U | VIRBUILD_PASS_FLAG_REQUIRED, NULL },		/* cfg.c */
	{ "slot-uses",          virbuild_pass_slot_uses,    O,      0,     O | P | D, U | VIRBUILD_PASS_FLAG_REQUIRED, NULL },		/* pipeline.c */
	{ "to-vir",             virbuild_pass_to_vir,       O,      0,     O | P | D, VIRBUILD_PASS_FLAG_REQUIRED | VIRBUILD_PASS_FLAG_ON_VIR, NULL },	/* pipeline.c, to-vir.c */
	{ "canon",              virbuild_pass_canon,        0,      0,     O | P | D, VIRBUILD_PASS_FLAG_REQUIRED | VIRBUILD_PASS_FLAG_ON_VIR, NULL },	/* vir/vir.c */
};
#define VIRBUILD_PASS_COUNT ((uint32_t)(sizeof virbuild_passes / sizeof *virbuild_passes))
_Static_assert(sizeof virbuild_passes / sizeof *virbuild_passes <= 32, "virbuild_pass_switch keeps a bit per row");

#undef O
#undef P
#undef D
#undef U

VPRIV const virbuild_pass *virbuild_pass_list(uint32_t *n)
{
	*n = VIRBUILD_PASS_COUNT;
	return virbuild_passes;
}

/* ---------------- the switch ---------------- */

static uint64_t virbuild_pass_read_cost;	/* instructions of a vperformance_now pair (time) */
static _Thread_local uint64_t virbuild_pass_overhead;	/* instructions this thread spent timing passes, not yet taken */
static pthread_once_t virbuild_pass_read_cost_once = PTHREAD_ONCE_INIT;

/* the job ends: a message and, where it helps, the passes there are */
static __attribute__((noreturn)) void virbuild_pass_switch_error(const char *what, const char *s, size_t n)
{
	char msg[1024];
	size_t k = (size_t)snprintf(msg, sizeof msg, "-fvir-passes: %s %.*s (passes:", what, (int)n, s);

	for (uint32_t i = 0; i < VIRBUILD_PASS_COUNT && k < sizeof msg; i++)
		k += (size_t)snprintf(msg + k, sizeof msg - k, " %s%s", virbuild_passes[i].name,
				      virbuild_passes[i].flags & VIRBUILD_PASS_FLAG_REQUIRED ? "*" : "");
	if (k < sizeof msg)
		snprintf(msg + k, sizeof msg - k, "; * required)");
	vjob_fail(msg);
}

static uint32_t virbuild_pass_switch_find(const char *s, size_t n)
{
	for (uint32_t i = 0; i < VIRBUILD_PASS_COUNT; i++)
		if (strlen(virbuild_passes[i].name) == n && !memcmp(virbuild_passes[i].name, s, n))
			return i;
	virbuild_pass_switch_error("unknown pass", s, n);
}

/* A+B+C (to `end`) as row bits */
static uint64_t virbuild_pass_switch_names_parse(const char *s, const char *end)
{
	uint64_t m = 0;

	while (s < end) {
		const char *q = memchr(s, '+', (size_t)(end - s));

		if (!q)
			q = end;
		m |= 1ull << virbuild_pass_switch_find(s, (size_t)(q - s));
		s = q + 1;
	}
	return m;
}

static const char *virbuild_fact_name(uint32_t f)
{
	return f & VIRBUILD_FACT_ORDER ? "the block order" : f & VIRBUILD_FACT_PREDS ? "the predecessor lists" : "the dominators";
}

/* what two reads of the counters cost, taken off every pass's count */
static void virbuild_pass_read_cost_measure(void)
{
	uint64_t best = UINT64_MAX;

	for (int r = 0; r < 8; r++) {
		vperf a = vperformance_now(), b = vperformance_now();

		if (vperformance_subtract(b, a).ins < best)
			best = vperformance_subtract(b, a).ins;
	}
	virbuild_pass_read_cost = best;
}

VPRIV void virbuild_pass_switch_read(virbuild_pass_switch *w, const char *s)
{
	uint64_t req = 0, only = 0, plus = 0, off = 0;
	uint32_t valid = 0;
	char msg[200];

	*w = (virbuild_pass_switch){ 0 };

	for (uint32_t i = 0; i < VIRBUILD_PASS_COUNT; i++)
		if (virbuild_passes[i].flags & VIRBUILD_PASS_FLAG_REQUIRED)
			req |= 1ull << i;
		else if (virbuild_passes[i].flags & PASS_OFF_BY_DEFAULT)
			off |= 1ull << i;
	while (s && *s) {
		const char *e = strchr(s, ',');
		size_t n;

		if (!e)
			e = s + strlen(s);
		n = (size_t)(e - s);
		if (n == 4 && !memcmp(s, "time", 4))
			w->time = true;
		else if (n == 6 && !memcmp(s, "verify", 6))
			w->verify = ~0ull;
		else if (n == 8 && !memcmp(s, "analyses", 8))
			w->analyses = true;
		else if (n > 7 && !memcmp(s, "verify=", 7))
			w->verify |= virbuild_pass_switch_names_parse(s + 7, e);
		else if (n > 1 && s[0] == '+')
			plus |= 1ull << virbuild_pass_switch_find(s + 1, n - 1);
		else if (n > 5 && !memcmp(s, "only=", 5))
			only |= virbuild_pass_switch_names_parse(s + 5, e);
		else if (n > 1 && s[0] == '-') {
			uint32_t i = virbuild_pass_switch_find(s + 1, n - 1);

			if (virbuild_passes[i].flags & VIRBUILD_PASS_FLAG_REQUIRED)
				virbuild_pass_switch_error("cannot leave out the required pass", s + 1, n - 1);
			w->skip |= 1ull << i;
		} else if (n)
			virbuild_pass_switch_error("unknown item", s, n);
		s = *e ? e + 1 : e;
	}
	if (only)
		w->skip |= ~(only | req);
	else
		w->skip |= off & ~plus;	/* the rows that are off unless asked for (+NAME) */
	for (uint32_t i = 0; i < VIRBUILD_PASS_COUNT; i++)	/* a VIR row carries its registry entry's name */
		if (virbuild_passes[i].vir && strcmp(virbuild_passes[i].vir->name, virbuild_passes[i].name)) {
			snprintf(msg, sizeof msg, "the VIR builder's row %s runs the VIR pass %s", virbuild_passes[i].name,
				 virbuild_passes[i].vir->name);
			VBUG(msg);
		}
	for (uint32_t i = 0; i + 1 < VIRBUILD_PASS_COUNT; i++)	/* (to-vir: checked after canon, the next row; a VIR pass's row checks itself) */
		if ((w->verify >> i & 1) && (virbuild_passes[i].flags & VIRBUILD_PASS_FLAG_ON_VIR) && !virbuild_passes[i + 1].vir)
			w->verify = (w->verify & ~(1ull << i)) | 1ull << (i + 1);
	w->skip &= (VIRBUILD_PASS_COUNT < 64 ? (1ull << VIRBUILD_PASS_COUNT) - 1 : ~0ull);
	w->verify &= (VIRBUILD_PASS_COUNT < 64 ? (1ull << VIRBUILD_PASS_COUNT) - 1 : ~0ull);
	w->active = w->skip || w->verify || w->time;
	if (w->time)
		pthread_once(&virbuild_pass_read_cost_once, virbuild_pass_read_cost_measure);
	/* every pass that runs finds the facts it needs, whichever conditional rows run */
	for (uint32_t i = 0; i < VIRBUILD_PASS_COUNT; i++) {
		const virbuild_pass *p = &virbuild_passes[i];
		uint32_t after;

		if (w->skip >> i & 1)
			continue;
		if (p->needs & ~valid) {
			snprintf(msg, sizeof msg, "-fvir-passes: pass %s needs %s, which no pass before it leaves valid", p->name,
				 virbuild_fact_name(p->needs & ~valid));
			vjob_fail(msg);
		}
		after = (valid & p->keeps) | p->makes;
		valid = p->flags & VIRBUILD_PASS_FLAG_IF_PREVIOUS_CHANGED ? valid & after : after;
	}
}

/* ---------------- running a row ---------------- */

/* the function's name, for the messages of the check */
static const char *virbuild_function_name(codegen_context *e, char *buf, size_t n);

/* per TU counters, see below: the instructions a VIR pass rewrote or removed */
static void vir_count_changes(codegen_unit *E, uint32_t row, uint64_t changes);

/* The adapter: a VIR pass's row runs its registry entry on V->f.  A pass cannot fail the function, but
 * a limit reached while it appends is the build's failure (the function goes to the direct emitter), and so
 * is a stale analysis its `keeps` claims (checked with -fvir-passes=verify or =analyses). */
static bool virbuild_pass_vir_run(codegen_context *e, const virbuild_pass *p)
{
	const virbuild_pass_switch *w = &e->unit->vir_pass_switch;
	vir_pass_context ctx = { .budget = VIR_PASS_BUDGET, .checked = w->verify != 0, .check_analyses = w->analyses };
	char err[160];
	bool changed = vir_pass_run(&e->vir_builder->f, p->vir, &ctx, err, sizeof err);

	if (vir_is_failed())
		virbuild_fail(e, VIRBUILD_FAILURE(build_failed));
	if (err[0]) {
		char nm[130];

		fprintf(stderr, "vcc vir-verify: pass=%s function=%s: %s\n", p->name, virbuild_function_name(e, nm, sizeof nm), err);
		virbuild_fail(e, VIRBUILD_FAILURE(verifier_rejected));
	}
	if (ctx.changes && e->unit->vir_statistics)
		vir_count_changes(e->unit, (uint32_t)(p - virbuild_passes), ctx.changes);
	return changed;
}

/* one call of a row's pass */
static bool virbuild_pass_call(codegen_context *e, const virbuild_pass *p)
{
	return p->vir ? virbuild_pass_vir_run(e, p) : p->run(e);
}

/* one row: its pass, to a fixed point if it says so, then virbuild_parameters_trivial_remove if it says so; whether it changed anything */
VPRIV bool virbuild_pass_step(codegen_context *e, const virbuild_pass *p)
{
	bool changed = virbuild_pass_call(e, p);

	if (changed && (p->flags & VIRBUILD_PASS_FLAG_FIXED_POINT))
		while (virbuild_pass_call(e, p))
			;
	if (changed && (p->flags & VIRBUILD_PASS_FLAG_THEN_TRIVIAL))
		while (virbuild_parameters_trivial_remove(e))
			;
	return changed;
}

/* per TU (keyed by its emitter, so codegen-internal.h gains no field): counters per row, plus @build and @lower */
typedef struct virbuild_pass_count { uint64_t runs, changed, instructions, checked, changes; } virbuild_pass_count;
typedef struct virbuild_pass_tu { codegen_unit *E; virbuild_pass_count c[VIRBUILD_PASS_COUNT + 2]; } virbuild_pass_tu;

static pthread_mutex_t virbuild_pass_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static virbuild_pass_tu *virbuild_pass_count_units;
static uint32_t vir_count_n, virbuild_pass_count_capacity;

/* the TU's counters (made on first use); call with virbuild_pass_count_mutex held; NULL: out of memory */
static virbuild_pass_tu *virbuild_pass_counts_find(codegen_unit *E)
{
	for (uint32_t i = 0; i < vir_count_n; i++)
		if (virbuild_pass_count_units[i].E == E)
			return &virbuild_pass_count_units[i];
	if (vir_count_n == virbuild_pass_count_capacity) {
		uint32_t nc = virbuild_pass_count_capacity ? 2 * virbuild_pass_count_capacity : 8;
		virbuild_pass_tu *t = realloc(virbuild_pass_count_units, nc * sizeof *t);

		if (!t)
			return NULL;
		virbuild_pass_count_units = t, virbuild_pass_count_capacity = nc;
	}
	virbuild_pass_count_units[vir_count_n] = (virbuild_pass_tu){ .E = E };
	return &virbuild_pass_count_units[vir_count_n++];
}

static void virbuild_pass_count_add(codegen_unit *E, uint32_t row, bool ran, bool changed, uint64_t ins, bool checked)
{
	virbuild_pass_tu *t;

	pthread_mutex_lock(&virbuild_pass_count_mutex);
	t = virbuild_pass_counts_find(E);
	if (t) {
		t->c[row].runs += ran;
		t->c[row].changed += changed;
		t->c[row].instructions += ins;
		t->c[row].checked += checked;
	}
	pthread_mutex_unlock(&virbuild_pass_count_mutex);
}

static void vir_count_changes(codegen_unit *E, uint32_t row, uint64_t changes)
{
	virbuild_pass_tu *t;

	pthread_mutex_lock(&virbuild_pass_count_mutex);
	t = virbuild_pass_counts_find(E);
	if (t)
		t->c[row].changes += changes;
	pthread_mutex_unlock(&virbuild_pass_count_mutex);
}

/* row @build or @lower ran `instructions`, of which the timing of the passes inside it is taken off */
VPRIV void virbuild_pass_note(codegen_unit *E, uint32_t row, uint64_t instructions)
{
	uint64_t o = virbuild_pass_overhead + virbuild_pass_read_cost;

	virbuild_pass_overhead = 0;
	virbuild_pass_count_add(E, row, true, false, instructions > o ? instructions - o : 0, false);
}

static const char *virbuild_function_name(codegen_context *e, char *buf, size_t n)
{
	const vident *id = &vintern_ents[e->compile_context->syms[e->walk_state->function_symbol].name];

	snprintf(buf, n, "%.*s", (int)id->len, id->s);
	return buf;
}

/* row i with what the switch asks for around it: timing, the check after it; valid: the facts (updated) */
VPRIV bool virbuild_pass_step_instrumented(codegen_context *e, uint32_t i, uint32_t *valid)
{
	const virbuild_pass *p = &virbuild_passes[i];
	const virbuild_pass_switch *w = &e->unit->vir_pass_switch;
	bool count = w->time && e->unit->vir_statistics, check = w->verify >> i & 1, changed;
	vperf t0 = { 0, 0 }, t1 = { 0, 0 };

	if (count)
		t0 = vperformance_now();
	changed = virbuild_pass_step(e, p);
	if (count) {
		uint64_t ins;

		t1 = vperformance_now();
		ins = vperformance_subtract(t1, t0).ins;
		virbuild_pass_count_add(e->unit, i, true, changed && !(p->flags & VIRBUILD_PASS_FLAG_CHANGE_UNKNOWN), ins > virbuild_pass_read_cost ? ins - virbuild_pass_read_cost : 0,
			      check);
	} else if (check && e->unit->vir_statistics)
		virbuild_pass_count_add(e->unit, i, false, false, 0, true);
	*valid = (*valid & p->keeps) | p->makes;
	if (check) {
		char msg[200], nm[130];
		const char *bad;

		if (p->flags & VIRBUILD_PASS_FLAG_ON_VIR) {
			vbuffer b = { 0 };
			uint32_t nerr = vir_verify(&e->vir_builder->f, NULL, &b, 1);
			size_t k = 0;

			while (k < b.n && b.p[k] != '\n')
				k++;
			snprintf(msg, sizeof msg, "%u verifier failures, the first: %.*s", nerr, (int)k, b.p ? (char *)b.p : "");
			free(b.p);
			bad = nerr ? msg : NULL;
		} else
			bad = virbuild_preir_check(e, *valid, msg, sizeof msg);
		if (bad) {
			fprintf(stderr, "vcc vir-verify: pass=%s function=%s: %s\n", p->name, virbuild_function_name(e, nm, sizeof nm), bad);
			virbuild_fail(e, VIRBUILD_FAILURE(verifier_rejected));
		}
	}
	if (count)	/* (the counting and the check are not the build's) */
		virbuild_pass_overhead += vperformance_subtract(vperformance_now(), t1).ins + virbuild_pass_read_cost;
	return changed;
}

/* -fvir-passes=time or verify with -fvir-stats: the TU's lines (virbuild_statistics_print), then its counters go */
VPRIV void virbuild_pass_stats_print(codegen_unit *E)
{
	const virbuild_pass_switch *w = &E->vir_pass_switch;
	virbuild_pass_tu *t;

	if (!w->time && !w->verify)
		return;
	pthread_mutex_lock(&virbuild_pass_count_mutex);
	t = virbuild_pass_counts_find(E);
	if (t) {
		for (uint32_t i = 0; i < VIRBUILD_PASS_COUNT + 2; i++) {
			const virbuild_pass_count *c = &t->c[i];
			const char *name = i < VIRBUILD_PASS_COUNT ? virbuild_passes[i].name : i == VIRBUILD_PASS_ROW_BUILD(VIRBUILD_PASS_COUNT) ? "@build" : "@lower";
			char ch[24];

			if (i < VIRBUILD_PASS_COUNT && (w->skip >> i & 1))
				continue;
			if (i < VIRBUILD_PASS_COUNT && (virbuild_passes[i].flags & VIRBUILD_PASS_FLAG_CHANGE_UNKNOWN))
				snprintf(ch, sizeof ch, "-");
			else
				snprintf(ch, sizeof ch, "%llu", (unsigned long long)c->changed);
			fprintf(stderr, "vcc vir-pass: pass=%s runs=%llu changed=%s instructions=%llu", name,
				(unsigned long long)c->runs, ch, (unsigned long long)c->instructions);
			if (i < VIRBUILD_PASS_COUNT && virbuild_passes[i].vir)	/* the instructions it rewrote or removed (changed counts runs) */
				fprintf(stderr, " changes=%llu", (unsigned long long)c->changes);
			if (w->verify)
				fprintf(stderr, " checked=%llu", (unsigned long long)c->checked);
			fputc('\n', stderr);
		}
		*t = virbuild_pass_count_units[--vir_count_n];
	}
	pthread_mutex_unlock(&virbuild_pass_count_mutex);
}
