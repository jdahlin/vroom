/* The linker script's statements: assignments, PROVIDE, ASSERT, input and output section descriptions, SECTIONS, PHDRS, VERSION, and parse_script. */

static uint32_t get_osec(const char *name)
{
	if (strcmp(name, "/DISCARD/"))
		for (uint32_t i = 0; i < osecs.n; i++)
			if (!strcmp(osecs.v[i].name, name))
				return i;
	OSec o = {.name = name, .type = SHT_PROGBITS, .addralign = 1, .ptload = -1};
	o.discard = !strcmp(name, "/DISCARD/");
	VVEC_PUSH(osecs, o, alloc);
	return osecs.n - 1;
}

/* After `name` was read: op= expr.  Returns NULL if not an assignment. */
static Assign *passign_rest(const char *name)
{
	char op[4];
	int n = peekop(op);
	if (!n || !strchr(op, '=') || !strcmp(op, "==") || !strcmp(op, "<=") || !strcmp(op, ">=") || !strcmp(op, "!="))
		return NULL;
	sp += n;
	Assign *a = alloc(sizeof *a);
	a->op = op[0] == '=' ? '=' : op[0];
	a->sym = strcmp(name, ".") ? gintern(name) : NONE;
	a->e = pexpr();
	return a;
}

static Assign *pprovide(bool hidden)
{
	sexpect('(');
	char *name = sname();
	Assign *a;
	VVEC_PUSH(provides, ((Provide){.sym = gintern(name)}), alloc);
	cur_provide = &provides.v[provides.n - 1];
	a = passign_rest(name);
	cur_provide = NULL;
	if (!a) {
		serr("expected assignment");
		return NULL;
	}
	a->provide = true;
	a->hidden = hidden;
	sexpect(')');
	return a;
}

static Assign *passert(void)
{
	Assign *a = alloc(sizeof *a);
	a->sym = NONE;
	a->op = '=';
	a->assert_ = true;
	Expr *e = mk(E_ASSERT);
	sexpect('(');
	e->a = pexpr();
	sexpect(',');
	e->msg = sname();
	sexpect(')');
	a->e = e;
	return a;
}

static void pinput(OSec *os, uint32_t osi, const char *filepat, bool keep)
{
	if (strcmp(filepat, "*"))
		error("linker script: file pattern '%s' not supported", filepat);
	ISD d = {.osec = osi, .keep = keep, .thunksec = NONE};
	VVEC(Pat) pats = {0};
	sexpect('(');
	while (!satend() && !speek(')')) {
		Pat p = {0};
		VVEC(const char *) g = {0};
		const char *save = sp;
		char *w = spatname();
		if ((!strcmp(w, "SORT") || !strcmp(w, "SORT_BY_NAME") || !strcmp(w, "SORT_BY_ALIGNMENT")) && speek('(')) {
			p.sort = w[5] == 'B' && w[8] == 'A' ? SORT_ALIGN : SORT_NAME;
			sexpect('(');
			while (!satend() && !speek(')'))
				VVEC_PUSH(g, spatname(), alloc);
			sexpect(')');
		} else if (!strcmp(w, "EXCLUDE_FILE")) {
			error("linker script: EXCLUDE_FILE not supported");
			sp = save;
			return;
		} else {
			VVEC_PUSH(g, w, alloc);
			/* consecutive plain globs belong to the same pattern */
			for (;;) {
				const char *s2 = sp;
				sskip();
				if (speek(')'))
					break;
				char *w2 = spatname();
				if (!strncmp(w2, "SORT", 4) || !strcmp(w2, "EXCLUDE_FILE")) {
					sp = s2;
					break;
				}
				VVEC_PUSH(g, w2, alloc);
			}
		}
		p.globs = g.v;
		p.nglobs = g.n;
		VVEC_PUSH(pats, p, alloc);
	}
	sexpect(')');
	d.pats = pats.v;
	d.npats = pats.n;
	VVEC_PUSH(isds, d, alloc);
	Cmd c = {.kind = C_ISD, .idx = isds.n - 1};
	VVEC_PUSH(os->cmds, c, alloc);
}

