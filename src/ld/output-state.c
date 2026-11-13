/* The state of the output image: file buffer, program headers, the ids of the synthetic sections, and ld_input_section_create(). */

/* ---------------- output state ---------------- */

static uint32_t nobj_real;		/* objects excluding synthetic */

static uint32_t ld_input_section_create(const char *name, uint8_t kind, uint32_t type, uint64_t flags, uint32_t align)
{
	ld_input_section is = {.name = name, .kind = kind, .type = type, .flags = flags, .align = align,
		   .obj = LD_NONE, .osec = LD_NONE, .isd = LD_NONE, .aux = LD_NONE, .shndx = LD_NONE};
	VVEC_PUSH(isecs, is, ld_region_alloc);
	return isecs.n - 1;
}
