/* Finding out whether any relocation needs a GOT slot. */

/* ---------------- the final link ---------------- */

static void ld_got_find(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	const ld_input_section *is = &ctx->inputs.sections.v[i];
	if (is->kind == LD_SECTION_KIND_NONE || atomic_load_explicit(&ctx->relocations.got_seen, memory_order_relaxed))
		return;
	for (uint32_t r = 0; r < is->rela_count; r++) {
		uint32_t t = (uint32_t)is->rela[r].r_info;
		if (t == R_AARCH64_ADR_GOT_PAGE || t == R_AARCH64_LD64_GOT_LO12_NC) {
			atomic_store(&ctx->relocations.got_seen, true);
			return;
		}
	}
}
static bool ld_got_is_needed(struct ld_context *ctx)
{
	atomic_store(&ctx->relocations.got_seen, false);
	if (!ctx->options.relocatable)
		ld_parallel_for(ctx->inputs.sections.n, ctx->inputs.x86 ? x64_ld_got_find : ld_got_find, ctx);
	return atomic_load(&ctx->relocations.got_seen);
}
