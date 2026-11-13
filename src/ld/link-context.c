/* The start of one link's context: all zero but the fields named here. */

/* every field of the context starts at zero but these; ld_main calls it
   under ld_link_lock before the link, so nothing survives from the last link */
static void ld_context_init(struct ld_context *ctx, FILE *error_stream)
{
	memset(ctx, 0, sizeof *ctx);
	ctx->diagnostics.error_stream = error_stream;
	ctx->diagnostics.error_limit = 20;
	ctx->diagnostics.phase_timing = getenv("VLD_TIME") != NULL;
	struct ld_output *o = &ctx->output;
	o->rela_dyn_section = o->build_id_section = o->comment_section = o->got_section = LD_NONE;
	o->eh_frame_header_section = o->symtab_section = o->strtab_section = o->shstrtab_section = LD_NONE;
	o->dynsym_section = o->dynstr_section = o->hash_section = o->gnu_hash_section = LD_NONE;
	o->versym_section = o->verdef_section = o->dynamic_section = LD_NONE;
}
