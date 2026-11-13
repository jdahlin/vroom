/*
 * The VIR builder's pass table and VCC_VIR_PASSES (pass-table.h).
 *
 * VCC_VIR_PASSES=ITEM[,ITEM...], read once per process (a test aid, not an option):
 *   -NAME          leave pass NAME out (a required one: error)
 *   only=A+B+...   run exactly these, in table order, plus the required ones
 *   verify         after every pass: the pre-IR check (preir-check.c), or vir_verify
 *                  after canon (VIR before canon is not in the layout vir_verify checks,
 *                  SPEC-vir 2.3, so to-vir's output is checked after canon: verify=to-vir
 *                  checks there); a failure prints
 *                  "vcc vir-verify: pass=NAME function=F: MESSAGE" and the function
 *                  falls back (reason verifier_rejected)
 *   verify=A+B     ... after the passes named
 *   time           per pass: runs, runs that changed something, instructions (vperf,
 *                  read only when set); printed with VCC_VIR_STATS=1 (vw_stats_print):
 *                    vcc vir-pass: pass=NAME runs=N changed=N instructions=N [checked=N]
 *                  changed is `-` for a pass that does not say; the rows @build (the
 *                  walk and every pass, to-vir and canon included) and @lower (the
 *                  target's lowering) are the totals a pass's share is taken of
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
static bool vir_pass_thread(ecx *e) { vw_thread(e); return false; }
static bool vir_pass_rpo(ecx *e) { vw_rpo(e); return false; }
static bool vir_pass_preds(ecx *e) { vw_preds(e); return false; }
static bool vir_pass_seal(ecx *e) { vw_seal(e); return false; }
static bool vir_pass_dead_params(ecx *e) { vw_deadparams(e); return false; }
static bool vir_pass_simplify(ecx *e) { vw_simplify(e); return false; }
static bool vir_pass_vectorize(ecx *e) { vw_vectorize(e); return false; }
static bool vir_pass_split(ecx *e) { vw_split(e); return false; }
static bool vir_pass_slot_uses(ecx *e) { vw_slot_uses(e); return false; }

static bool vir_pass_to_vir(ecx *e)
{
	vwst *V = e->vw;

	if (!vw_to_vir(e))
		vw_fail(e, V->why ? V->why : VIRFAIL(build_failed));
	return true;
}

static bool vir_pass_canon(ecx *e)
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
static const vir_pass vir_passes[] = {
	/* name                 run                    needs   makes  keeps      flags */
	{ "thread",             vir_pass_thread,       0,      0,     0,         U },				/* cfg.c */
	{ "rpo",                vir_pass_rpo,          0,      O,     O,         U | PASS_REQUIRED },		/* cfg.c */
	{ "preds",              vir_pass_preds,        O,      P,     O | P,     U | PASS_REQUIRED },		/* cfg.c */
	{ "seal",               vir_pass_seal,         O | P,  0,     O | P | D, U | PASS_REQUIRED },		/* pipeline.c */
	{ "promote",            vw_promote,            O | P,  0,     O | P | D, 0 },				/* promote.c */
	{ "trivial",            vw_trivial,            P,      0,     O | P | D, PASS_FIXED_POINT },		/* parameters.c */
	{ "dead-params",        vir_pass_dead_params,  O,      0,     O | P | D, U },				/* parameters.c */
	{ "simplify",           vir_pass_simplify,     O | P,  0,     O | P | D, U },				/* simplify.c */
	/* simplifying can free a slot's address (an inlined call's pointer argument parked as an
	   integer comes back as i2p(p2i $slot)): promote again, then simplify what it forwards */
	{ "promote-again",      vw_promote,            O | P,  0,     O | P | D, 0 },				/* promote.c */
	{ "trivial-again",      vw_trivial,            P,      0,     O | P | D, PASS_FIXED_POINT | PASS_IF_PREVIOUS_CHANGED },
	{ "dead-params-again",  vir_pass_dead_params,  O,      0,     O | P | D, U | PASS_IF_PREVIOUS_CHANGED },
	{ "simplify-again",     vir_pass_simplify,     O | P,  0,     O | P | D, U | PASS_IF_PREVIOUS_CHANGED },
	{ "condition-merge",    vw_condmerge,          O | P,  0,     O | P,     PASS_THEN_TRIVIAL },		/* condition-merge.c */
	{ "if-conversion",      vw_ifconv,             O | P,  0,     O | P,     PASS_THEN_TRIVIAL },		/* if-conversion.c */
	/* (loop-constants, vv_pin, runs inside vectorize, per loop: a row of its own needs vectorize.c) */
	{ "vectorize",          vir_pass_vectorize,    O | P,  0,     O | P,     U | PASS_VECTOR_GATE },	/* vectorize.c */
	/* required: VIR's block parameters take no critical edges (left out, the verifier rejects 10 741
	   functions of the suites and 31 vcc tests miscompile under VCC_VIR=bl, which does not verify) */
	{ "split",              vir_pass_split,        O | P,  0,     O | P,     U | PASS_REQUIRED },		/* cfg.c */
	{ "slot-uses",          vir_pass_slot_uses,    O,      0,     O | P | D, U | PASS_REQUIRED },		/* pipeline.c */
	{ "to-vir",             vir_pass_to_vir,       O,      0,     O | P | D, PASS_REQUIRED | PASS_ON_VIR },	/* pipeline.c, to-vir.c */
	{ "canon",              vir_pass_canon,        0,      0,     O | P | D, PASS_REQUIRED | PASS_ON_VIR },	/* vir/vir.c */
};
#define NPASS ((uint32_t)(sizeof vir_passes / sizeof *vir_passes))
_Static_assert(sizeof vir_passes / sizeof *vir_passes <= 32, "vir_pass_switch keeps a bit per row");

