/* The state of the output image: file buffer, program headers, the ids of the synthetic sections, and ld_input_section_create(). */

/* ---------------- output state ---------------- */


static uint32_t ld_input_section_create(struct ld_context *ctx, const char *name, uint8_t kind, uint32_t type, uint64_t flags, uint32_t align)
{
	ld_input_section is = {.name = name, .kind = kind, .type = type, .flags = flags, .align = align,
		   .obj = LD_NONE, .osec = LD_NONE, .isd = LD_NONE, .aux = LD_NONE, .shndx = LD_NONE};
	VVEC_PUSH(ctx->inputs.sections, is, ld_region_alloc);
	return ctx->inputs.sections.n - 1;
}
