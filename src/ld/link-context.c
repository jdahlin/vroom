/* The start of one link's context: all zero but the fields named here. */

/* every field of the context starts at zero but these; ld_main calls it
   under ld_link_lock before the link, so nothing survives from the last link */
static void ld_context_init(struct ld_context *ctx, FILE *error_stream)
{
	memset(ctx, 0, sizeof *ctx);
	ctx->diagnostics.error_stream = error_stream;
	ctx->diagnostics.error_limit = 20;
	ctx->diagnostics.phase_timing = getenv("VLD_TIME") != NULL;
}
