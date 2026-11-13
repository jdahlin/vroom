/* arm64 range-extension thunks: their records and the (isec, reloc) -> thunk map; part of ld/ld.c (#included there) */

/* ---------------- thunks ---------------- */

static void a64_ld_thunk_relocation_record(struct ld_context *ctx, uint32_t isec, uint32_t ri, uint32_t th)
{
	if (!ctx->relocations.thunk_redirect_keys || ctx->relocations.thunk_redirect_count * 2 >= ctx->relocations.thunk_redirect_mask) {
		uint32_t nm = ctx->relocations.thunk_redirect_keys ? ctx->relocations.thunk_redirect_mask * 2 + 1 : 1023;
		uint64_t *nk = ld_region_alloc((uint64_t)(nm + 1) * 8);
		uint32_t *nv = ld_region_alloc((uint64_t)(nm + 1) * 4);
		memset(nk, 0xff, (uint64_t)(nm + 1) * 8);
		for (uint32_t i = 0; ctx->relocations.thunk_redirect_keys && i <= ctx->relocations.thunk_redirect_mask; i++)
			if (ctx->relocations.thunk_redirect_keys[i] != UINT64_MAX) {
				uint32_t j = (uint32_t)(ctx->relocations.thunk_redirect_keys[i] * 0x9E3779B97F4A7C15ULL >> 32) & nm;
				while (nk[j] != UINT64_MAX)
					j = (j + 1) & nm;
				nk[j] = ctx->relocations.thunk_redirect_keys[i];
				nv[j] = ctx->relocations.thunk_redirect_values[i];
			}
		ctx->relocations.thunk_redirect_keys = nk;
		ctx->relocations.thunk_redirect_values = nv;
		ctx->relocations.thunk_redirect_mask = nm;
	}
	ctx->inputs.sections.v[isec].thunked = true;
	uint64_t k = (uint64_t)isec << 32 | ri;
	uint32_t j = (uint32_t)(k * 0x9E3779B97F4A7C15ULL >> 32) & ctx->relocations.thunk_redirect_mask;
	while (ctx->relocations.thunk_redirect_keys[j] != UINT64_MAX && ctx->relocations.thunk_redirect_keys[j] != k)
		j = (j + 1) & ctx->relocations.thunk_redirect_mask;
	if (ctx->relocations.thunk_redirect_keys[j] == UINT64_MAX)
		ctx->relocations.thunk_redirect_count++;
	ctx->relocations.thunk_redirect_keys[j] = k;
	ctx->relocations.thunk_redirect_values[j] = th;
}
static uint32_t a64_ld_thunk_relocation_find(struct ld_context *ctx, uint32_t isec, uint32_t ri)
{
	if (!ctx->relocations.thunk_redirect_keys)
		return LD_NONE;
	uint64_t k = (uint64_t)isec << 32 | ri;
	uint32_t j = (uint32_t)(k * 0x9E3779B97F4A7C15ULL >> 32) & ctx->relocations.thunk_redirect_mask;
	while (ctx->relocations.thunk_redirect_keys[j] != UINT64_MAX) {
		if (ctx->relocations.thunk_redirect_keys[j] == k)
			return ctx->relocations.thunk_redirect_values[j];
		j = (j + 1) & ctx->relocations.thunk_redirect_mask;
	}
	return LD_NONE;
}

static bool ld_value_fits_signed(int64_t v, int bits);
static uint64_t a64_ld_thunk_address(struct ld_context *ctx, uint32_t t)
{
	a64_ld_thunk *th = &ctx->relocations.thunks.v[t];
	return ld_input_section_address(ctx, th->thunk_section, th->offset);
}
static uint64_t a64_ld_thunk_destination(struct ld_context *ctx, uint32_t t)
{
	a64_ld_thunk *th = &ctx->relocations.thunks.v[t];
	if (th->global_symbol != LD_NONE)
		return ld_global_symbol_address(ctx, th->global_symbol) + th->addend;
	return ld_input_section_address(ctx, th->input_section, th->value) + th->addend;
}

static bool a64_ld_branch_is_in_range(uint64_t src, uint64_t dst)
{
	uint64_t range = 128 * 1024 * 1024;
	if (dst > src)
		return dst - src <= range - 4;
	return src - dst <= range;
}
