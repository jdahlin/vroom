/*
 * A function through VIR (PLAN-vir.md): the VIR walker builds it
 * (emit/vir/build.c); what it cannot build is emitted as before (X8).
 *
 * VCC_VIR (read once): unset or 0 off; otherwise letters:
 *   b  build VIR (then emit the function as before)
 *   v  ... and run the verifier on it (failures to stderr)
 *   d  ... and print it to stderr
 *   s  one line per function to stderr: built, or why not
 * Only at -O2 and above.
 */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "vw.h"

static int vw_mode(void)
{
	static int mode = -1;

	if (mode < 0) {
		const char *s = getenv("VCC_VIR");
		int m = 0;

		if (s && *s && strcmp(s, "0"))
			for (m = 1; *s; s++)
				m |= *s == 'v' ? 2 : *s == 'd' ? 4 : *s == 's' ? 8 : 0;
		mode = m;
	}
	return mode;
}

static void vw_name(ecx *e, uint32_t s, char *o, size_t n)
{
	const vident *id = &vintern_ents[e->c->syms[s].name];

	snprintf(o, n, "%.*s", (int)id->len, id->s);
}

VPRIV bool vw_try(ecx *e, uint32_t s)
{
	int m = vw_mode();
	char nm[128];

	if (!m || e->E->o->opt < 2 || WASM(e->c) || X86(e->c))
		return false;
	e->vtry = true;
	e->vfail = NULL;
	vw_wk_function(e, s);
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
	}
	return false;
}
