/* vcc front end: VCC_DRCOV, coverage of a per-region declaration replay */
#include <stdatomic.h>

#include "ccimpl.h"

/* ---------------- VCC_DRCOV: coverage of a per-region declaration replay ---------------- */

static int drcov_on;

VPRIV void drcov_init(void)
{
	drcov_on = getenv("VCC_DRCOV") != NULL;
}

VPRIV bool drcov_active(void)
{
	return drcov_on;
}

static _Atomic uint64_t dr_tok, dr_hits, dr_htok, dr_ok, dr_oktok, dr_oktop, dr_endbad, dr_startbad, dr_dc;

VPRIV void drcov_boundary(cc *c)
{
	if (c->ndrb == c->cdrb) {
		c->cdrb = c->cdrb ? c->cdrb * 2 : 4096;
		c->drb = realloc(c->drb, c->cdrb * sizeof(uint64_t));
	}
	if (c->ntk == 1)
		c->drb[c->ndrb++] = pp_ntok(c->p) - 1;
}

static bool drb_has(const cc *c, uint64_t x)
{
	uint32_t lo = 0, hi = c->ndrb;	/* (ascending) */

	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;

		if (c->drb[m] < x)
			lo = m + 1;
		else
			hi = m;
	}
	return lo < c->ndrb && c->drb[lo] == x;
}

VPRIV void drcov_finish(cc *c)
{
	uint32_t n;
	const pp_hitreg *h = pp_hit_regions(c->p, &n);
	uint64_t ok = 0, oktok = 0, oktop = 0, htok = 0, eb = 0, sb = 0;

	for (uint32_t i = 0; i < n; i++) {
		uint64_t len = h[i].e - h[i].b;
		bool s = drb_has(c, h[i].b), e = drb_has(c, h[i].e);

		htok += len;
		if (s && e)
			ok++, oktok += len, oktop += h[i].nfile == 1 ? len : 0;
		else if (s)
			eb += len;
		else
			sb += len;
	}
	atomic_fetch_add(&dr_tok, pp_ntok(c->p));
	atomic_fetch_add(&dr_hits, n);
	atomic_fetch_add(&dr_htok, htok);
	atomic_fetch_add(&dr_ok, ok);
	atomic_fetch_add(&dr_oktok, oktok);
	atomic_fetch_add(&dr_oktop, oktop);
	atomic_fetch_add(&dr_endbad, eb);
	atomic_fetch_add(&dr_startbad, sb);
	atomic_fetch_add(&dr_dc, pp_skipped(c->p));
	free(c->drb);
	c->drb = NULL;
}

void cc_drcov_stats(char *buf, size_t n)
{
	if (!drcov_on) {
		*buf = 0;
		return;
	}
	snprintf(buf, n, "drcov: %.1f M tokens delivered (+%.1f M dc-skipped); %llu hits %.1f M tokens; "
		 "at boundaries %llu hits %.1f M tokens (%.1f M top-level); end not at one %.1f M, start not at one %.1f M",
		 dr_tok / 1e6, dr_dc / 1e6, (unsigned long long)dr_hits, dr_htok / 1e6, (unsigned long long)dr_ok,
		 dr_oktok / 1e6, dr_oktop / 1e6, dr_endbad / 1e6, dr_startbad / 1e6);
}
