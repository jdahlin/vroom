/*
 * The VIR builder's pass table and VCC_VIR_PASSES (pass-table.h).
 *
 * VCC_VIR_PASSES=ITEM[,ITEM...], read once per process (a test aid, not an option):
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
 *   time           per pass: runs, runs that changed something, instructions (vperf,
 *                  read only when set); printed with VCC_VIR_STATS=1 (vw_stats_print):
 *                    vcc vir-pass: pass=NAME runs=N changed=N instructions=N [changes=N] [checked=N]
 *                  changed is `-` for a pass that does not say; changes=N (rows of VIR
 *                  passes only) counts the instructions the pass rewrote or removed; the rows @build (the
 *                  walk and every pass, to-vir and canon included) and @lower (the
 *                  target's lowering) are the totals a pass's share is taken of
 * A row with a `vir` entry runs that pass of the registry (vir/pass.h) on V->f; with `verify`
 * the check of its `keeps` row runs too (VCC_VIR_CHECK_ANALYSES=1 asks for that alone).
 * An unknown pass or item prints the passes to stderr and exits 2.
 */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"
#include "vectorize.h"
#include "pass-table.h"
#include "port/performance-counters.h"

/* the passes without a "changed" result, and the ones on VIR (failure: vw_fail, as vw_finish did) */
static bool virbuild_pass_thread(ecx *e) { vw_thread(e); return false; }
static bool virbuild_pass_rpo(ecx *e) { vw_rpo(e); return false; }
static bool virbuild_pass_preds(ecx *e) { vw_preds(e); return false; }
static bool virbuild_pass_seal(ecx *e) { vw_seal(e); return false; }
static bool virbuild_pass_dead_params(ecx *e) { vw_deadparams(e); return false; }
static bool virbuild_pass_simplify(ecx *e) { vw_simplify(e); return false; }
static bool virbuild_pass_vectorize(ecx *e) { vw_vectorize(e); return false; }
static bool virbuild_pass_split(ecx *e) { vw_split(e); return false; }
static bool virbuild_pass_slot_uses(ecx *e) { vw_slot_uses(e); return false; }

static bool virbuild_pass_to_vir(ecx *e)
{
	vwst *V = e->vw;

	if (!vw_to_vir(e))
		vw_fail(e, V->why ? V->why : VIRFAIL(build_failed));
	return true;
}

static bool virbuild_pass_canon(ecx *e)
{
	vwst *V = e->vw;

	if (!vir_canon(&V->f))
		vw_fail(e, V->why ? V->why : VIRFAIL(build_failed));
	return true;
}

#define O PIR_ORDER
#define P PIR_PREDS
#define D PIR_DOMS
#define U PASS_CHANGE_UNKNOWN

