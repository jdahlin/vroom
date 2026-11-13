/*
 * The registry of VIR's passes (include/vir/pass.h): one const row per pass,
 * no order (the -O2 order is the builder's table, codegen/vir/pass-table.c;
 * virtest runs the order a test names), and vir_pass_run, which runs a pass
 * and, when asked, checks the claim of its `keeps` row against a fresh
 * computation (VCC_VIR_CHECK_ANALYSES=1).
 *
 * `compact` is the registry's first row: vir_function_compact as a pass, so
 * that virtest can run, verify and print a pass end to end.  It is not a row
 * of the compiler's table.
 */
#include <pthread.h>

#include "virimpl.h"
#include "vir/pass.h"

static bool vir_compact_run(vir_fn *f, vir_pass_context *ctx)
{
	uint32_t n = vir_function_compact(f);

	ctx->changes += n;
	return n != 0;
}

const vir_pass_info vir_pass_compact = { "compact", vir_compact_run, 0, VIR_VALID_CFG | VIR_VALID_DOM, 0 };
/* it asks for no analysis (a late parameter is found from the brs) and changes no block, edge or terminator */
const vir_pass_info vir_pass_redundant_extensions = { "redundant-extensions", vir_redundant_extensions_remove, 0,
						      VIR_VALID_CFG | VIR_VALID_DOM, 0 };

static const vir_pass_info *const vir_passes[] = { &vir_pass_compact, &vir_pass_redundant_extensions };
#define VIR_NPASS ((uint32_t)(sizeof vir_passes / sizeof *vir_passes))

const vir_pass_info *const *vir_pass_list(uint32_t *n)
{
	*n = VIR_NPASS;
	return vir_passes;
}

const vir_pass_info *vir_pass_find(const char *name, size_t n)
{
	for (uint32_t i = 0; i < VIR_NPASS; i++)
		if (strlen(vir_passes[i]->name) == n && !memcmp(vir_passes[i]->name, name, n))
			return vir_passes[i];
	return NULL;
}

/* ---------------- the check of `keeps` ---------------- */

static bool vir_check_env;
static pthread_once_t vir_check_once = PTHREAD_ONCE_INIT;

static void vir_check_read(void)
{
	const char *s = getenv("VCC_VIR_CHECK_ANALYSES");

	vir_check_env = s && *s && *s != '0';
}

/* The analyses of `keeps` still marked valid in f are computed again and compared.  Debug runs only: it
 * copies what it compares (malloc), puts f back as it found it, and returns the name of the first stale
 * analysis or NULL. */
static const char *vir_analyses_check(vir_fn *f, uint8_t keeps)
{
	vir_ctx *x = vir_ctx_get();
	uint32_t was = f->valid, chk = was & keeps, nextra = f->nextra, mark = x->cfg_mark, end = x->cfg_end, nb = f->nblk;
	vir_blk *snap;
	uint32_t *lists = NULL, nl = 0;
	const char *bad = NULL;

	if (!chk || !nb)
		return NULL;
	snap = malloc((size_t)nb * sizeof *snap);
	if (!snap)
		return NULL;
	memcpy(snap, f->blk, (size_t)nb * sizeof *snap);
	if ((chk & VIR_VALID_CFG) && end >= mark && end <= f->nextra) {
		nl = end - mark;
		lists = malloc((size_t)(nl ? nl : 1) * 4);
		if (!lists) {
			free(snap);
			return NULL;
		}
		memcpy(lists, f->extra + mark, (size_t)nl * 4);
	}
	f->valid = 0;
	if (!vir_cfg(f)) {
		bad = "cfg";
	} else {
		if (chk & VIR_VALID_CFG) {
			for (uint32_t b = 0; b < nb && !bad; b++) {
				if (f->blk[b].npred != snap[b].npred || f->blk[b].pred != snap[b].pred)
					bad = "cfg";
				else
					for (uint32_t j = 0; j < snap[b].npred; j++)
						if (f->extra[snap[b].pred + j] != lists[snap[b].pred + j - mark])
							bad = "cfg";
			}
		}
		if (!bad && (chk & VIR_VALID_DOM)) {
			if (!vir_dom(f))
				bad = "dom";
			for (uint32_t b = 0; b < nb && !bad; b++)
				if (f->blk[b].idom != snap[b].idom || f->blk[b].pre != snap[b].pre || f->blk[b].post != snap[b].post)
					bad = "dom";
		}
	}
	memcpy(f->blk, snap, (size_t)nb * sizeof *snap);
	if (lists)
		memcpy(f->extra + mark, lists, (size_t)nl * 4);
	f->nextra = nextra;
	x->cfg_mark = mark;
	x->cfg_end = end;
	f->valid = was;
	free(lists);
	free(snap);
	return bad;
}

bool vir_pass_run(vir_fn *f, const vir_pass_info *p, vir_pass_context *ctx, char *err, size_t n)
{
	bool changed = p->run(f, ctx);

	if (n)
		*err = 0;
	pthread_once(&vir_check_once, vir_check_read);
	if (n && (ctx->checked || vir_check_env)) {
		const char *bad = vir_analyses_check(f, p->keeps);

		if (bad)
			snprintf(err, n, "its `keeps` names the %s analysis, which a fresh computation does not reproduce", bad);
	}
	return changed;
}
