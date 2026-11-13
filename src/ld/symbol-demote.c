/* Demoting symbols to local, and the __start_X/__stop_X symbols. */

static int cmp_dyn(const void *a, const void *b)
{
	uint64_t x = isec_va(*(const uint32_t *)a, 0), y = isec_va(*(const uint32_t *)b, 0);
	return (x > y) - (x < y);
}

static void demote_symbols(void *arg, size_t i)
{
	(void)arg;
	G *s = &gsyms[gorder.v[i]];
	if (s->state == GS_OBJ && s->sec != NONE) {
		uint32_t os = isec_osec(s->sec);
		if (isecs.v[s->sec].kind == K_NONE || os == NONE || os == OS_DISCARD)
			s->demoted = 1;
	}
}

static void finish_syms(void *arg, size_t i) { finish_sym(arg, i); }

static VVEC(uint32_t) stop_syms;	/* __stop_X: the section size, after layout */

/* lld addStartStopSymbols: a referenced, undefined __start_X/__stop_X is the
 * start/end of output section X (X a C identifier), protected, global */
static void start_stop_symbols(void)
{
	for (uint32_t i = 0; i < gorder.n; i++) {
		G *s = &gsyms[gorder.v[i]];
		const char *n = gname(gorder.v[i]), *x;

		if (s->state != GS_UNDEF || !(x = start_stop_name(n)))
			continue;
		for (uint32_t k = 0; k < topcmds.n; k++) {
			if (topcmds.v[k].kind != C_OSEC || strcmp(osecs.v[topcmds.v[k].idx].name, x) ||
			    osecs.v[topcmds.v[k].idx].discard)
				continue;
			s->state = GS_SCRIPT;
			s->ev = (EV){.kind = EV_OSEC, .idx = topcmds.v[k].idx, .align = 1};
			s->vis = STV_PROTECTED;
			s->bind = STB_GLOBAL;
			s->type = STT_NOTYPE;
			if (n[2] == 's' && n[3] == 't' && n[4] == 'o')
				VVEC_PUSH(stop_syms, gorder.v[i], alloc);
			break;
		}
	}
}
