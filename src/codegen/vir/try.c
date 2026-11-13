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
 *                          vw_stats_print
 *   -fvir-passes=ITEMS     time, leave out or check the builder's passes by name
 *                          (pass-table.c); its lines are printed with -fvir-stats's
 */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "pass-table.h"
#include "port/performance-counters.h"

/* every reason VIR gives up on a function (failure.h); vfail points at a message of this table */
VPRIV const vir_failure_info vir_failure_table[VIRFAIL_COUNT] = {
#define X(name, message) { #name, message },
	VIR_FAILURES(X)
#undef X
};

/* the reason whose message `why` is; none: the walker's own diagnostic, or what `other` says */
static int vw_failure_find(const char *why)
{
	for (int k = 0; k < VIRFAIL_COUNT; k++)
		if (vir_failure_table[k].message == why)
			return k;
	return -1;
}

/* -fvir-stats: count one failed function; the first of each reason is kept as its example */
static void vw_failure_note(ecx *e, uint32_t s, const char *why, vir_failure other, const char *detail)
{
	emitter *E = e->E;
	int k;

	if (!E->vstats)
		return;
	k = vw_failure_find(why);
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
VPRIV void vw_stats_print(emitter *E)
{
	uint64_t fell = 0;
	int ord[VIRFAIL_COUNT], n = 0;

	for (int k = 0; k < VIRFAIL_COUNT; k++)
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

		fprintf(stderr, "vcc vir-failure: reason=%s count=%u example=%.*s detail=%s\n", vir_failure_table[k].name,
			(unsigned)E->vfailed[k].count, (int)id->len, id->s, E->vfailed[k].detail);
	}
	virbuild_pass_stats_print(E);
}

/* what vw_try does with a function */
enum {
	VW_BUILD = 1,
	VW_VERIFY = 2,
	VW_DUMP = 4,
	VW_REPORT = 8,
	VW_LOWER = 16,
};

static int vw_mode(const cc_opts *o)
{
	const cc_vir_aids *v = &o->vir_aid;

	if (!o->optimize.vir)
		return 0;
	return VW_BUILD | (v->verify ? VW_VERIFY : 0) | (v->dump ? VW_DUMP : 0) | (v->report ? VW_REPORT : 0) |
	       (v->build_only ? 0 : VW_LOWER);
}

static void vw_name(ecx *e, uint32_t s, char *o, size_t n)
{
	const vident *id = &vintern_ents[e->c->syms[s].name];

	snprintf(o, n, "%.*s", (int)id->len, id->s);
}

/* -fvir-functions=FILE, read once per TU (tu.c): the names between newlines; a file that cannot be
   read fails the job */
VPRIV void vw_function_list_read(emitter *E, const char *path)
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
static bool vw_listed(ecx *e, uint32_t s)
{
	char nm[130];

	if (!e->E->vlist)
		return true;
	nm[0] = '\n';
	vw_name(e, s, nm + 1, sizeof nm - 2);
	strcat(nm, "\n");
	return strstr(e->E->vlist, nm) != NULL;
}

VPRIV bool vw_enabled(const cc_opts *o)
{
	return (vw_mode(o) & VW_LOWER) != 0;
}

VPRIV bool vw_try(ecx *e, uint32_t s)
{
	int m = vw_mode(e->E->o);
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

		vw_wk_function(e, s);
		e->novtag = novtag;
	}
	if (timed)
		virbuild_pass_note(e->E, VIR_PASS_ROW_BUILD(npass), vperformance_subtract(vperformance_now(), t0).ins);
	e->vtry = false;
	if (m & VW_REPORT)
		vw_name(e, s, nm, sizeof nm);
	if (e->vfail) {
		vw_failure_note(e, s, e->vfail, VIRFAIL_emitter_error, NULL);
		if (m & VW_REPORT)
			fprintf(stderr, "vir: %s: no: %s\n", nm, e->vfail);
		return false;
	}
	{
		vwst *V = e->vw;
		vbuffer b = { 0 };
		uint32_t nerr = 0;

		if (m & VW_VERIFY) {
			nerr = vir_verify(&V->f, NULL, &b, 10);
			if (nerr)
				fprintf(stderr, "vir: %s: %u verifier failures:\n%.*s", nm[0] ? nm : "?", nerr, (int)b.n, b.p ? (char *)b.p : "");
			b.n = 0;
		}
		if (m & VW_DUMP) {
			vir_print_fn(&b, NULL, &V->f);
			fprintf(stderr, "%.*s", (int)b.n, b.p ? (char *)b.p : "");
		}
		if (m & VW_REPORT)
			fprintf(stderr, "vir: %s: built, %u instructions, %u blocks%s\n", nm, V->f.nins, V->f.nblk, nerr ? ", INVALID" : "");
		free(b.p);
		if (nerr) {
			char n[16];

			snprintf(n, sizeof n, "%u", nerr);
			vw_failure_note(e, s, VIRFAIL(verifier_rejected), VIRFAIL_verifier_rejected, n);
		}
		if ((m & VW_LOWER) && !nerr && !vw_listed(e, s))
			m &= ~VW_LOWER;
		if ((m & VW_LOWER) && !nerr) {
			const char *why;

			bool ok;

			V->detail[0] = 0;
			if (timed)
				t0 = vperformance_now();
			ok = X86(e->c) ? vw_x64(e, s, &why) : vw_a64(e, s, &why);
			if (timed)
				virbuild_pass_note(e->E, VIR_PASS_ROW_LOWER(npass), vperformance_subtract(vperformance_now(), t0).ins);
			if (ok) {
				if (e->E->vstats)
					e->E->vlowered++;
				if (m & VW_REPORT)
					fprintf(stderr, "vir: %s: lowered, %u spills\n", nm, V->ao.st.spills);
				return true;
			}
			vw_failure_note(e, s, why, VIRFAIL_lowering_failed, V->detail);
			if (m & VW_REPORT)
				fprintf(stderr, "vir: %s: not lowered: %s%s%s\n", nm, why, V->detail[0] ? ": " : "", V->detail);
		} else if (!nerr && e->E->vstats)
			e->E->vbuilt++;
	}
	return false;
}
