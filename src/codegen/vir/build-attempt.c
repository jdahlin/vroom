/*
 * A function through VIR (PLAN-vir.md): the VIR walker builds it
 * (codegen/vir/preir.h); what it cannot build is emitted as before (X8).
 *
 * VCC_VIR (read once): unset or 0 off; otherwise letters:
 *   b  build VIR (then emit the function as before)
 *   v  ... and run the verifier on it (failures to stderr)
 *   d  ... and print it to stderr
 *   s  one line per function to stderr: built, or why not
 *   l  lower what was built to target records (arm64 or x86-64) and use them;
 *      VCC_VIR_CHECK=1 also runs the allocation checker; VCC_VIR_LIST=FILE lowers
 *      only the functions named in FILE, one per line (bisecting)
 * Only at -O2 and above.
 *
 * VCC_VIR_STATS=1: per TU, on stderr, how many functions went through VIR and
 * why the others did not (reasons: failure.h); see virbuild_statistics_print.
 *
 * VCC_VIR_PASSES: time, leave out or check the builder's passes by name
 * (pass-table.c); its lines are printed with VCC_VIR_STATS's.
 */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "pass-table.h"
#include "port/performance-counters.h"

/* every reason VIR gives up on a function (failure.h); vfail points at a message of this table */
VPRIV const virbuild_failure_info virbuild_failure_table[VIRBUILD_FAILURE_COUNT] = {
#define X(name, message) { #name, message },
	VIRBUILD_FAILURES(X)
#undef X
};

/* the reason whose message `why` is; none: the walker's own diagnostic, or what `other` says */
static int virbuild_failure_find(const char *why)
{
	for (int k = 0; k < VIRBUILD_FAILURE_COUNT; k++)
		if (virbuild_failure_table[k].message == why)
			return k;
	return -1;
}

/* VCC_VIR_STATS: count one failed function; the first of each reason is kept as its example */
static void virbuild_failure_note(ecx *e, uint32_t s, const char *why, virbuild_failure other, const char *detail)
{
	emitter *E = e->E;
	int k;

	if (!E->vstats)
		return;
	k = virbuild_failure_find(why);
	if (k < 0) {	/* a diagnostic of the shared walker or emitter: its format (a literal) is the detail */
		k = other;
		detail = why ? why : "";
	}
	pthread_mutex_lock(&E->emu);
	if (!E->vfailed[k].count++) {
		E->vfailed[k].sym = s;
		snprintf(E->vfailed[k].detail, sizeof E->vfailed[k].detail, "%s", detail ? detail : "");
	}
	pthread_mutex_unlock(&E->emu);
}

/*
 * VCC_VIR_STATS=1, after a TU's code is generated (stderr, parsed by bench/vir-failures.py):
 *   vcc vir: attempted=N lowered=N built=N fallback=N
 *   vcc vir-failure: reason=NAME count=N example=FUNCTION detail=TEXT-TO-END-OF-LINE
 * attempted: functions the VIR builder was started on; lowered: emitted from
 * VIR; built: built but, without `l` in VCC_VIR, not lowered; fallback: the
 * direct emitter compiled them, one reason each.  Reasons are listed most
 * frequent first.
 */
VPRIV void virbuild_statistics_print(emitter *E)
{
	uint64_t fell = 0;
	int ord[VIRBUILD_FAILURE_COUNT], n = 0;

	for (int k = 0; k < VIRBUILD_FAILURE_COUNT; k++)
		if (E->vfailed[k].count) {
			int j = n++;

			fell += E->vfailed[k].count;
			while (j > 0 && E->vfailed[ord[j - 1]].count < E->vfailed[k].count)
				ord[j] = ord[j - 1], j--;
			ord[j] = k;
		}
	fprintf(stderr, "vcc vir: attempted=%llu lowered=%llu built=%llu fallback=%llu\n", (unsigned long long)E->vattempted,
		(unsigned long long)E->vlowered, (unsigned long long)E->vbuilt, (unsigned long long)fell);
	for (int i = 0; i < n; i++) {
		int k = ord[i];
		const vident *id = &vintern_ents[E->c->syms[E->vfailed[k].sym].name];

		fprintf(stderr, "vcc vir-failure: reason=%s count=%u example=%.*s detail=%s\n", virbuild_failure_table[k].name,
			(unsigned)E->vfailed[k].count, (int)id->len, id->s, E->vfailed[k].detail);
	}
	virbuild_pass_stats_print(E);
}

static int virbuild_mode_get(void)
{
	static _Thread_local int mode = -1;

	if (mode < 0) {
		const char *s = getenv("VCC_VIR");
		int m = 0;

		if (s && *s && strcmp(s, "0"))
			for (m = 1; *s; s++)
				m |= *s == 'v' ? 2 : *s == 'd' ? 4 : *s == 's' ? 8 : *s == 'l' ? 16 : 0;
		mode = m;
	}
	return mode;
}

static void virbuild_function_name_get(ecx *e, uint32_t s, char *o, size_t n)
{
	const vident *id = &vintern_ents[e->c->syms[s].name];

	snprintf(o, n, "%.*s", (int)id->len, id->s);
}

