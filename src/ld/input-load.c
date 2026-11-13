/* Loading all inputs, allocating their sections, and setting up and freeing the global table. */

static void ld_inputs_load(void)
{
	bool lazy = false;

	for (uint32_t i = 0; i < ctx->options.inputs.n; i++)
		if (!ctx->options.inputs.v[i].whole) {
			const char *p = ctx->options.inputs.v[i].path;
			size_t l = strlen(p);

			lazy |= l > 2 && !strcmp(p + l - 2, ".a");
		}
	for (uint32_t i = 0; i < ctx->options.inputs.n; i++) {
		const char *p = ctx->options.inputs.v[i].path;
		size_t n;
		uint8_t *b = ld_file_map(p, &n);
		if (!b)
			continue;
		if (n >= 8 && (!memcmp(b, "!<arch>\n", 8) || !memcmp(b, "!<thin>\n", 8))) {
			if (lazy && !ctx->options.inputs.v[i].whole) {
				uint32_t before = ctx->inputs.pending.n, c0 = ctx->inputs.lazy_candidates.n;

				ld_archive_add(p, b, n);
				for (uint32_t k = before; k < ctx->inputs.pending.n; k++) {
					VVEC_PUSH(ctx->inputs.lazy_candidates, ctx->inputs.pending.v[k], ld_region_alloc);
					VVEC_PUSH(ctx->inputs.lazy_taken, 0, ld_region_alloc);
				}
				ctx->inputs.pending.n = before;
				/* an input marker: the archive's members are read here */
				VVEC_PUSH(ctx->inputs.pending, ((ld_pending_member){.path = NULL, .size = c0, .buf = (const uint8_t *)(uintptr_t)ctx->inputs.lazy_candidates.n}), ld_region_alloc);
				continue;
			}
			ld_archive_add(p, b, n);
			continue;
		}
		VVEC_PUSH(ctx->inputs.pending, ((ld_pending_member){.path = p, .buf = b, .size = n}), ld_region_alloc);
	}
	if (lazy) {
		for (uint32_t i = 0; i < ctx->inputs.pending.n; i++)
			if (ctx->inputs.pending.v[i].path)
				ld_lazy_object_add(ctx->inputs.pending.v[i]);
			else
				ld_lazy_archive_process(ctx->inputs.pending.v[i].size, (uint32_t)(uintptr_t)ctx->inputs.pending.v[i].buf);
		ctx->inputs.pending = ctx->inputs.lazy_order;
	}
	for (uint32_t i = 0; i < ctx->inputs.pending.n; i++) {
		ld_object *o = ld_region_alloc(sizeof *o);
		o->path = ctx->inputs.pending.v[i].path;
		o->buf = ctx->inputs.pending.v[i].buf;
		o->size = ctx->inputs.pending.v[i].size;
		o->idx = ctx->inputs.objects.n;
		VVEC_PUSH(ctx->inputs.objects, o, ld_region_alloc);
	}
	ld_parallel_for(ctx->inputs.objects.n, ld_object_parse, NULL);
	if (!ctx->options.max_page)
		ctx->options.max_page = ctx->inputs.x86 ? 4096 : 65536;
}

static void ld_input_sections_alloc(uint32_t from)
{
	uint32_t total = ctx->inputs.sections.n;
	for (uint32_t i = from; i < ctx->inputs.objects.n; i++) {
		ctx->inputs.objects.v[i]->isec0 = total;
		total += ctx->inputs.objects.v[i]->nsh;
	}
	if (total > ctx->inputs.sections.cap) {
		uint32_t nc = ctx->inputs.sections.cap ? ctx->inputs.sections.cap : 1024;
		while (nc < total + 1024)
			nc *= 2;
		ld_input_section *nv = ld_region_alloc((size_t)nc * sizeof(ld_input_section));
		if (ctx->inputs.sections.n)
			memcpy(nv, ctx->inputs.sections.v, ctx->inputs.sections.n * sizeof(ld_input_section));
		ctx->inputs.sections.v = nv;
		ctx->inputs.sections.cap = nc;
	}
	memset(&ctx->inputs.sections.v[ctx->inputs.sections.n], 0, (size_t)(total - ctx->inputs.sections.n) * sizeof(ld_input_section));
	ctx->inputs.sections.n = total;
}

static void ld_global_table_init(void)
{
	uint64_t total = 1024;
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++)
		if (ctx->inputs.objects.v[i]->nsym > ctx->inputs.objects.v[i]->firstg)
			total += ctx->inputs.objects.v[i]->nsym - ctx->inputs.objects.v[i]->firstg;
	uint32_t cap = 4096;
	while (cap < total * 2 + 4096)
		cap *= 2;
	ctx->symbols.slot_mask = cap - 1;
	ctx->symbols.slot_names = ld_region_large_alloc((size_t)cap * sizeof(*ctx->symbols.slot_names));
	ctx->symbols.slot_words = ld_region_large_alloc((size_t)cap * sizeof(*ctx->symbols.slot_words));
	ctx->symbols.names = ld_region_large_alloc((size_t)cap * sizeof(*ctx->symbols.names));
	ctx->symbols.globals = ld_region_large_alloc((size_t)cap * sizeof(ld_global_symbol));
	atomic_store(&ctx->symbols.count, 0);
}

static void ld_global_table_free(void)
{
	ld_region_unmap((void *)ctx->symbols.slot_names, (size_t)(ctx->symbols.slot_mask + 1) * sizeof(*ctx->symbols.slot_names));
	ld_region_unmap((void *)ctx->symbols.slot_words, (size_t)(ctx->symbols.slot_mask + 1) * sizeof(*ctx->symbols.slot_words));
	ld_region_unmap(ctx->symbols.names, (size_t)(ctx->symbols.slot_mask + 1) * sizeof(*ctx->symbols.names));
	ld_region_unmap(ctx->symbols.globals, (size_t)(ctx->symbols.slot_mask + 1) * sizeof(ld_global_symbol));
	ctx->symbols.slot_names = NULL;
}
