/*
 * A function through VIR (PLAN-vir.md): the VIR walker builds it
 * (emit/vir/build.c); what it cannot build is emitted as before (X8).
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
 */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "vw.h"

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
	{
		int novtag = e->novtag;	/* (a bail out of a volatile access leaves it raised) */

		vw_wk_function(e, s);
		e->novtag = novtag;
	}
	e->vtry = false;
	if (m & 8)
		vw_name(e, s, nm, sizeof nm);
	if (e->vfail) {
		if (m & 8)
			fprintf(stderr, "vir: %s: no: %s\n", nm, e->vfail);
		return false;
	}
	{
		vwst *V = e->vw;
		vbuf b = { 0 };
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
		if ((m & 16) && !nerr && !vw_listed(e, s))
			m &= ~16;
		if ((m & 16) && !nerr) {
			const char *why;

			if (X86(e->c) ? vw_x64(e, s, &why) : vw_a64(e, s, &why)) {
				if (m & 8)
					fprintf(stderr, "vir: %s: lowered, %u spills\n", nm, V->ao.st.spills);
				return true;
			}
			if (m & 8)
				fprintf(stderr, "vir: %s: not lowered: %s\n", nm, why);
		}
	}
	return false;
}
