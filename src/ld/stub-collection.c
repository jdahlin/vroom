/* Collecting the undefined symbols the stub object is to define. */

/* names of undefined symbols that the -O0 stub object defines */
static const char **stub_names;
static uint32_t nstubs;
static void mark_target(Assign *a)
{
	if (a->sym != NONE)
		gsyms[a->sym].hidden_script = 1;	/* reused as "assigned by the script" */
}

static void collect_stubs(void)
{
	for_assigns(mark_target);
	VVEC(const char *) names = {0};
	for (uint32_t i = 0; i < objs.n; i++) {
		Obj *o = objs.v[i];
		if (!o->buf)
			continue;
		for (uint32_t s = o->firstg; s < o->nsym; s++) {
			uint32_t g = o->gs[s - o->firstg];
			G *gs = &gsyms[g];
			if (o->sym[s].st_shndx != SHN_UNDEF || g_defined_obj(g) || gs->hidden_script || gs->dup)
				continue;
			if ((o->sym[s].st_info >> 4) == STB_WEAK)
				continue;
			gs->dup = 1;
			VVEC_PUSH(names, gname(g), alloc);
		}
	}
	for_assigns(mark_target);	/* idempotent */
	qsort(names.v, names.n, sizeof(char *), cmp_str);
	stub_names = names.v;
	nstubs = names.n;
	for (uint32_t i = 0; i < nstubs; i++)
		gsyms[gfind(stub_names[i])].dup = 0;
}

static void write_stub_list(void)
{
	char *p = alloc(strlen(opt.out) + 16);
	sprintf(p, "%s.vroom-stubs", opt.out);
	FILE *f = NULL;
	int fd = openat(dirfd_base, p, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
		f = fdopen(fd, "w");
	if (!f) {
		error("cannot write %s", p);
		return;
	}
	for (uint32_t i = 0; i < nstubs; i++)
		fprintf(f, "%s\n", stub_names[i]);
	fclose(f);
}