/* one row per step of the pipeline, in its order (quality-vir-build.md 3.1); the file is the pass's */
static const virbuild_pass virbuild_passes[] = {
	/* name                 run                    needs   makes  keeps      flags, vir (the registry entry of a VIR pass's row: none yet) */
	{ "thread",             virbuild_pass_thread,       0,      0,     0,         U, NULL },				/* cfg.c */
	{ "rpo",                virbuild_pass_rpo,          0,      O,     O,         U | PASS_REQUIRED, NULL },		/* cfg.c */
	{ "preds",              virbuild_pass_preds,        O,      P,     O | P,     U | PASS_REQUIRED, NULL },		/* cfg.c */
	{ "seal",               virbuild_pass_seal,         O | P,  0,     O | P | D, U | PASS_REQUIRED, NULL },		/* pipeline.c */
	{ "promote",            vw_promote,            O | P,  0,     O | P | D, 0, NULL },				/* promote.c */
	{ "trivial",            vw_trivial,            P,      0,     O | P | D, PASS_FIXED_POINT, NULL },		/* parameters.c */
	{ "dead-params",        virbuild_pass_dead_params,  O,      0,     O | P | D, U, NULL },				/* parameters.c */
	{ "simplify",           virbuild_pass_simplify,     O | P,  0,     O | P | D, U, NULL },				/* simplify.c */
	/* simplifying can free a slot's address (an inlined call's pointer argument parked as an
	   integer comes back as i2p(p2i $slot)): promote again, then simplify what it forwards */
	{ "promote-again",      vw_promote,            O | P,  0,     O | P | D, 0, NULL },				/* promote.c */
	{ "trivial-again",      vw_trivial,            P,      0,     O | P | D, PASS_FIXED_POINT | PASS_IF_PREVIOUS_CHANGED, NULL },
	{ "dead-params-again",  virbuild_pass_dead_params,  O,      0,     O | P | D, U | PASS_IF_PREVIOUS_CHANGED, NULL },
	{ "simplify-again",     virbuild_pass_simplify,     O | P,  0,     O | P | D, U | PASS_IF_PREVIOUS_CHANGED, NULL },
	{ "condition-merge",    vw_condmerge,          O | P,  0,     O | P,     PASS_THEN_TRIVIAL, NULL },		/* condition-merge.c */
	{ "if-conversion",      vw_ifconv,             O | P,  0,     O | P,     PASS_THEN_TRIVIAL, NULL },		/* if-conversion.c */
	/* (loop-constants, vv_pin, runs inside vectorize, per loop: a row of its own needs vectorize.c) */
	{ "vectorize",          virbuild_pass_vectorize,    O | P,  0,     O | P,     U | PASS_VECTOR_GATE, NULL },	/* vectorize.c */
	/* required: VIR's block parameters take no critical edges (left out, the verifier rejects 10 741
	   functions of the suites and 31 vcc tests miscompile under VCC_VIR=bl, which does not verify) */
	{ "split",              virbuild_pass_split,        O | P,  0,     O | P,     U | PASS_REQUIRED, NULL },		/* cfg.c */
	{ "slot-uses",          virbuild_pass_slot_uses,    O,      0,     O | P | D, U | PASS_REQUIRED, NULL },		/* pipeline.c */
	{ "to-vir",             virbuild_pass_to_vir,       O,      0,     O | P | D, PASS_REQUIRED | PASS_ON_VIR, NULL },	/* pipeline.c, to-vir.c */
	{ "canon",              virbuild_pass_canon,        0,      0,     O | P | D, PASS_REQUIRED | PASS_ON_VIR, NULL },	/* vir/vir.c */
	/* the passes on VIR (vir/pass/), in the order they run: the row is the registry's entry */
	{ "redundant-extensions", NULL,                     0,      0,     O | P | D, PASS_ON_VIR, &vir_pass_redundant_extensions },	/* vir/pass/redundant-extensions.c */
};
#define NPASS ((uint32_t)(sizeof virbuild_passes / sizeof *virbuild_passes))
_Static_assert(sizeof virbuild_passes / sizeof *virbuild_passes <= 32, "virbuild_pass_switch keeps a bit per row");

#undef O
#undef P
#undef D
#undef U

VPRIV const virbuild_pass *virbuild_pass_list(uint32_t *n)
{
	*n = NPASS;
	return virbuild_passes;
}

/* ---------------- the switch ---------------- */

static virbuild_pass_switch vir_switch;
static uint64_t vir_read_cost;	/* instructions of a vperformance_now pair (time) */
static _Thread_local uint64_t vir_overhead;	/* instructions this thread spent timing passes, not yet taken */
static pthread_once_t vir_switch_once = PTHREAD_ONCE_INIT;

static __attribute__((noreturn)) void vir_switch_error(const char *what, const char *s, size_t n)
{
	fprintf(stderr, "vcc: VCC_VIR_PASSES: %s %.*s (passes:", what, (int)n, s);
	for (uint32_t i = 0; i < NPASS; i++)
		fprintf(stderr, " %s%s", virbuild_passes[i].name, virbuild_passes[i].flags & PASS_REQUIRED ? "*" : "");
	fprintf(stderr, "; * required)\n");
	exit(2);
}

static uint32_t vir_switch_find(const char *s, size_t n)
{
	for (uint32_t i = 0; i < NPASS; i++)
		if (strlen(virbuild_passes[i].name) == n && !memcmp(virbuild_passes[i].name, s, n))
			return i;
	vir_switch_error("unknown pass", s, n);
}

/* A+B+C (to `end`) as row bits */
static uint64_t vir_switch_names(const char *s, const char *end)
{
	uint64_t m = 0;

	while (s < end) {
		const char *q = memchr(s, '+', (size_t)(end - s));

		if (!q)
			q = end;
		m |= 1ull << vir_switch_find(s, (size_t)(q - s));
		s = q + 1;
	}
	return m;
}

