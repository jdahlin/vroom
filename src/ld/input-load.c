/* Loading all inputs, allocating their sections, and setting up and freeing the global table. */

static void load_inputs(void)
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
		uint8_t *b = map_file(p, &n);
		if (!b)
			continue;
		if (n >= 8 && (!memcmp(b, "!<arch>\n", 8) || !memcmp(b, "!<thin>\n", 8))) {
			if (lazy && !opt.inputs.v[i].whole) {
				uint32_t before = pending.n, c0 = lcand.n;

				add_archive(p, b, n);
				for (uint32_t k = before; k < pending.n; k++) {
					VVEC_PUSH(lcand, pending.v[k], alloc);
					VVEC_PUSH(ltaken, 0, alloc);
				}
				pending.n = before;
				/* an input marker: the archive's members are read here */
				VVEC_PUSH(pending, ((Pending){.path = NULL, .size = c0, .buf = (const uint8_t *)(uintptr_t)lcand.n}), alloc);
				continue;
			}
			add_archive(p, b, n);
			continue;
		}
		VVEC_PUSH(pending, ((Pending){.path = p, .buf = b, .size = n}), alloc);
	}
	if (lazy) {
		for (uint32_t i = 0; i < pending.n; i++)
			if (pending.v[i].path)
				lobject(pending.v[i]);
			else
				larchive(pending.v[i].size, (uint32_t)(uintptr_t)pending.v[i].buf);
		pending = lout;
	}
	for (uint32_t i = 0; i < pending.n; i++) {
		Obj *o = alloc(sizeof *o);
		o->path = pending.v[i].path;
		o->buf = pending.v[i].buf;
		o->size = pending.v[i].size;
		o->idx = objs.n;
		VVEC_PUSH(objs, o, alloc);
	}
	pfor(objs.n, parse_obj, NULL);
	if (!opt.max_page)
		opt.max_page = x86_ld ? 4096 : 65536;
}

static void alloc_isecs(uint32_t from)
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
		ISec *nv = alloc((size_t)nc * sizeof(ISec));
		if (isecs.n)
			memcpy(nv, isecs.v, isecs.n * sizeof(ISec));
		isecs.v = nv;
		isecs.cap = nc;
	}
	memset(&isecs.v[isecs.n], 0, (size_t)(total - isecs.n) * sizeof(ISec));
	isecs.n = total;
}

static void gtable_init(void)
{
	uint64_t total = 1024;
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->nsym > objs.v[i]->firstg)
			total += objs.v[i]->nsym - objs.v[i]->firstg;
	uint32_t cap = 4096;
	while (cap < total * 2 + 4096)
		cap *= 2;
	hmask = cap - 1;
	hnames = bigalloc((size_t)cap * sizeof(*hnames));
	hword = bigalloc((size_t)cap * sizeof(*hword));
	gnames = bigalloc((size_t)cap * sizeof(*gnames));
	gsyms = bigalloc((size_t)cap * sizeof(G));
	atomic_store(&ngsyms, 0);
}

static void gtable_free(void)
{
	unmap((void *)hnames, (size_t)(hmask + 1) * sizeof(*hnames));
	unmap((void *)hword, (size_t)(hmask + 1) * sizeof(*hword));
	unmap(gnames, (size_t)(hmask + 1) * sizeof(*gnames));
	unmap(gsyms, (size_t)(hmask + 1) * sizeof(G));
	hnames = NULL;
}
