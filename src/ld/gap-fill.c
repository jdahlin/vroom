/* The script's =fill patterns between and after input sections. */

/* =fill: lld fills before, between and after the input sections, restarting the pattern per gap */
static void ld_gap_fill(uint8_t *b, uint64_t n, const uint8_t f[4])
{
	uint64_t i = 0;
	for (; i + 4 < n; i += 4)
		memcpy(b + i, f, 4);
	memcpy(b + i, f, n - i);
}
static void ld_gap_fills_write(void)
{
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		if (ctx->script.commands.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		ld_output_section *os = &ctx->script.sections.v[ctx->script.commands.v[c].idx];
		/* without =FILL lld pads executable sections with the target's trap
		   instruction: int3 on x86-64 */
		bool trap = !os->fill && ctx->inputs.x86 && (os->flags & SHF_EXECINSTR);
		if (!os->live || (!os->fill && !trap) || os->type == SHT_NOBITS)
			continue;
		uint32_t v = trap ? 0xccccccccu : (uint32_t)ld_expression_value_resolve(ld_expression_eval(os->fill));
		if (!v)
			continue;
		uint8_t f[4] = {v >> 24, v >> 16, v >> 8, v};	/* big-endian (lld readFill) */
		uint8_t *b = ctx->output.image + os->offset;
		uint64_t at = 0;
		for (uint32_t k = 0; k < os->cmds.n; k++) {
			if (os->cmds.v[k].kind != LD_COMMAND_INPUT_SECTION_DESCRIPTION)
				continue;
			ld_input_section_description *d = &ctx->script.input_descriptions.v[os->cmds.v[k].idx];
			for (uint32_t q = 0; q < d->secs.n; q++) {
				ld_input_section *is = &ctx->inputs.sections.v[d->secs.v[q]];
				if (is->off > at)
					ld_gap_fill(b + at, is->off - at, f);
				at = is->off + is->size;
			}
		}
		if (os->size > at)
			ld_gap_fill(b + at, os->size - at, f);
	}
}
