/* One link's context: its creation, all zero but the fields named here, and its end. */

/* a new context for one link: every field starts at zero but these; NULL
   when out of memory */
static struct ld_context *ld_context_create(FILE *error_stream, int directory_fd)
{
	struct ld_context *ctx = calloc(1, sizeof *ctx);

	if (!ctx)
		return NULL;
	ctx->options.directory_fd = directory_fd;
	ctx->diagnostics.error_stream = error_stream;
	ctx->diagnostics.error_limit = 20;
	ctx->diagnostics.phase_timing = getenv("VLD_TIME") != NULL;
	struct ld_output *o = &ctx->output;
	o->rela_dyn_section = o->build_id_section = o->comment_section = o->got_section = LD_NONE;
	o->eh_frame_header_section = o->symtab_section = o->strtab_section = o->shstrtab_section = LD_NONE;
	o->dynsym_section = o->dynstr_section = o->hash_section = o->gnu_hash_section = LD_NONE;
	o->versym_section = o->verdef_section = o->dynamic_section = LD_NONE;
	return ctx;
}

/* the end of a link: the context's own memory (the rest of the link's memory
   is in the regions, which ld_regions_free unmaps) */
static void ld_context_destroy(struct ld_context *ctx)
{
	free(ctx->inputs.lazy_symbols.v);	/* the one malloc'ed field */
	free(ctx);
}