static const char *vir_fact_name(uint32_t f)
{
	return f & PIR_ORDER ? "the block order" : f & PIR_PREDS ? "the predecessor lists" : "the dominators";
}

static void vir_switch_read(void)
{
	const char *s = getenv("VCC_VIR_PASSES");
	virbuild_pass_switch *w = &vir_switch;
	uint64_t req = 0, only = 0, plus = 0, off = 0;
	uint32_t valid = 0;

	for (uint32_t i = 0; i < NPASS; i++)
		if (virbuild_passes[i].flags & PASS_REQUIRED)
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
		else if (n > 7 && !memcmp(s, "verify=", 7))
			w->verify |= vir_switch_names(s + 7, e);
		else if (n > 1 && s[0] == '+')
			plus |= 1ull << vir_switch_find(s + 1, n - 1);
		else if (n > 5 && !memcmp(s, "only=", 5))
			only |= vir_switch_names(s + 5, e);
		else if (n > 1 && s[0] == '-') {
			uint32_t i = vir_switch_find(s + 1, n - 1);

			if (virbuild_passes[i].flags & PASS_REQUIRED)
				vir_switch_error("cannot leave out the required pass", s + 1, n - 1);
			w->skip |= 1ull << i;
		} else if (n)
			vir_switch_error("unknown item", s, n);
		s = *e ? e + 1 : e;
	}
	if (only)
		w->skip |= ~(only | req);
	else
		w->skip |= off & ~plus;	/* the rows that are off unless asked for (+NAME) */
	for (uint32_t i = 0; i < NPASS; i++)	/* a VIR row carries its registry entry's name */
		if (virbuild_passes[i].vir && strcmp(virbuild_passes[i].vir->name, virbuild_passes[i].name)) {
			fprintf(stderr, "vcc: VCC_VIR_PASSES: row %s runs the VIR pass %s\n", virbuild_passes[i].name,
				virbuild_passes[i].vir->name);
			exit(2);
		}
	for (uint32_t i = 0; i + 1 < NPASS; i++)	/* (to-vir: checked after canon, the next row; a VIR pass's row checks itself) */
		if ((w->verify >> i & 1) && (virbuild_passes[i].flags & PASS_ON_VIR) && !virbuild_passes[i + 1].vir)
			w->verify = (w->verify & ~(1ull << i)) | 1ull << (i + 1);
	w->skip &= (NPASS < 64 ? (1ull << NPASS) - 1 : ~0ull);
	w->verify &= (NPASS < 64 ? (1ull << NPASS) - 1 : ~0ull);
	w->active = w->skip || w->verify || w->time;
	if (w->time) {	/* what two reads cost, taken off every pass's count */
		uint64_t best = UINT64_MAX;

		for (int r = 0; r < 8; r++) {
			vperf a = vperformance_now(), b = vperformance_now();

			if (vperformance_subtract(b, a).ins < best)
				best = vperformance_subtract(b, a).ins;
		}
		vir_read_cost = best;
	}
	/* every pass that runs finds the facts it needs, whichever conditional rows run */
	for (uint32_t i = 0; i < NPASS; i++) {
		const virbuild_pass *p = &virbuild_passes[i];
		uint32_t after;

		if (w->skip >> i & 1)
			continue;
		if (p->needs & ~valid) {
			fprintf(stderr, "vcc: VCC_VIR_PASSES: pass %s needs %s, which no pass before it leaves valid\n", p->name,
				vir_fact_name(p->needs & ~valid));
			exit(2);
		}
		after = (valid & p->keeps) | p->makes;
		valid = p->flags & PASS_IF_PREVIOUS_CHANGED ? valid & after : after;
	}
}

VPRIV const virbuild_pass_switch *virbuild_pass_switch_get(void)
{
	pthread_once(&vir_switch_once, vir_switch_read);
	return &vir_switch;
}

/* ---------------- running a row ---------------- */

/* the function's name, for the messages of the check */
static const char *vir_fn_name(ecx *e, char *buf, size_t n);

/* per TU counters, see below: the instructions a VIR pass rewrote or removed */
static void vir_count_changes(emitter *E, uint32_t row, uint64_t changes);

