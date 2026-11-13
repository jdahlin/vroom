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

#include "vir-internal.h"
#include "vir/pass.h"

static bool vir_compact_run(vir_function *f, vir_pass_context *ctx)
{
	uint32_t n = vir_function_compact(f);

	ctx->changes += n;
	return n != 0;
}

static const vir_pass_info vir_passes[] = {
	/* name       run              needs  keeps                              flags */
	{ "compact", vir_compact_run, 0,     VIR_VALID_CFG | VIR_VALID_DOM,      0 },
};
#define VIR_NPASS ((uint32_t)(sizeof vir_passes / sizeof *vir_passes))

const vir_pass_info *vir_pass_list(uint32_t *n)
{
	*n = VIR_NPASS;
	return vir_passes;
}

const vir_pass_info *vir_pass_find(const char *name, size_t n)
{
	for (uint32_t i = 0; i < VIR_NPASS; i++)
		if (strlen(vir_passes[i].name) == n && !memcmp(vir_passes[i].name, name, n))
			return &vir_passes[i];
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
static const char *vir_analyses_check(vir_function *f, uint8_t keeps)
{
	vir_context *x = vir_context_get();
	uint32_t was = f->valid, vir_alloc_checker = was & keeps, nextra = f->extra_count, mark = x->cfg_mark, end = x->cfg_end, nb = f->block_count;
	vir_block *snap;
	uint32_t *lists = NULL, nl = 0;
	const char *vir_alloc_check_failure_add = NULL;

	if (!vir_alloc_checker || !nb)
		return NULL;
	snap = malloc((size_t)nb * sizeof *snap);
	if (!snap)
		return NULL;
	memcpy(snap, f->blocks, (size_t)nb * sizeof *snap);
	if ((vir_alloc_checker & VIR_VALID_CFG) && end >= mark && end <= f->extra_count) {
		nl = end - mark;
		lists = malloc((size_t)(nl ? nl : 1) * 4);
		if (!lists) {
			free(snap);
			return NULL;
		}
		memcpy(lists, f->extra + mark, (size_t)nl * 4);
	}
	f->valid = 0;
	if (!vir_cfg_build(f)) {
		vir_alloc_check_failure_add = "cfg";
	} else {
		if (vir_alloc_checker & VIR_VALID_CFG) {
			for (uint32_t b = 0; b < nb && !vir_alloc_check_failure_add; b++) {
				if (f->blocks[b].predecessor_count != snap[b].predecessor_count || f->blocks[b].predecessor_first != snap[b].predecessor_first)
					vir_alloc_check_failure_add = "cfg";
				else
					for (uint32_t j = 0; j < snap[b].predecessor_count; j++)
						if (f->extra[snap[b].predecessor_first + j] != lists[snap[b].predecessor_first + j - mark])
							vir_alloc_check_failure_add = "cfg";
			}
		}
		if (!vir_alloc_check_failure_add && (vir_alloc_checker & VIR_VALID_DOM)) {
			if (!vir_dominators_build(f))
				vir_alloc_check_failure_add = "dom";
			for (uint32_t b = 0; b < nb && !vir_alloc_check_failure_add; b++)
				if (f->blocks[b].immediate_dominator != snap[b].immediate_dominator || f->blocks[b].preorder != snap[b].preorder || f->blocks[b].preorder_last != snap[b].preorder_last)
					vir_alloc_check_failure_add = "dom";
		}
	}
	memcpy(f->blocks, snap, (size_t)nb * sizeof *snap);
	if (lists)
		memcpy(f->extra + mark, lists, (size_t)nl * 4);
	f->extra_count = nextra;
	x->cfg_mark = mark;
	x->cfg_end = end;
	f->valid = was;
	free(lists);
	free(snap);
	return vir_alloc_check_failure_add;
}

bool vir_pass_run(vir_function *f, const vir_pass_info *p, vir_pass_context *ctx, char *err, size_t n)
{
	bool changed = p->run(f, ctx);

	if (n)
		*err = 0;
	pthread_once(&vir_check_once, vir_check_read);
	if (n && (ctx->checked || vir_check_env)) {
		const char *vir_alloc_check_failure_add = vir_analyses_check(f, p->keeps);

		if (vir_alloc_check_failure_add)
			snprintf(err, n, "its `keeps` names the %s analysis, which a fresh computation does not reproduce", vir_alloc_check_failure_add);
	}
	return changed;
}
