/* Collecting the undefined symbols the stub object is to define. */

#include "ld-internal.h"

/* names of undefined symbols that the -O0 stub object defines */
static void ld_stub_target_mark(struct ld_context *ctx, ld_assignment *a)
{
	if (a->symbol != LD_NONE)
		ctx->symbols.globals[a->symbol].hidden_by_script = 1;	/* reused as "assigned by the script" */
}

VPRIV void ld_stubs_collect(struct ld_context *ctx)
{
	ld_script_assignments_for_each(ctx, ld_stub_target_mark);
	VVEC(const char *) names = {0};
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++) {
		ld_object *o = ctx->inputs.objects.v[i];
		if (!o->file_bytes)
			continue;
		for (uint32_t s = o->first_global_index; s < o->symbol_count; s++) {
			uint32_t g = o->global_slots[s - o->first_global_index];
			ld_global_symbol *gs = &ctx->symbols.globals[g];
			if (o->symbols[s].st_shndx != SHN_UNDEF || ld_global_symbol_is_defined_by_object(ctx, g) || gs->hidden_by_script || gs->duplicate)
				continue;
			if ((o->symbols[s].st_info >> 4) == STB_WEAK)
				continue;
			gs->duplicate = 1;
			VVEC_PUSH(names, ld_global_name(ctx, g), ld_region_alloc);
		}
	}
	ld_script_assignments_for_each(ctx, ld_stub_target_mark);	/* idempotent */
	if (names.n)	/* (no stubs: names.v is NULL, and qsort's base is nonnull) */
		qsort(names.v, names.n, sizeof(char *), ld_string_cmp);
	ctx->symbols.stub_names = names.v;
	ctx->symbols.stub_count = names.n;
	for (uint32_t i = 0; i < ctx->symbols.stub_count; i++)
		ctx->symbols.globals[ld_global_name_find(ctx, ctx->symbols.stub_names[i])].duplicate = 0;
}

VPRIV void ld_stub_list_write(struct ld_context *ctx)
{
	char *p = ld_region_alloc(strlen(ctx->options.output_path) + 16);
	sprintf(p, "%s.vroom-stubs", ctx->options.output_path);
	FILE *f = NULL;
	int fd = openat(ctx->options.directory_fd, p, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
		f = fdopen(fd, "w");
	if (!f) {
		ld_error_report(ctx, "cannot write %s", p);
		return;
	}
	for (uint32_t i = 0; i < ctx->symbols.stub_count; i++)
		fprintf(f, "%s\n", ctx->symbols.stub_names[i]);
	fclose(f);
}
