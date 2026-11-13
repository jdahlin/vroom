/* Writing .eh_frame_hdr (lld's EhFrameHeader::write). */

#include "ld-internal.h"

/* .eh_frame_hdr (lld EhFrameHeader::write): version 1, eh_frame_ptr pcrel
 * sdata4, fde_count udata4, a table datarel sdata4 of (initial location,
 * FDE) sorted by location, duplicate locations dropped; read from the
 * relocated FDEs */
static int ld_eh_frame_header_pair_cmp(const void *a, const void *b)
{
	const uint64_t *x = a, *y = b;
	return x[0] != y[0] ? (x[0] < y[0] ? -1 : 1) : x[1] < y[1] ? -1 : x[1] > y[1];	/* lld stable_sort: output order */
}
VPRIV void ld_eh_frame_header_write(struct ld_context *ctx)
{
	if (ctx->output.eh_frame_header_section == LD_NONE)
		return;
	uint32_t ho = ld_input_section_output_section(ctx, ctx->output.eh_frame_header_section);

	if (ho == LD_NONE || ho == LD_OUTPUT_SECTION_DISCARD)
		return;
	uint64_t hva = ld_input_section_address(ctx, ctx->output.eh_frame_header_section, 0);
	uint8_t *h = ctx->output.image + ctx->script.sections.v[ho].offset + ctx->inputs.sections.v[ctx->output.eh_frame_header_section].offset;
	uint64_t (*t)[2] = ld_region_alloc((ctx->layout.eh_frame_fdes.n + 1) * sizeof *t);
	uint32_t n = 0;
	uint64_t ehva = 0;

	for (uint32_t k = 0; k < ctx->layout.eh_frame_fdes.n; k++) {
		uint32_t f = ctx->layout.eh_frame_fdes.v[k], fo = ld_input_section_output_section(ctx, f);
		const uint8_t *b = ctx->output.image + ctx->script.sections.v[fo].offset + ctx->inputs.sections.v[f].offset;
		uint64_t fva = ld_input_section_address(ctx, f, 0);

		if (!k)
			ehva = ctx->script.sections.v[fo].address;
		t[n][0] = fva + 8 + (uint64_t)(int64_t)(int32_t)ld_u32_load(b + 8);	/* pc_begin: pcrel sdata4 (vcc, clang) */
		t[n][1] = fva;
		n++;
	}
	if (!ctx->layout.eh_frame_fdes.n && ctx->inputs.sections.n)
		for (uint32_t i = 0; i < ctx->inputs.sections.n; i++)
			if (!strcmp(ctx->inputs.sections.v[i].name, ".eh_frame") && ctx->inputs.sections.v[i].kind == LD_SECTION_KIND_NORMAL && ld_input_section_output_section(ctx, i) < ctx->script.sections.n) {
				ehva = ctx->script.sections.v[ld_input_section_output_section(ctx, i)].address;
				break;
			}
	qsort(t, n, sizeof *t, ld_eh_frame_header_pair_cmp);
	uint32_t m = 0;
	for (uint32_t k = 0; k < n; k++)
		if (!m || t[k][0] != t[m - 1][0]) {
			t[m][0] = t[k][0];
			t[m][1] = t[k][1];
			m++;
		}
	h[0] = 1;
	h[1] = 0x1b;	/* DW_EH_PE_pcrel | sdata4 */
	h[2] = 0x03;	/* udata4 */
	h[3] = 0x3b;	/* DW_EH_PE_datarel | sdata4 */
	ld_u32_store(h + 4, (uint32_t)(ehva - (hva + 4)));
	ld_u32_store(h + 8, m);
	for (uint32_t k = 0; k < m; k++) {
		ld_u32_store(h + 12 + 8 * k, (uint32_t)(t[k][0] - hva));
		ld_u32_store(h + 16 + 8 * k, (uint32_t)(t[k][1] - hva));
	}
}
