/*
 * A function through VIR (PLAN-vir.md): the VIR walker builds it
 * (codegen/vir/build.c); what it cannot build is emitted as before (X8).
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
 * why the others did not (reasons: failure.h); see vw_stats_print.
 */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "vw.h"

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

/* VCC_VIR_STATS: count one failed function; the first of each reason is kept as its example */
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
 * VCC_VIR_STATS=1, after a TU's code is generated (stderr, parsed by bench/vir-failures.py):
 *   vcc vir: attempted=N lowered=N built=N fallback=N
 *   vcc vir-failure: reason=NAME count=N example=FUNCTION detail=TEXT-TO-END-OF-LINE
 * attempted: functions the VIR builder was started on; lowered: emitted from
 * VIR; built: built but, without `l` in VCC_VIR, not lowered; fallback: the
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
}

static int vw_mode(void)
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

static void vw_name(ecx *e, uint32_t s, char *o, size_t n)
{
	const vident *id = &vintern_ents[e->c->syms[s].name];

	snprintf(o, n, "%.*s", (int)id->len, id->s);
}

/* VCC_VIR_LIST: the function is named in the file (or there is none) */
static bool vw_listed(ecx *e, uint32_t s)
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
	vw_name(e, s, nm + 1, sizeof nm - 2);
	strcat(nm, "\n");
	return strstr(list, nm) != NULL;
}

VPRIV bool vw_enabled(const cc_opts *o)
{
	return (vw_mode() & 16) && o->opt >= 2;
}

VPRIV bool vw_try(ecx *e, uint32_t s)
{
	int m = vw_mode();
	char nm[128] = "";

	if (!m || e->E->o->opt < 2 || WASM(e->c))
		return false;
	e->vtry = true;
	e->vfail = NULL;
	if (e->E->vstats)
		e->E->vattempted++;
	{
		int novtag = e->novtag;	/* (a bail out of a volatile access leaves it raised) */

		vw_wk_function(e, s);
		e->novtag = novtag;
	}
	e->vtry = false;
	if (m & 8)
		vw_name(e, s, nm, sizeof nm);
	if (e->vfail) {
		vw_failure_note(e, s, e->vfail, VIRFAIL_emitter_error, NULL);
		if (m & 8)
			fprintf(stderr, "vir: %s: no: %s\n", nm, e->vfail);
		return false;
	}
	{
		vwst *V = e->vw;
		vbuffer b = { 0 };
		uint32_t nerr = 0;

		if (m & 2) {
			nerr = vir_verify(&V->f, NULL, &b, 10);
			if (nerr)
				fprintf(stderr, "vir: %s: %u verifier failures:\n%.*s", nm[0] ? nm : "?", nerr, (int)b.n, b.p ? (char *)b.p : "");
			b.n = 0;
		}
		if (m & 4) {
			vir_print_fn(&b, NULL, &V->f);
			fprintf(stderr, "%.*s", (int)b.n, b.p ? (char *)b.p : "");
		}
		if (m & 8)
			fprintf(stderr, "vir: %s: built, %u instructions, %u blocks%s\n", nm, V->f.nins, V->f.nblk, nerr ? ", INVALID" : "");
		free(b.p);
		if (nerr) {
			char n[16];

			snprintf(n, sizeof n, "%u", nerr);
			vw_failure_note(e, s, VIRFAIL(verifier_rejected), VIRFAIL_verifier_rejected, n);
		}
		if ((m & 16) && !nerr && !vw_listed(e, s))
			m &= ~16;
		if ((m & 16) && !nerr) {
			const char *why;

			V->detail[0] = 0;
			if (X86(e->c) ? vw_x64(e, s, &why) : vw_a64(e, s, &why)) {
				if (e->E->vstats)
					e->E->vlowered++;
				if (m & 8)
					fprintf(stderr, "vir: %s: lowered, %u spills\n", nm, V->ao.st.spills);
				return true;
			}
			vw_failure_note(e, s, why, VIRFAIL_lowering_failed, V->detail);
			if (m & 8)
				fprintf(stderr, "vir: %s: not lowered: %s%s%s\n", nm, why, V->detail[0] ? ": " : "", V->detail);
		} else if (!nerr && e->E->vstats)
			e->E->vbuilt++;
	}
	return false;
}
