/* --gc-sections as lld's MarkLive. */

/* --gc-sections as lld's MarkLive: roots are the entry symbol, KEEP, SHF_GNU_RETAIN,
 * non-allocated sections, init/fini/preinit arrays, notes outside groups,
 * .ctors/.dtors/.init/.fini/.jcr, and under -shared the exported definitions;
 * relocations, SHF_LINK_ORDER and section groups propagate liveness; a
 * __start_X/__stop_X reference keeps nothing (lld's default -z start-stop-gc) */
static void gc_sections(const char *entry)
{
	uint8_t *live = calloc(isecs.n, 1);
	uint32_t *work = malloc(isecs.n * sizeof(uint32_t)), nw = 0;
#define MARK(x) do { uint32_t m_ = (x); if (m_ < isecs.n && !live[m_] && isecs.v[m_].kind != K_NONE) { live[m_] = 1; work[nw++] = m_; } } while (0)

	ncache_init();
	for (uint32_t i = 0; i < isecs.n; i++) {
		ISec *is = &isecs.v[i];

		if (is->kind == K_NONE || is->obj == NONE)
			continue;
		if (eh_input(is)) {
			/* lld MarkLive::scanEhFrameSection: kept, but its relocations do not
			 * keep the functions; a CIE's (the personality) keep their targets, an
			 * FDE's only outside code and section groups (an LSDA) */
			uint32_t nrec;
			EhRec *rec = eh_split(i, &nrec);
			const Obj *o = objs.v[is->obj];

			live[i] = 1;
			for (uint32_t r = 0; rec && r < nrec; r++)
				for (uint32_t k = rec[r].r0; k < rec[r].r1; k++) {
					uint32_t t = rel_isec(o, isecs.v[i].rela[k].r_info >> 32);

					if (t == NONE)
						continue;
					if (!rec[r].cie && ((isecs.v[t].flags & SHF_EXECINSTR) || isecs.v[t].gnext))
						continue;
					MARK(t);
				}
			free(rec);
			continue;
		}
		const char *n = is->name;
		uint32_t d = match_isd(n);
		bool root = !(is->flags & SHF_ALLOC) || (is->flags & 0x200000) /* SHF_GNU_RETAIN */ ||
			    is->type == SHT_INIT_ARRAY || is->type == SHT_FINI_ARRAY || is->type == SHT_PREINIT_ARRAY ||
			    (is->type == SHT_NOTE && !is->gnext) ||
			    !strncmp(n, ".ctors", 6) || !strncmp(n, ".dtors", 6) || !strncmp(n, ".init", 5) ||
			    !strncmp(n, ".fini", 5) || !strncmp(n, ".jcr", 4) || (d != NONE && isds.v[d].keep);

		if (root)
			MARK(i);
	}
	uint32_t eg = gfind(entry);

	if (eg != NONE && gsyms[eg].state == GS_OBJ)
		MARK(gsyms[eg].sec);
	for (uint32_t g = 0; opt.shared && g < atomic_load(&ngsyms); g++)
		if (gsyms[g].state == GS_OBJ && (gsyms[g].vis == STV_DEFAULT || gsyms[g].vis == STV_PROTECTED))
			MARK(gsyms[g].sec);
	for (bool again = true; again;) {
		while (nw) {
			ISec *is = &isecs.v[work[--nw]];
			Obj *o = objs.v[is->obj];

			for (uint32_t r = 0; r < is->nrela; r++) {
				uint32_t y = is->rela[r].r_info >> 32;

				if (!y)
					continue;
				if (y >= o->firstg) {
					G *gs = &gsyms[o->gs[y - o->firstg]];

					if (gs->state == GS_OBJ)
						MARK(gs->sec);
				} else if (o->sym[y].st_shndx && o->sym[y].st_shndx < 0xff00)
					MARK(o->isec0 + o->sym[y].st_shndx);
			}
			if (is->gnext)
				MARK(is->gnext - 1);
		}
		/* SHF_LINK_ORDER: live when the section it links to is */
		again = false;
		for (uint32_t i = 0; i < isecs.n; i++) {
			ISec *is = &isecs.v[i];

			if (live[i] || is->kind == K_NONE || is->obj == NONE || !(is->flags & 0x80))
				continue;
			const Obj *o = objs.v[is->obj];
			uint32_t l = o->sh[is->shndx].sh_link;

			if (l && l < o->nsh && live[o->isec0 + l]) {
				MARK(i);
				again = true;
			}
		}
	}
	for (uint32_t i = 0; i < isecs.n; i++) {
		ISec *is = &isecs.v[i];

		if (live[i] || is->kind == K_NONE || is->obj == NONE)
			continue;
		if (opt.print_gc)
			printf("removing unused section %s:(%s)\n", objs.v[is->obj]->path, is->name);	/* lld: stdout */
		is->kind = K_NONE;
	}
#undef MARK
	free(live);
	free(work);
}