static void poutsec(const char *name)
{
	uint32_t osi = get_osec(name);
	uint32_t nref0 = refsyms.n;
	OSec *os;
	Expr *addr = NULL;
	bool info = false, noload = false;
	/* optional address and (TYPE) */
	if (!speek(':')) {
		if (speek('(')) {
			const char *save = sp;
			sp++;
			char *t = spatname();
			if (!strcmp(t, "INFO") || !strcmp(t, "NOLOAD") || !strcmp(t, "COPY") || !strcmp(t, "OVERLAY")) {
				sexpect(')');
				info = strcmp(t, "NOLOAD");
				noload = !info;
			} else {
				sp = save;
				addr = pexpr();
			}
		} else {
			addr = pexpr();
			if (speek('(')) {
				sp++;
				char *t = spatname();
				sexpect(')');
				info = strcmp(t, "NOLOAD");
				noload = !info;
			}
		}
	}
	sexpect(':');
	os = &osecs.v[osi];
	os->addr = addr;
	os->nonalloc |= info;
	os->noload |= noload;
	for (;;) {
		if (sword("AT")) { Expr *e = pparen(); osecs.v[osi].lma = e; continue; }
		if (sword("ALIGN")) { Expr *e = pparen(); osecs.v[osi].alignx = e; continue; }
		if (sword("SUBALIGN")) { Expr *e = pparen(); osecs.v[osi].subalign = e; continue; }
		break;
	}
	sexpect('{');
	while (!satend() && !speek('}')) {
		if (sconsume(';'))
			continue;
		if (sword("KEEP")) {
			sexpect('(');
			char *fp = spatname();
			pinput(&osecs.v[osi], osi, fp, true);
			sexpect(')');
			continue;
		}
		if (sword("CONSTRUCTORS"))
			continue;
		if (sword("ASSERT")) {
			Assign *a = passert();
			Cmd c = {.kind = C_ASSIGN, .as = a};
			VVEC_PUSH(osecs.v[osi].cmds, c, alloc);
			sconsume(';');
			continue;
		}
		int pk = sword("PROVIDE") ? 1 : sword("PROVIDE_HIDDEN") ? 2 : sword("HIDDEN") ? 3 : 0;
		if (pk) {
			bool hid = pk >= 2, prov = pk <= 2;
			Assign *a = pprovide(hid);
			if (a) {
				a->provide = prov;
				Cmd c = {.kind = C_ASSIGN, .as = a};
				VVEC_PUSH(osecs.v[osi].cmds, c, alloc);
			}
			sexpect(';');
			continue;
		}
		const char *save = sp;
		char *w = sname();
		Assign *a = passign_rest(w);
		if (a) {
			Cmd c = {.kind = C_ASSIGN, .as = a};
			VVEC_PUSH(osecs.v[osi].cmds, c, alloc);
			sexpect(';');
			continue;
		}
		sp = save;
		w = spatname();
		if (!*w) {
			serr("unexpected token in output section");
			sp = sp_end;
			break;
		}
		pinput(&osecs.v[osi], osi, w, false);
	}
	sexpect('}');
	/* trailers: >region, AT>region, :phdr, =fill */
	for (;;) {
		sskip();
		if (sp < sp_end && *sp == '>') {
			error("linker script: memory regions not supported in %s", name);
			sp = sp_end;
			break;
		}
		if (sp < sp_end && *sp == '=') {
			sp++;
			osecs.v[osi].fill = pprimary();	/* lld readFill: a primary */
			continue;
		}
		if (speek(':') && sp + 1 < sp_end && sp[1] != ':') {
			sp++;
			OSec *o = &osecs.v[osi];
			if (o->nphn == 4)
				serr("too many :phdr names");
			else
				o->phn[o->nphn++] = sname();
			continue;
		}
		break;
	}
	sconsume(',');
	if (refsyms.n > nref0)
		osecs.v[osi].uses_syms = true;
	Cmd c = {.kind = C_OSEC, .idx = osi};
	VVEC_PUSH(topcmds, c, alloc);
}