#undef O
#undef P
#undef D
#undef U

VPRIV const vir_pass *vir_pass_list(uint32_t *n)
{
	*n = NPASS;
	return vir_passes;
}

/* ---------------- the switch ---------------- */

static vir_pass_switch vir_switch;
static uint64_t vir_read_cost;	/* instructions of a vperformance_now pair (time) */
static _Thread_local uint64_t vir_overhead;	/* instructions this thread spent timing passes, not yet taken */
static pthread_once_t vir_switch_once = PTHREAD_ONCE_INIT;

static __attribute__((noreturn)) void vir_switch_error(const char *what, const char *s, size_t n)
{
	fprintf(stderr, "vcc: VCC_VIR_PASSES: %s %.*s (passes:", what, (int)n, s);
	for (uint32_t i = 0; i < NPASS; i++)
		fprintf(stderr, " %s%s", vir_passes[i].name, vir_passes[i].flags & PASS_REQUIRED ? "*" : "");
	fprintf(stderr, "; * required)\n");
	exit(2);
}

static uint32_t vir_switch_find(const char *s, size_t n)
{
	for (uint32_t i = 0; i < NPASS; i++)
		if (strlen(vir_passes[i].name) == n && !memcmp(vir_passes[i].name, s, n))
			return i;
	vir_switch_error("unknown pass", s, n);
}

