/*
 * A function through VIR (PLAN-vir.md): the VIR walker builds it
 * (codegen/vir/preir.h); what it cannot build is emitted as before (X8).
 *
 * VIR is -O2's code generator: cc_opts.optimize.vir (driver/optimization.c,
 * doc/OPTIMIZATION.md).  The developer's aids, cc_opts.vir_aid:
 *   -fno-vir-lower         build VIR, then emit the function as before
 *   -fvir-verify           run the verifier on it (failures to stderr)
 *   -fvir-dump             print it to stderr
 *   -fvir-report           one line per function to stderr: built, lowered, or why not
 *   -fvir-check            the allocation checker runs after the allocator
 *   -fvir-functions=FILE   only the functions named in FILE, one per line, are
 *                          lowered (bisecting)
 *   -fvir-stats            per TU, on stderr, how many functions went through VIR
 *                          and why the others did not (reasons: failure.h); see
 *                          virbuild_statistics_print
 *   -fvir-passes=ITEMS     time, leave out or check the builder's passes by name
 *                          (pass-table.c); its lines are printed with -fvir-stats's
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

/* -fvir-stats: count one failed function; the first of each reason is kept as its example */
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
 * -fvir-stats, after a TU's code is generated (stderr, parsed by bench/vir-failures.py):
 *   vcc vir: attempted=N lowered=N built=N fallback=N
 *   vcc vir-failure: reason=NAME count=N example=FUNCTION detail=TEXT-TO-END-OF-LINE
 * attempted: functions the VIR builder was started on; lowered: emitted from
 * VIR; built: built but, with -fno-vir-lower, not lowered; fallback: the
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

/* what virbuild_function_try does with a function */
enum {
	VIRBUILD_MODE_BUILD = 1,
	VIRBUILD_MODE_VERIFY = 2,
	VIRBUILD_MODE_DUMP = 4,
	VIRBUILD_MODE_REPORT = 8,
	VIRBUILD_MODE_LOWER = 16,
};

static int virbuild_mode_get(const cc_opts *o)
{
	const cc_vir_aids *v = &o->vir_aid;

	if (!o->optimize.vir)
		return 0;
	return VIRBUILD_MODE_BUILD | (v->verify ? VIRBUILD_MODE_VERIFY : 0) | (v->dump ? VIRBUILD_MODE_DUMP : 0) |
	       (v->report ? VIRBUILD_MODE_REPORT : 0) | (v->build_only ? 0 : VIRBUILD_MODE_LOWER);
}

static void virbuild_function_name_get(ecx *e, uint32_t s, char *o, size_t n)
{
	const vident *id = &vintern_ents[e->c->syms[s].name];

	snprintf(o, n, "%.*s", (int)id->len, id->s);
}

/* -fvir-functions=FILE, read once per TU (tu.c): the names between newlines; a file that cannot be
   read fails the job */
VPRIV void virbuild_function_list_read(emitter *E, const char *path)
{
	FILE *fp = fopen(path, "r");
	size_t n = 0, cap = 1 << 16, k;
	char *list;

	if (!fp)
		vjob_fail_errno(path);
	list = malloc(cap + 2);
	if (!list)
		vout_of_memory();
	list[n++] = '\n';
	while ((k = fread(list + n, 1, cap - n, fp)) > 0) {
		n += k;
		if (n == cap && !(list = realloc(list, (cap *= 2) + 2)))
			vout_of_memory();
	}
	fclose(fp);
	list[n++] = '\n';
	list[n] = 0;
	E->vlist = list;
}

/* the function is named in -fvir-functions' file (or there is none) */
static bool virbuild_function_is_listed(ecx *e, uint32_t s)
{
	char nm[130];

	if (!e->E->vlist)
		return true;
	nm[0] = '\n';
	virbuild_function_name_get(e, s, nm + 1, sizeof nm - 2);
	strcat(nm, "\n");
	return strstr(e->E->vlist, nm) != NULL;
}

VPRIV bool virbuild_is_enabled(const cc_opts *o)
{
	return (virbuild_mode_get(o) & VIRBUILD_MODE_LOWER) != 0;
}

VPRIV bool virbuild_function_try(ecx *e, uint32_t s)
{
	int m = virbuild_mode_get(e->E->o);
	char nm[128] = "";
	bool timed;	/* -fvir-passes=time: the build and the lowering are rows of its statistic */
	uint32_t npass;
	vperf t0 = { 0, 0 };

	if (!m || WASM(e->c))
		return false;
	e->vtry = true;
	e->vfail = NULL;
	if (e->E->vstats)
		e->E->vattempted++;
	timed = e->E->vstats && e->E->vpass.time;
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
	if (m & VIRBUILD_MODE_REPORT)
		virbuild_function_name_get(e, s, nm, sizeof nm);
	if (e->vfail) {
		virbuild_failure_note(e, s, e->vfail, VIRBUILD_FAILURE_emitter_error, NULL);
		if (m & VIRBUILD_MODE_REPORT)
			fprintf(stderr, "vir: %s: no: %s\n", nm, e->vfail);
		return false;
	}
	{
		virbuild_context *V = e->vw;
		vbuffer b = { 0 };
		uint32_t nerr = 0;

		if (m & VIRBUILD_MODE_VERIFY) {
			nerr = vir_verify(&V->f, NULL, &b, 10);
			if (nerr)
				fprintf(stderr, "vir: %s: %u verifier failures:\n%.*s", nm[0] ? nm : "?", nerr, (int)b.n, b.p ? (char *)b.p : "");
			b.n = 0;
		}
		if (m & VIRBUILD_MODE_DUMP) {
			vir_print_function(&b, NULL, &V->f);
			fprintf(stderr, "%.*s", (int)b.n, b.p ? (char *)b.p : "");
		}
		if (m & VIRBUILD_MODE_REPORT)
			fprintf(stderr, "vir: %s: built, %u instructions, %u blocks%s\n", nm, V->f.insn_count, V->f.block_count, nerr ? ", INVALID" : "");
		free(b.p);
		if (nerr) {
			char n[16];

			snprintf(n, sizeof n, "%u", nerr);
			virbuild_failure_note(e, s, VIRBUILD_FAILURE(verifier_rejected), VIRBUILD_FAILURE_verifier_rejected, n);
		}
		if ((m & VIRBUILD_MODE_LOWER) && !nerr && !virbuild_function_is_listed(e, s))
			m &= ~VIRBUILD_MODE_LOWER;
		if ((m & VIRBUILD_MODE_LOWER) && !nerr) {
			const char *why;

			bool ok;

			V->failure_detail[0] = 0;
			if (timed)
				t0 = vperformance_now();
			ok = X86(e->c) ? x64_vir_function_lower(e, s, &why) : a64_vir_function_lower(e, s, &why);
			if (timed)
				virbuild_pass_note(e->E, VIRBUILD_PASS_ROW_LOWER(npass), vperformance_subtract(vperformance_now(), t0).ins);
			if (ok) {
				if (e->E->vstats)
					e->E->vlowered++;
				if (m & VIRBUILD_MODE_REPORT)
					fprintf(stderr, "vir: %s: lowered, %u spills\n", nm, V->allocation.statistics.spills);
				return true;
			}
			virbuild_failure_note(e, s, why, VIRBUILD_FAILURE_lowering_failed, V->failure_detail);
			if (m & VIRBUILD_MODE_REPORT)
				fprintf(stderr, "vir: %s: not lowered: %s%s%s\n", nm, why, V->failure_detail[0] ? ": " : "", V->failure_detail);
		} else if (!nerr && e->E->vstats)
			e->E->vbuilt++;
	}
	return false;
}