/* The adapter: a VIR pass's row runs its registry entry on V->f.  A pass cannot fail the function, but
 * a limit reached while it appends is the build's failure (the function goes to the direct emitter), and so
 * is a stale analysis its `keeps` claims (checked with VCC_VIR_PASSES=verify or VCC_VIR_CHECK_ANALYSES=1). */
static bool virbuild_pass_vir_run(ecx *e, const virbuild_pass *p)
{
	vir_pass_context ctx = { .budget = VIR_PASS_BUDGET, .checked = virbuild_pass_switch_get()->verify != 0 };
	char err[160];
	bool changed = vir_pass_run(&e->vw->f, p->vir, &ctx, err, sizeof err);

	if (vir_failed())
		vw_fail(e, VIRFAIL(build_failed));
	if (err[0]) {
		char nm[130];

		fprintf(stderr, "vcc vir-verify: pass=%s function=%s: %s\n", p->name, vir_fn_name(e, nm, sizeof nm), err);
		vw_fail(e, VIRFAIL(verifier_rejected));
	}
	if (ctx.changes && e->E->vstats)
		vir_count_changes(e->E, (uint32_t)(p - virbuild_passes), ctx.changes);
	return changed;
}

/* one call of a row's pass */
static bool virbuild_pass_call(ecx *e, const virbuild_pass *p)
{
	return p->vir ? virbuild_pass_vir_run(e, p) : p->run(e);
}

/* one row: its pass, to a fixed point if it says so, then vw_trivial if it says so; whether it changed anything */
VPRIV bool virbuild_pass_step(ecx *e, const virbuild_pass *p)
{
	bool changed = virbuild_pass_call(e, p);

	if (changed && (p->flags & PASS_FIXED_POINT))
		while (virbuild_pass_call(e, p))
			;
	if (changed && (p->flags & PASS_THEN_TRIVIAL))
		while (vw_trivial(e))
			;
	return changed;
}

/* per TU (keyed by its emitter, so emitimpl.h gains no field): counters per row, plus @build and @lower */
typedef struct virbuild_pass_count { uint64_t runs, changed, ins, checked, changes; } virbuild_pass_count;
typedef struct virbuild_pass_tu { emitter *E; virbuild_pass_count c[NPASS + 2]; } virbuild_pass_tu;

static pthread_mutex_t vir_count_mu = PTHREAD_MUTEX_INITIALIZER;
static virbuild_pass_tu *vir_count_tus;
static uint32_t vir_count_n, vir_count_cap;

/* the TU's counters (made on first use); call with vir_count_mu held; NULL: out of memory */
static virbuild_pass_tu *vir_count_tu(emitter *E)
{
	for (uint32_t i = 0; i < vir_count_n; i++)
		if (vir_count_tus[i].E == E)
			return &vir_count_tus[i];
	if (vir_count_n == vir_count_cap) {
		uint32_t nc = vir_count_cap ? 2 * vir_count_cap : 8;
		virbuild_pass_tu *t = realloc(vir_count_tus, nc * sizeof *t);

		if (!t)
			return NULL;
		vir_count_tus = t, vir_count_cap = nc;
	}
	vir_count_tus[vir_count_n] = (virbuild_pass_tu){ .E = E };
	return &vir_count_tus[vir_count_n++];
}

static void vir_count_add(emitter *E, uint32_t row, bool ran, bool changed, uint64_t ins, bool checked)
{
	virbuild_pass_tu *t;

	pthread_mutex_lock(&vir_count_mu);
	t = vir_count_tu(E);
	if (t) {
		t->c[row].runs += ran;
		t->c[row].changed += changed;
		t->c[row].ins += ins;
		t->c[row].checked += checked;
	}
	pthread_mutex_unlock(&vir_count_mu);
}

static void vir_count_changes(emitter *E, uint32_t row, uint64_t changes)
{
	virbuild_pass_tu *t;

	pthread_mutex_lock(&vir_count_mu);
	t = vir_count_tu(E);
	if (t)
		t->c[row].changes += changes;
	pthread_mutex_unlock(&vir_count_mu);
}

/* row @build or @lower ran `instructions`, of which the timing of the passes inside it is taken off */
VPRIV void virbuild_pass_note(emitter *E, uint32_t row, uint64_t instructions)
{
	uint64_t o = vir_overhead + vir_read_cost;

	vir_overhead = 0;
	vir_count_add(E, row, true, false, instructions > o ? instructions - o : 0, false);
}