/* VCC_VIR_LIST: the function is named in the file (or there is none) */
static bool virbuild_function_is_listed(ecx *e, uint32_t s)
{
	static char *list;
	static int state;	/* 0 unread, 1 no list, 2 list */
	static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
	char nm[130];

	pthread_mutex_lock(&mu);
	if (!state) {
		const char *p = getenv("VCC_VIR_LIST");
		FILE *fp = p ? fopen(p, "r") : NULL;

		state = 1;
		if (fp) {
			size_t n = 0, cap = 1 << 16;
			size_t k;

			list = malloc(cap + 2);
			list[n++] = '\n';
			while (list && (k = fread(list + n, 1, cap - n, fp)) > 0) {
				n += k;
				if (n == cap)
					list = realloc(list, (cap *= 2) + 2);
			}
			fclose(fp);
			if (list) {
				list[n++] = '\n';
				list[n] = 0;
				state = 2;
			}
		}
	}
	pthread_mutex_unlock(&mu);
	if (state != 2)
		return true;
	nm[0] = '\n';
	virbuild_function_name_get(e, s, nm + 1, sizeof nm - 2);
	strcat(nm, "\n");
	return strstr(list, nm) != NULL;
}

VPRIV bool virbuild_is_enabled(const cc_opts *o)
{
	return (virbuild_mode_get() & 16) && o->opt >= 2;
}

VPRIV bool virbuild_function_try(ecx *e, uint32_t s)
{
	int m = virbuild_mode_get();
	char nm[128] = "";
	bool timed;	/* VCC_VIR_PASSES=time: the build and the lowering are rows of its statistic */
	uint32_t npass;
	vperf t0 = { 0, 0 };

	if (!m || e->E->o->opt < 2 || WASM(e->c))
		return false;
	e->vtry = true;
	e->vfail = NULL;
	if (e->E->vstats)
		e->E->vattempted++;
	timed = e->E->vstats && virbuild_pass_switch_get()->time;
	virbuild_pass_list(&npass);
	if (timed)
		t0 = vperformance_now();
	{
		int novtag = e->novtag;	/* (a bail out of a volatile access leaves it raised) */

		virbuild_wk_function(e, s);
		e->novtag = novtag;
	}
	if (timed)
		virbuild_pass_note(e->E, VIRBUILD_PASS_ROW_BUILD(npass), vperformance_subtract(vperformance_now(), t0).ins);
	e->vtry = false;
	if (m & 8)
		virbuild_function_name_get(e, s, nm, sizeof nm);
	if (e->vfail) {
		virbuild_failure_note(e, s, e->vfail, VIRBUILD_FAILURE_emitter_error, NULL);
		if (m & 8)
			fprintf(stderr, "vir: %s: no: %s\n", nm, e->vfail);
		return false;
	}
	{
		virbuild_context *V = e->vw;
		vbuffer b = { 0 };
		uint32_t nerr = 0;

		if (m & 2) {
			nerr = vir_verify(&V->f, NULL, &b, 10);
			if (nerr)
				fprintf(stderr, "vir: %s: %u verifier failures:\n%.*s", nm[0] ? nm : "?", nerr, (int)b.n, b.p ? (char *)b.p : "");
			b.n = 0;
		}
		if (m & 4) {
			vir_print_function(&b, NULL, &V->f);
			fprintf(stderr, "%.*s", (int)b.n, b.p ? (char *)b.p : "");
		}
		if (m & 8)
			fprintf(stderr, "vir: %s: built, %u instructions, %u blocks%s\n", nm, V->f.insn_count, V->f.block_count, nerr ? ", INVALID" : "");
		free(b.p);
		if (nerr) {
			char n[16];

			snprintf(n, sizeof n, "%u", nerr);
			virbuild_failure_note(e, s, VIRBUILD_FAILURE(verifier_rejected), VIRBUILD_FAILURE_verifier_rejected, n);
		}
		if ((m & 16) && !nerr && !virbuild_function_is_listed(e, s))
			m &= ~16;
		if ((m & 16) && !nerr) {
			const char *why;

			bool ok;

			V->detail[0] = 0;
			if (timed)
				t0 = vperformance_now();
			ok = X86(e->c) ? x64_vir_function_lower(e, s, &why) : a64_vir_function_lower(e, s, &why);
			if (timed)
				virbuild_pass_note(e->E, VIRBUILD_PASS_ROW_LOWER(npass), vperformance_subtract(vperformance_now(), t0).ins);
			if (ok) {
				if (e->E->vstats)
					e->E->vlowered++;
				if (m & 8)
					fprintf(stderr, "vir: %s: lowered, %u spills\n", nm, V->ao.statistics.spills);
				return true;
			}
			virbuild_failure_note(e, s, why, VIRBUILD_FAILURE_lowering_failed, V->detail);
			if (m & 8)
				fprintf(stderr, "vir: %s: not lowered: %s%s%s\n", nm, why, V->detail[0] ? ": " : "", V->detail);
		} else if (!nerr && e->E->vstats)
			e->E->vbuilt++;
	}
	return false;
}