static void psections(void)
{
	sexpect('{');
	while (!satend() && !speek('}')) {
		if (sconsume(';'))
			continue;
		if (sword("ASSERT")) {
			Assign *a = passert();
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sconsume(';');
			continue;
		}
		if (sword("PROVIDE_HIDDEN") || sword("HIDDEN")) {
			Assign *a = pprovide(true);
			if (a) VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		if (sword("PROVIDE")) {
			Assign *a = pprovide(false);
			if (a) VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		const char *save = sp;
		char *w = sname();
		Assign *a = passign_rest(w);
		if (a) {
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		if (!*w) {
			sp = save;
			serr("unexpected token in SECTIONS");
			sp = sp_end;
			return;
		}
		poutsec(w);
	}
	sexpect('}');
}

static void pphdrs(void)
{
	static const struct { const char *n; uint32_t t; } pt[] = {
		{"PT_NULL", 0}, {"PT_LOAD", 1}, {"PT_DYNAMIC", 2}, {"PT_INTERP", 3}, {"PT_NOTE", 4},
		{"PT_SHLIB", 5}, {"PT_PHDR", 6}, {"PT_TLS", 7}, {"PT_GNU_EH_FRAME", 0x6474e550},
		{"PT_GNU_STACK", 0x6474e551}, {"PT_GNU_RELRO", 0x6474e552}, {"PT_GNU_PROPERTY", 0x6474e553},
	};
	have_phdrs = true;
	sexpect('{');
	while (!satend() && !speek('}')) {
		if (sconsume(';'))
			continue;
		SPhdr h = {.name = sname()};
		char *t = sname();
		bool ok = false;
		for (size_t i = 0; i < sizeof pt / sizeof pt[0] && !ok; i++)
			if (!strcmp(t, pt[i].n))
				h.type = pt[i].t, ok = true;
		if (!ok && *t >= '0' && *t <= '9')
			h.type = (uint32_t)strtoul(t, NULL, 0), ok = true;
		if (!ok) {
			serr("unknown program header type");
			sp = sp_end;
			return;
		}
		for (;;) {
			if (sword("FILEHDR")) { h.filehdr = true; continue; }
			if (sword("PHDRS")) { h.phdrs = true; continue; }
			if (sword("FLAGS")) { h.flags = pparen(); continue; }
			if (sword("AT")) { serr("PHDRS AT() not supported"); sp = sp_end; return; }
			break;
		}
		VVEC_PUSH(sphdrs, h, alloc);
		sexpect(';');
	}
	sexpect('}');
}

static void pversion(void)
{
	sexpect('{');
	while (!satend() && !speek('}')) {
		VerNode v = {.name = ""};
		if (!speek('{'))
			v.name = sname();
		sexpect('{');
		bool local = false;
		while (!satend() && !speek('}')) {
			if (sconsume(';'))
				continue;
			if (sword("global")) { sexpect(':'); local = false; continue; }
			if (sword("local")) { sexpect(':'); local = true; continue; }
			if (sword("extern")) { serr("VERSION extern not supported"); sp = sp_end; return; }
			char *n = sname();
			if (!*n) {
				serr("unexpected token in VERSION");
				sp = sp_end;
				return;
			}
			if (local)
				VVEC_PUSH(ver_locals, n, alloc);
			else
				VVEC_PUSH(v.globs, n, alloc);
			sexpect(';');
		}
		sexpect('}');
		while (!satend() && !speek(';'))	/* dependencies */
			if (!*sname()) {
				serr("unexpected token in VERSION");
				sp = sp_end;
				return;
			}
		sexpect(';');
		if (*v.name)
			nverdefs++;
		VVEC_PUSH(vernodes, v, alloc);
	}
	sexpect('}');
}

static void parse_script(const char *text, size_t len)
{
	sp = sp_file = text;
	sp_end = text + len;
	while (!satend()) {
		if (sconsume(';'))
			continue;
		if (sword("SECTIONS")) { psections(); continue; }
		if (sword("PHDRS")) { pphdrs(); continue; }
		if (sword("VERSION")) { pversion(); continue; }
		if (sword("OUTPUT_ARCH") || sword("OUTPUT_FORMAT") || sword("TARGET") || sword("SEARCH_DIR")) {
			sexpect('(');
			while (!satend() && !speek(')'))
				sp++;
			sexpect(')');
			continue;
		}
		if (sword("ENTRY")) {
			sexpect('(');
			entry_name = sname();
			sexpect(')');
			continue;
		}
		if (sword("ASSERT")) {
			Assign *a = passert();
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sconsume(';');
			continue;
		}
		if (sword("PROVIDE_HIDDEN") || sword("HIDDEN")) {
			Assign *a = pprovide(true);
			if (a) VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		if (sword("PROVIDE")) {
			Assign *a = pprovide(false);
			if (a) VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		const char *save = sp;
		char *w = sname();
		Assign *a = passign_rest(w);
		if (a) {
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		sp = save;
		serr("unsupported linker script command");
		return;
	}
}