static const char *vir_fn_name(ecx *e, char *buf, size_t n)
{
	const vident *id = &vintern_ents[e->c->syms[e->wk->fsym].name];

	snprintf(buf, n, "%.*s", (int)id->len, id->s);
	return buf;
}

/* row i with what the switch asks for around it: timing, the check after it; valid: the facts (updated) */
VPRIV bool virbuild_pass_step_instrumented(ecx *e, uint32_t i, uint32_t *valid)
{
	const virbuild_pass *p = &virbuild_passes[i];
	const virbuild_pass_switch *w = &vir_switch;
	bool count = w->time && e->E->vstats, check = w->verify >> i & 1, changed;
	vperf t0 = { 0, 0 }, t1 = { 0, 0 };

	if (count)
		t0 = vperformance_now();
	changed = virbuild_pass_step(e, p);
	if (count) {
		uint64_t ins;

		t1 = vperformance_now();
		ins = vperformance_subtract(t1, t0).ins;
		vir_count_add(e->E, i, true, changed && !(p->flags & PASS_CHANGE_UNKNOWN), ins > vir_read_cost ? ins - vir_read_cost : 0,
			      check);
	} else if (check && e->E->vstats)
		vir_count_add(e->E, i, false, false, 0, true);
	*valid = (*valid & p->keeps) | p->makes;
	if (check) {
		char msg[200], nm[130];
		const char *bad;

		if (p->flags & PASS_ON_VIR) {
			vbuffer b = { 0 };
			uint32_t nerr = vir_verify(&e->vw->f, NULL, &b, 1);
			size_t k = 0;

			while (k < b.n && b.p[k] != '\n')
				k++;
			snprintf(msg, sizeof msg, "%u verifier failures, the first: %.*s", nerr, (int)k, b.p ? (char *)b.p : "");
			free(b.p);
			bad = nerr ? msg : NULL;
		} else
			bad = vw_preir_check(e, *valid, msg, sizeof msg);
		if (bad) {
			fprintf(stderr, "vcc vir-verify: pass=%s function=%s: %s\n", p->name, vir_fn_name(e, nm, sizeof nm), bad);
			vw_fail(e, VIRFAIL(verifier_rejected));
		}
	}
	if (count)	/* (the counting and the check are not the build's) */
		vir_overhead += vperformance_subtract(vperformance_now(), t1).ins + vir_read_cost;
	return changed;
}

/* VCC_VIR_PASSES=time or verify with VCC_VIR_STATS=1: the TU's lines (vw_stats_print), then its counters go */
VPRIV void virbuild_pass_stats_print(emitter *E)
{
	const virbuild_pass_switch *w = virbuild_pass_switch_get();
	virbuild_pass_tu *t;

	if (!w->time && !w->verify)
		return;
	pthread_mutex_lock(&vir_count_mu);
	t = vir_count_tu(E);
	if (t) {
		for (uint32_t i = 0; i < NPASS + 2; i++) {
			const virbuild_pass_count *c = &t->c[i];
			const char *name = i < NPASS ? virbuild_passes[i].name : i == VIR_PASS_ROW_BUILD(NPASS) ? "@build" : "@lower";
			char ch[24];

			if (i < NPASS && (w->skip >> i & 1))
				continue;
			if (i < NPASS && (virbuild_passes[i].flags & PASS_CHANGE_UNKNOWN))
				snprintf(ch, sizeof ch, "-");
			else
				snprintf(ch, sizeof ch, "%llu", (unsigned long long)c->changed);
			fprintf(stderr, "vcc vir-pass: pass=%s runs=%llu changed=%s instructions=%llu", name,
				(unsigned long long)c->runs, ch, (unsigned long long)c->ins);
			if (i < NPASS && virbuild_passes[i].vir)	/* the instructions it rewrote or removed (changed counts runs) */
				fprintf(stderr, " changes=%llu", (unsigned long long)c->changes);
			if (w->verify)
				fprintf(stderr, " checked=%llu", (unsigned long long)c->checked);
			fputc('\n', stderr);
		}
		*t = vir_count_tus[--vir_count_n];
	}
	pthread_mutex_unlock(&vir_count_mu);
}
