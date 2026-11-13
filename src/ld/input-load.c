/* Loading all inputs, allocating their sections, and setting up and freeing the global table. */

static void ld_inputs_load(void)
{
	bool lazy = false;

	for (uint32_t i = 0; i < opt.inputs.n; i++)
		if (!opt.inputs.v[i].whole) {
			const char *p = opt.inputs.v[i].path;
			size_t l = strlen(p);

			lazy |= l > 2 && !strcmp(p + l - 2, ".a");
		}
	for (uint32_t i = 0; i < opt.inputs.n; i++) {
		const char *p = opt.inputs.v[i].path;
		size_t n;
		uint8_t *b = ld_file_map(p, &n);
		if (!b)
			continue;
		if (n >= 8 && (!memcmp(b, "!<arch>\n", 8) || !memcmp(b, "!<thin>\n", 8))) {
			if (lazy && !opt.inputs.v[i].whole) {
				uint32_t before = pending.n, c0 = lcand.n;

				ld_archive_add(p, b, n);
				for (uint32_t k = before; k < pending.n; k++) {
					VVEC_PUSH(lcand, pending.v[k], ld_region_alloc);
					VVEC_PUSH(ltaken, 0, ld_region_alloc);
				}
				pending.n = before;
				/* an input marker: the archive's members are read here */
				VVEC_PUSH(pending, ((ld_pending_member){.path = NULL, .size = c0, .buf = (const uint8_t *)(uintptr_t)lcand.n}), ld_region_alloc);
				continue;
			}
			ld_archive_add(p, b, n);
			continue;
		}
		VVEC_PUSH(pending, ((ld_pending_member){.path = p, .buf = b, .size = n}), ld_region_alloc);
	}
	if (lazy) {
		for (uint32_t i = 0; i < pending.n; i++)
			if (pending.v[i].path)
				ld_lazy_object_add(pending.v[i]);
			else
				ld_lazy_archive_process(pending.v[i].size, (uint32_t)(uintptr_t)pending.v[i].buf);
		pending = lout;
	}
	for (uint32_t i = 0; i < pending.n; i++) {
		ld_object *o = ld_region_alloc(sizeof *o);
		o->path = pending.v[i].path;
		o->buf = pending.v[i].buf;
		o->size = pending.v[i].size;
		o->idx = objs.n;
		VVEC_PUSH(objs, o, ld_region_alloc);
	}
	ld_parallel_for(objs.n, ld_object_parse, NULL);
	if (!opt.max_page)
		opt.max_page = x86_ld ? 4096 : 65536;
}

static void ld_input_sections_alloc(uint32_t from)
{
	uint32_t total = isecs.n;
	for (uint32_t i = from; i < objs.n; i++) {
		objs.v[i]->isec0 = total;
		total += objs.v[i]->nsh;
	}
	if (total > isecs.cap) {
		uint32_t nc = isecs.cap ? isecs.cap : 1024;
		while (nc < total + 1024)
			nc *= 2;
		ld_input_section *nv = ld_region_alloc((size_t)nc * sizeof(ld_input_section));
		if (isecs.n)
			memcpy(nv, isecs.v, isecs.n * sizeof(ld_input_section));
		isecs.v = nv;
		isecs.cap = nc;
	}
	memset(&isecs.v[isecs.n], 0, (size_t)(total - isecs.n) * sizeof(ld_input_section));
	isecs.n = total;
}

static void ld_global_table_init(void)
{
	uint64_t total = 1024;
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->nsym > objs.v[i]->firstg)
			total += objs.v[i]->nsym - objs.v[i]->firstg;
	uint32_t cap = 4096;
	while (cap < total * 2 + 4096)
		cap *= 2;
	hmask = cap - 1;
	hnames = ld_region_large_alloc((size_t)cap * sizeof(*hnames));
	hword = ld_region_large_alloc((size_t)cap * sizeof(*hword));
	gnames = ld_region_large_alloc((size_t)cap * sizeof(*gnames));
	gsyms = ld_region_large_alloc((size_t)cap * sizeof(ld_global_symbol));
	atomic_store(&ngsyms, 0);
}

static void ld_global_table_free(void)
{
	ld_region_unmap((void *)hnames, (size_t)(hmask + 1) * sizeof(*hnames));
	ld_region_unmap((void *)hword, (size_t)(hmask + 1) * sizeof(*hword));
	ld_region_unmap(gnames, (size_t)(hmask + 1) * sizeof(*gnames));
	ld_region_unmap(gsyms, (size_t)(hmask + 1) * sizeof(ld_global_symbol));
	hnames = NULL;
}
