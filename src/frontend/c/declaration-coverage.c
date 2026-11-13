/* vcc front end: VCC_DRCOV, coverage of a per-region declaration replay */
#include <stdatomic.h>

#include "cc-internal.h"

/* ---------------- VCC_DRCOV: coverage of a per-region declaration replay ---------------- */

static int cc_replay_coverage_on;

CC_PRIVATE void cc_replay_coverage_init(void)
{
	cc_replay_coverage_on = getenv("VCC_DRCOV") != NULL;
}

CC_PRIVATE bool cc_replay_coverage_is_active(void)
{
	return cc_replay_coverage_on;
}

static _Atomic uint64_t cc_replay_coverage_tokens, cc_replay_coverage_hits, cc_replay_coverage_hit_tokens, cc_replay_coverage_ok, cc_replay_coverage_ok_tokens, cc_replay_coverage_ok_top_level, cc_replay_coverage_end_bad, cc_replay_coverage_start_bad, cc_replay_coverage_declarations;

CC_PRIVATE void cc_replay_coverage_boundary(cc *c)
{
	if (c->ndrb == c->cdrb) {
		c->cdrb = c->cdrb ? c->cdrb * 2 : 4096;
		c->drb = realloc(c->drb, c->cdrb * sizeof(uint64_t));
	}
	if (c->ntk == 1)
		c->drb[c->ndrb++] = pp_token_count(c->p) - 1;
}

static bool cc_replay_coverage_has_boundary(const cc *c, uint64_t x)
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

CC_PRIVATE void cc_replay_coverage_finish(cc *c)
{
	uint32_t n;
	const pp_hitreg *h = pp_hit_regions(c->p, &n);
	uint64_t ok = 0, oktok = 0, oktop = 0, htok = 0, eb = 0, sb = 0;

	for (uint32_t i = 0; i < n; i++) {
		uint64_t len = h[i].e - h[i].b;
		bool s = cc_replay_coverage_has_boundary(c, h[i].b), e = cc_replay_coverage_has_boundary(c, h[i].e);

		htok += len;
		if (s && e)
			ok++, oktok += len, oktop += h[i].nfile == 1 ? len : 0;
		else if (s)
			eb += len;
		else
			sb += len;
	}
	atomic_fetch_add(&cc_replay_coverage_tokens, pp_token_count(c->p));
	atomic_fetch_add(&cc_replay_coverage_hits, n);
	atomic_fetch_add(&cc_replay_coverage_hit_tokens, htok);
	atomic_fetch_add(&cc_replay_coverage_ok, ok);
	atomic_fetch_add(&cc_replay_coverage_ok_tokens, oktok);
	atomic_fetch_add(&cc_replay_coverage_ok_top_level, oktop);
	atomic_fetch_add(&cc_replay_coverage_end_bad, eb);
	atomic_fetch_add(&cc_replay_coverage_start_bad, sb);
	atomic_fetch_add(&cc_replay_coverage_declarations, pp_skipped_token_count(c->p));
	free(c->drb);
	c->drb = NULL;
}

void cc_replay_coverage_statistics(char *buf, size_t n)
{
	if (!cc_replay_coverage_on) {
		*buf = 0;
		return;
	}
	snprintf(buf, n, "drcov: %.1f M tokens delivered (+%.1f M dc-skipped); %llu hits %.1f M tokens; "
		 "at boundaries %llu hits %.1f M tokens (%.1f M top-level); end not at one %.1f M, start not at one %.1f M",
		 cc_replay_coverage_tokens / 1e6, cc_replay_coverage_declarations / 1e6, (unsigned long long)cc_replay_coverage_hits, cc_replay_coverage_hit_tokens / 1e6, (unsigned long long)cc_replay_coverage_ok,
		 cc_replay_coverage_ok_tokens / 1e6, cc_replay_coverage_ok_top_level / 1e6, cc_replay_coverage_end_bad / 1e6, cc_replay_coverage_start_bad / 1e6);
}
