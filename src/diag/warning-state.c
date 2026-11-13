/* The warning state: `#pragma diagnostic`, the -W options and each warning's level. */
#include "../frontend/pp/ppimpl.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------- #pragma clang/GCC diagnostic ---------------- */

enum { PD_IGNORED = 1, PD_WARNING, PD_ERROR };

/* a new state at loc (the current one + node n, or a popped one) */
static void dtrans(pp *p, uint32_t st, uint32_t loc)
{
	if (!p->dtr)
		p->dtr = varena_alloc(&p->perm, 4096 * sizeof(*p->dtr));
	if (p->ndtr < 4096)
		p->dtr[p->ndtr].loc = loc, p->dtr[p->ndtr++].st = st;
	p->dcur = st;
}

/* "push", "pop", "ignored|warning|error|fatal \"-Wflag\"" (after "clang diagnostic") */
VPRIV void diag_pragma(pp *p, const char *q, const char *e, uint32_t loc)
{
	static const char *const acts[] = { "ignored", "warning", "error", "fatal" };
	size_t n;
	int act = 0;

	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	for (n = 0; q + n < e && ((q[n] >= 'a' && q[n] <= 'z') || q[n] == '_'); n++)
		;
	if (n == 4 && !memcmp(q, "push", 4)) {
		if (p->ndstk < 64)
			p->dstk[p->ndstk++] = p->dcur;
		return;
	}
	if (n == 3 && !memcmp(q, "pop", 3)) {
		if (p->ndstk)	/* (clang: "pragma diagnostic pop could not pop, no matching push") */
			dtrans(p, p->dstk[--p->ndstk], loc);
		return;
	}
	for (int i = 0; i < 4; i++)
		if (strlen(acts[i]) == n && !memcmp(q, acts[i], n))
			act = i < 2 ? i + 1 : PD_ERROR;
	if (!act)
		return;
	q += n;
	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	if (q + 3 >= e || q[0] != '"' || q[1] != '-' || q[2] != 'W')
		return;
	q += 3;
	for (n = 0; q + n < e && q[n] != '"'; n++)
		;
	if (n >= sizeof(p->dn->name) || (n > 3 && !memcmp(q, "no-", 3)))
		return;
	if (!p->dn) {
		p->dn = varena_alloc(&p->perm, 4096 * sizeof(*p->dn));
		memset(&p->dn[0], 0, sizeof(*p->dn));
		p->ndn = 1;
	}
	if (p->ndn == 4096)
		return;
	p->dn[p->ndn].prev = p->dcur;
	p->dn[p->ndn].act = (uint8_t)act;
	memcpy(p->dn[p->ndn].name, q, n);
	p->dn[p->ndn].name[n] = 0;
	p->nden += act != PD_IGNORED;
	dtrans(p, p->ndn++, loc);
}

/* the state at loc: the last transition before it in its file, else the one its file was
   entered in (clang's DiagStateMap) */
static uint32_t dstate_at(const pp *p, uint32_t loc)
{
	const fent *e = loc_fent(p, loc);

	if (!e || loc - e->base > e->f->size)
		return p->dcur;
	for (uint32_t i = p->ndtr; i-- > 0;)
		if (p->dtr[i].loc >= e->base && p->dtr[i].loc < loc)
			return p->dtr[i].st;
	return e->dstate;
}

/* leaving file fe: a state it changed holds in the includer from the #include on (clang) */
VPRIV void diag_file_end(pp *p, uint32_t fe)
{
	const fent *e = &p->fe[fe];

	if (e->from && p->dcur != dstate_at(p, e->from + 1))
		dtrans(p, p->dcur, e->from + 1);
}

/* the command line's st (0 off, 1 warning, 2 error) under the pragmas in force at loc;
   clang: "warning" is a warning, also under -Werror and -Werror=flag */
static int dpragma(const pp *p, int st, const char *flag, const char *const *groups, uint32_t loc)
{
	for (uint32_t s = dstate_at(p, loc); s; s = p->dn[s].prev) {
		const char *n = p->dn[s].name;
		bool hit = !strcmp(n, flag) || !strcmp(n, "everything");

		for (int g = 0; !hit && groups && groups[g]; g++)
			hit = !strcmp(n, groups[g]);
		if (hit)
			return p->dn[s].act == PD_IGNORED ? 0 : p->dn[s].act == PD_ERROR ? 2 : 1;
	}
	return st;
}

int pp_diag_npragma(const pp *p)
{
	return (int)p->ndtr;
}

const char *const *pp_diag_wargs(const pp *p, int *n)
{
	*n = p->o->nwargs;
	return p->o->wargs;
}

const uint32_t *pp_diag_nenable_ref(const pp *p)
{
	return &p->nden;
}

