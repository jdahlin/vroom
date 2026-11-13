/* Collecting the undefined symbols the stub object is to define. */

/* names of undefined symbols that the -O0 stub object defines */
static void ld_stub_target_mark(ld_assignment *a)
{
	if (a->sym != LD_NONE)
		ctx->symbols.globals[a->sym].hidden_script = 1;	/* reused as "assigned by the script" */
}

static void ld_stubs_collect(void)
{
	ld_script_assignments_for_each(ld_stub_target_mark);
	VVEC(const char *) names = {0};
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++) {
		ld_object *o = ctx->inputs.objects.v[i];
		if (!o->buf)
			continue;
		for (uint32_t s = o->firstg; s < o->nsym; s++) {
			uint32_t g = o->gs[s - o->firstg];
			ld_global_symbol *gs = &ctx->symbols.globals[g];
			if (o->sym[s].st_shndx != SHN_UNDEF || ld_global_symbol_is_defined_by_object(g) || gs->hidden_script || gs->dup)
				continue;
			if ((o->sym[s].st_info >> 4) == STB_WEAK)
				continue;
			gs->dup = 1;
			VVEC_PUSH(names, ld_global_name(g), ld_region_alloc);
		}
	}
	ld_script_assignments_for_each(ld_stub_target_mark);	/* idempotent */
	if (names.n)	/* (no stubs: names.v is NULL, and qsort's base is nonnull) */
		qsort(names.v, names.n, sizeof(char *), ld_string_cmp);
	ctx->symbols.stub_names = names.v;
	ctx->symbols.stub_count = names.n;
	for (uint32_t i = 0; i < ctx->symbols.stub_count; i++)
		ctx->symbols.globals[ld_global_name_find(ctx->symbols.stub_names[i])].dup = 0;
}

static void ld_stub_list_write(void)
{
	char *p = ld_region_alloc(strlen(ctx->options.out) + 16);
	sprintf(p, "%s.vroom-stubs", ctx->options.out);
	FILE *f = NULL;
	int fd = openat(ctx->options.directory_fd, p, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
		f = fdopen(fd, "w");
	if (!f) {
		ld_error_report("cannot write %s", p);
		return;
	}
	for (uint32_t i = 0; i < ctx->symbols.stub_count; i++)
		fprintf(f, "%s\n", ctx->symbols.stub_names[i]);
	fclose(f);
}
