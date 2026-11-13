/* Demoting symbols to local, and the __start_X/__stop_X symbols. */

static void ld_symbols_demote(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_global_symbol *s = &ctx->symbols.globals[ctx->symbols.order.v[i]];
	if (s->state == LD_SYMBOL_STATE_OBJECT && s->input_section != LD_NONE) {
		uint32_t os = ld_input_section_output_section(ctx, s->input_section);
		if (ctx->inputs.sections.v[s->input_section].kind == LD_SECTION_KIND_NONE || os == LD_NONE || os == LD_OUTPUT_SECTION_DISCARD)
			s->demoted = 1;
	}
}

static void ld_global_symbol_finish_task(void *arg, size_t i) { ld_global_symbol_finish(arg, i); }


/* lld addStartStopSymbols: a referenced, undefined __start_X/__stop_X is the
 * start/end of output section X (X a C identifier), protected, global */
static void ld_start_stop_symbols_define(struct ld_context *ctx)
{
	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		ld_global_symbol *s = &ctx->symbols.globals[ctx->symbols.order.v[i]];
		const char *n = ld_global_name(ctx, ctx->symbols.order.v[i]), *x;

		if (s->state != LD_SYMBOL_STATE_UNDEFINED || !(x = ld_start_stop_section_name(n)))
			continue;
		for (uint32_t k = 0; k < ctx->script.commands.n; k++) {
			if (ctx->script.commands.v[k].kind != LD_COMMAND_OUTPUT_SECTION || strcmp(ctx->script.sections.v[ctx->script.commands.v[k].index].name, x) ||
			    ctx->script.sections.v[ctx->script.commands.v[k].index].discard)
				continue;
			s->state = LD_SYMBOL_STATE_SCRIPT;
			s->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_OUTPUT_SECTION, .index = ctx->script.commands.v[k].index, .alignment = 1};
			s->visibility = STV_PROTECTED;
			s->binding = STB_GLOBAL;
			s->type = STT_NOTYPE;
			if (n[2] == 's' && n[3] == 't' && n[4] == 'o')
				VVEC_PUSH(ctx->symbols.stop_symbols, ctx->symbols.order.v[i], ld_region_alloc);
			break;
		}
	}
}