/* the command line's state of flag; *w: -w (no pragma turns it on) */
static int wstate(const pp *p, const char *flag, bool on, const char *const *groups, bool *w, bool deferr)
{
	bool err = deferr, allerr = false, noerr = false;

	*w = false;

	for (int i = 0; i < p->o->nwargs; i++) {
		const char *a = p->o->wargs[i];
		bool no = !strncmp(a, "-Wno-", 5);
		const char *n = a + (no ? 5 : 2);
		bool hit;

		if (!strcmp(a, "-w")) {
			if (!deferr) on = false;
			continue;
		}
		if (!strcmp(a, "-Werror")) { allerr = true; continue; }
		if (!strcmp(a, "-Wno-error")) { allerr = false; continue; }
		if (!strncmp(n, "error=", 6)) {
			bool g = !strcmp(n + 6, flag);

			for (int k = 0; !g && groups && groups[k]; k++)	/* clang: -Werror=GROUP too */
				g = !strcmp(n + 6, groups[k]);
			if (g) {
				if (no)
					noerr = true, err = false;
				else
					err = true, on = true, noerr = false;
			}
			continue;
		}
		hit = !strcmp(n, flag) || !strcmp(n, "everything");
		for (int g = 0; !hit && groups && groups[g]; g++)
			hit = !strcmp(n, groups[g]);
		if (hit) {
			on = !no;
			if (deferr && no) err = false;
		}
	}
	for (int i = 0; i < p->o->nwargs; i++)
		if (!strcmp(p->o->wargs[i], "-w")) {
			*w = true;
			if (!deferr || !(err || (allerr && !noerr))) return 0;
		}
	if (!on)
		return 0;
	return err || (allerr && !noerr) ? 2 : 1;
}

int pp_wstate(const pp *p, const char *flag, bool on, const char *const *groups)
{
	bool w;

	return wstate(p, flag, on, groups, &w, false);
}

VPRIV void pp_wc_fill(pp *p)
{
	static const char *const g[] = { "pedantic", NULL };

	p->wc_o = p->o;
	p->wc[PPW_C2Y] = pp_wstate(p, "c2y-extensions", false, g) != 0;
	p->wc[PPW_VARIADIC] = pp_wstate(p, "variadic-macros", false, g) != 0;
}

/* the level a warning of flag is reported at at loc: VDL_WARNING, VDL_ERROR (-Werror), or -1 (off) */
int pp_diag_wlevel(pp *p, const char *flag, bool on, const char *const *groups, uint32_t loc)
{
	bool w;
	int st = wstate(p, flag, on, groups, &w, false);

	if (unlikely(p->ndtr) && loc != UINT32_MAX && !w)
		st = dpragma(p, st, flag, groups, loc);
	if (!st || (loc != UINT32_MAX && pp_in_system_header(p, loc)))
		return -1;
	return st == 2 ? VDL_ERROR : VDL_WARNING;
}

/* A recoverable error: its default severity survives -w/-Wno-error, but
 * named warning controls and diagnostic pragmas may downgrade or suppress it. */
int pp_diag_elevel(pp *p, const char *flag, const char *const *groups, uint32_t loc)
{
	bool w;
	int st = wstate(p, flag, true, groups, &w, true);

	if (unlikely(p->ndtr) && loc != UINT32_MAX)
		st = dpragma(p, st, flag, groups, loc);
	if (!st || (st != 2 && (w || (loc != UINT32_MAX && pp_in_system_header(p, loc)))))
		return -1;
	return st == 2 ? VDL_ERROR : VDL_WARNING;
}

/* any #pragma clang diagnostic changed a flag's state: per-location answers can differ */
int pp_diag_pragmas(const pp *p)
{
	return p->ndtr != 0;
}

int pp_werror_named(const pp *p, const char *flag, const char *const *groups)
{
	int r = 0;

	for (int i = 0; i < p->o->nwargs; i++) {
		const char *a = p->o->wargs[i], *n;
		bool hit;

		if (!strncmp(a, "-Werror=", 8))
			n = a + 8;
		else if (!strncmp(a, "-Wno-error=", 11))
			n = a + 11;
		else
			continue;
		hit = !strcmp(n, flag);
		for (int g = 0; !hit && groups && groups[g]; g++)
			hit = !strcmp(n, groups[g]);
		if (hit)
			r = a[2] == 'e' ? 1 : -1;
	}
	return r;
}

bool pp_warnx(pp *p, const char *flag, bool on, const char *const *groups, pp_dinfo *di, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	int lv = pp_diag_wlevel(p, flag, on, groups, di->loc);
	vdiag *d;
	va_list ap;

	if (lv < 0)
		return false;
	va_start(ap, fmt);
	d = pp_diag_vbegin(p, lv, di->loc, fmt, ap);
	va_end(ap);
	pp_diag_flag(d, flag);
	d->werror = lv == VDL_ERROR;
	for (int i = 0; i < di->nr; i++)
		pp_diag_label(p, d, di->r[i].b, di->r[i].e, NULL);
	for (int i = 0; i < di->nfx; i++)
		pp_diag_fix(p, d, di->fx[i].b, di->fx[i].e, di->fx[i].s, di->fx[i].app);
	pp_diag_macro(d, di->ms, di->mlen);
	pp_diag_emit(p, d);
	return true;
}
