/* The state of the output image: file buffer, program headers, the ids of the synthetic sections, and ld_input_section_create(). */

/* ---------------- output state ---------------- */


static uint32_t ld_input_section_create(struct ld_context *ctx, const char *name, uint8_t kind, uint32_t type, uint64_t flags, uint32_t align)
{
	ld_input_section is = {.name = name, .kind = kind, .type = type, .flags = flags, .alignment = align,
		   .object = LD_NONE, .output_section = LD_NONE, .input_description = LD_NONE, .auxiliary_index = LD_NONE, .section_header_index = LD_NONE};
	VVEC_PUSH(ctx->inputs.sections, is, ld_region_alloc);
	return ctx->inputs.sections.n - 1;
}