/* A+B+C (to `end`) as row bits */
static uint32_t vir_switch_names(const char *s, const char *end)
{
	uint32_t m = 0;

	while (s < end) {
		const char *q = memchr(s, '+', (size_t)(end - s));

		if (!q)
			q = end;
		m |= 1u << vir_switch_find(s, (size_t)(q - s));
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
	vir_pass_switch *w = &vir_switch;
	uint32_t req = 0, only = 0, valid = 0;

	for (uint32_t i = 0; i < NPASS; i++)
		if (vir_passes[i].flags & PASS_REQUIRED)
			req |= 1u << i;
	while (s && *s) {
		const char *e = strchr(s, ',');
		size_t n;

		if (!e)
			e = s + strlen(s);
		n = (size_t)(e - s);
		if (n == 4 && !memcmp(s, "time", 4))
			w->time = true;
		else if (n == 6 && !memcmp(s, "verify", 6))
			w->verify = ~0u;
		else if (n > 7 && !memcmp(s, "verify=", 7))
			w->verify |= vir_switch_names(s + 7, e);
		else if (n > 5 && !memcmp(s, "only=", 5))
			only |= vir_switch_names(s + 5, e);
		else if (n > 1 && s[0] == '-') {
			uint32_t i = vir_switch_find(s + 1, n - 1);

			if (vir_passes[i].flags & PASS_REQUIRED)
				vir_switch_error("cannot leave out the required pass", s + 1, n - 1);
			w->skip |= 1u << i;
		} else if (n)
			vir_switch_error("unknown item", s, n);
		s = *e ? e + 1 : e;
	}
	if (only)
		w->skip |= ~(only | req);
	for (uint32_t i = 0; i + 1 < NPASS; i++)	/* (to-vir: checked after canon, the next row) */
		if ((w->verify >> i & 1) && (vir_passes[i].flags & PASS_ON_VIR))
			w->verify = (w->verify & ~(1u << i)) | 1u << (i + 1);
	w->skip &= (NPASS < 32 ? (1u << NPASS) - 1 : ~0u);
	w->verify &= (NPASS < 32 ? (1u << NPASS) - 1 : ~0u);
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
		const vir_pass *p = &vir_passes[i];
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

VPRIV const vir_pass_switch *vir_pass_switch_get(void)
{
	pthread_once(&vir_switch_once, vir_switch_read);
	return &vir_switch;
}

/* ---------------- running a row ---------------- */

/* one row: its pass, to a fixed point if it says so, then vw_trivial if it says so; whether it changed anything */
VPRIV bool vir_pass_step(ecx *e, const vir_pass *p)
{
	bool changed = p->run(e);

	if (changed && (p->flags & PASS_FIXED_POINT))
		while (p->run(e))
			;
	if (changed && (p->flags & PASS_THEN_TRIVIAL))
		while (vw_trivial(e))
			;
	return changed;
}

/* per TU (keyed by its emitter, so emitimpl.h gains no field): counters per row, plus @build and @lower */
typedef struct vir_pass_count { uint64_t runs, changed, ins, checked; } vir_pass_count;
typedef struct vir_pass_tu { emitter *E; vir_pass_count c[NPASS + 2]; } vir_pass_tu;

static pthread_mutex_t vir_count_mu = PTHREAD_MUTEX_INITIALIZER;
static vir_pass_tu *vir_count_tus;
static uint32_t vir_count_n, vir_count_cap;

/* the TU's counters (made on first use); call with vir_count_mu held; NULL: out of memory */
static vir_pass_tu *vir_count_tu(emitter *E)
{
	for (uint32_t i = 0; i < vir_count_n; i++)
		if (vir_count_tus[i].E == E)
			return &vir_count_tus[i];
	if (vir_count_n == vir_count_cap) {
		uint32_t nc = vir_count_cap ? 2 * vir_count_cap : 8;
		vir_pass_tu *t = realloc(vir_count_tus, nc * sizeof *t);

		if (!t)
			return NULL;
		vir_count_tus = t, vir_count_cap = nc;
	}
	vir_count_tus[vir_count_n] = (vir_pass_tu){ .E = E };
	return &vir_count_tus[vir_count_n++];
}

static void vir_count_add(emitter *E, uint32_t row, bool ran, bool changed, uint64_t ins, bool checked)
{
	vir_pass_tu *t;

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

/* row @build or @lower ran `instructions`, of which the timing of the passes inside it is taken off */
VPRIV void vir_pass_note(emitter *E, uint32_t row, uint64_t instructions)
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
VPRIV bool vir_pass_step_instrumented(ecx *e, uint32_t i, uint32_t *valid)
{
	const vir_pass *p = &vir_passes[i];
	const vir_pass_switch *w = &vir_switch;
	bool count = w->time && e->E->vstats, check = w->verify >> i & 1, changed;
	vperf t0 = { 0, 0 }, t1 = { 0, 0 };

	if (count)
		t0 = vperformance_now();
	changed = vir_pass_step(e, p);
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
VPRIV void vir_pass_stats_print(emitter *E)
{
	const vir_pass_switch *w = vir_pass_switch_get();
	vir_pass_tu *t;

	if (!w->time && !w->verify)
		return;
	pthread_mutex_lock(&vir_count_mu);
	t = vir_count_tu(E);
	if (t) {
		for (uint32_t i = 0; i < NPASS + 2; i++) {
			const vir_pass_count *c = &t->c[i];
			const char *name = i < NPASS ? vir_passes[i].name : i == VIR_PASS_ROW_BUILD(NPASS) ? "@build" : "@lower";
			char ch[24];

			if (i < NPASS && (w->skip >> i & 1))
				continue;
			if (i < NPASS && (vir_passes[i].flags & PASS_CHANGE_UNKNOWN))
				snprintf(ch, sizeof ch, "-");
			else
				snprintf(ch, sizeof ch, "%llu", (unsigned long long)c->changed);
			fprintf(stderr, "vcc vir-pass: pass=%s runs=%llu changed=%s instructions=%llu", name,
				(unsigned long long)c->runs, ch, (unsigned long long)c->ins);
			if (w->verify)
				fprintf(stderr, " checked=%llu", (unsigned long long)c->checked);
			fputc('\n', stderr);
		}
		*t = vir_count_tus[--vir_count_n];
	}
	pthread_mutex_unlock(&vir_count_mu);
}
