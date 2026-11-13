/* The linker script's expression parser (precedence climbing) and its primaries. */

static uint32_t get_osec(const char *name);
static Expr *pexpr(void);

static Expr *mk(uint8_t op)
{
	Expr *e = alloc(sizeof *e);
	e->op = op;
	return e;
}

static Provide *cur_provide;

static void note_ref(uint32_t g)
{
	if (cur_provide)
		VVEC_PUSH(cur_provide->refs, g, alloc);
	else
		VVEC_PUSH(refsyms, g, alloc);
}

static int prec(const char *op)
{
	char c = op[0], c1 = c ? op[1] : 0;
	if (c1 && op[2])
		return -1;
	switch (c) {
	case '*': case '/': case '%': return c1 ? -1 : 11;
	case '+': case '-': return c1 ? -1 : 10;
	case '<': case '>': return c1 == c ? 9 : !c1 || c1 == '=' ? 8 : -1;
	case '=': case '!': return c1 == '=' ? 7 : -1;
	case '&': return !c1 ? 6 : c1 == '&' ? 3 : -1;
	case '^': return c1 ? -1 : 5;
	case '|': return !c1 ? 4 : c1 == '|' ? 2 : -1;
	case '?': return c1 ? -1 : 1;
	default: return -1;
	}
}
/* peek an operator token (longest match of << >> <= >= == != && || op= and
 * the single-character operators); returns length or 0 */
static int peekop(char out[4])
{
	sskip();
	size_t left = (size_t)(sp_end - sp);
	if (!left)
		return 0;
	char c = sp[0], c1 = left > 1 ? sp[1] : 0, c2 = left > 2 ? sp[2] : 0;
	int n;
	switch (c) {
	case '<': case '>': n = c1 == c ? (c2 == '=' ? 3 : 2) : c1 == '=' ? 2 : 1; break;
	case '=': n = c1 == '=' ? 2 : 1; break;
	case '!': n = c1 == '=' ? 2 : 0; break;
	case '&': case '|': n = c1 == c || c1 == '=' ? 2 : 1; break;
	case '+': case '-': case '*': case '/': case '^': n = c1 == '=' ? 2 : 1; break;
	case '%': case '?': case ':': n = 1; break;
	default: n = 0;
	}
	memcpy(out, sp, (size_t)n);
	out[n] = 0;
	return n;
}

static Expr *pprimary(void);
static Expr *pexpr1(Expr *lhs, int minp)
{
	char op1[4], op2[4];
	for (;;) {
		int n = peekop(op1);
		if (!n || prec(op1) < minp || (strchr(op1, '=') && strcmp(op1, "==") && strcmp(op1, "!=") &&
		    strcmp(op1, "<=") && strcmp(op1, ">=")))
			break;
		if (!strcmp(op1, ":"))
			break;
		sp += n;
		if (!strcmp(op1, "?")) {
			Expr *t = mk(E_TERN);
			t->a = lhs;
			t->b = pexpr();
			sexpect(':');
			t->c = pexpr();
			return t;
		}
		Expr *rhs = pprimary();
		for (;;) {
			int m = peekop(op2);
			if (!m || prec(op2) <= prec(op1) || !strcmp(op2, ":"))
				break;
			if (strchr(op2, '=') && strcmp(op2, "==") && strcmp(op2, "!=") &&
			    strcmp(op2, "<=") && strcmp(op2, ">="))
				break;
			rhs = pexpr1(rhs, prec(op2));
		}
		Expr *b = mk(E_BIN);
		strcpy(b->bop, op1);
		b->a = lhs;
		b->b = rhs;
		lhs = b;
	}
	return lhs;
}
static Expr *pexpr(void) { return pexpr1(pprimary(), 0); }

static Expr *pparen(void)
{
	sexpect('(');
	Expr *e = pexpr();
	sexpect(')');
	return e;
}
static uint32_t pparen_osec(void)
{
	sexpect('(');
	char *n = spatname();
	sexpect(')');
	return get_osec(n);
}

static Expr *pprimary(void)
{
	sskip();
	if (speek('('))
		return pparen();
	if (sconsume('~')) { Expr *e = mk(E_NOT); e->a = pprimary(); return e; }
	if (sconsume('!')) { Expr *e = mk(E_LNOT); e->a = pprimary(); return e; }
	if (sconsume('-')) { Expr *e = mk(E_NEG); e->a = pprimary(); return e; }
	if (sconsume('+')) return pprimary();
	sskip();
	const char *b = sp;
	if (sp < sp_end && *sp == '"') {
		char *n = sname();
		uint32_t g = gintern(n);
		note_ref(g);
		Expr *e = mk(E_SYM);
		e->sym = g;
		return e;
	}
	while (sp < sp_end && (ctab[(unsigned char)*sp] & CT_IDENT))
		sp++;
	if (sp == b) {
		serr("expected expression");
		sp = sp_end;
		return mk(E_NUM);
	}
	char *tok = sdup(b, sp);
	if (!strcmp(tok, ".")) return mk(E_DOT);
	if (isdigit((unsigned char)tok[0])) {
		Expr *e = mk(E_NUM);
		char *end;
		uint64_t v;
		size_t n = strlen(tok);
		if (n > 1 && (tok[n - 1] == 'h' || tok[n - 1] == 'H') && !(tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))) {
			tok[n - 1] = 0;
			v = strtoull(tok, &end, 16);
		} else {
			uint64_t mul = 1;
			if (tok[n - 1] == 'K' || tok[n - 1] == 'k') { mul = 1024; tok[n - 1] = 0; }
			else if (tok[n - 1] == 'M' || tok[n - 1] == 'm') { mul = 1024 * 1024; tok[n - 1] = 0; }
			v = strtoull(tok, &end, 0) * mul;
		}
		if (*end)
			serr("malformed number");
		e->num = v;
		return e;
	}
	Expr *e;
	if (!strcmp(tok, "ABSOLUTE")) { e = mk(E_ABSOLUTE); e->a = pparen(); return e; }
	if (!strcmp(tok, "ADDR")) {
		e = mk(E_ADDR);
		e->sym = pparen_osec();
		osecs.v[e->sym].used_in_expr = true;
		return e;
	}
	if (!strcmp(tok, "LOADADDR")) {
		e = mk(E_LOADADDR);
		e->sym = pparen_osec();
		osecs.v[e->sym].used_in_expr = true;
		return e;
	}
	if (!strcmp(tok, "SIZEOF")) { e = mk(E_SIZEOF); e->sym = pparen_osec(); return e; }
	if (!strcmp(tok, "ALIGNOF")) { e = mk(E_ALIGNOF); e->sym = pparen_osec(); return e; }
	if (!strcmp(tok, "SIZEOF_HEADERS")) return mk(E_SIZEOF_HEADERS);
	if (!strcmp(tok, "ALIGN")) {
		sexpect('(');
		Expr *a = pexpr();
		if (sconsume(')')) { e = mk(E_ALIGN1); e->a = a; return e; }
		sexpect(',');
		e = mk(E_ALIGN2);
		e->a = a;
		e->b = pexpr();
		sexpect(')');
		return e;
	}
	if (!strcmp(tok, "ASSERT")) {
		sexpect('(');
		e = mk(E_ASSERT);
		e->a = pexpr();
		sexpect(',');
		e->msg = sname();
		sexpect(')');
		return e;
	}
	if (!strcmp(tok, "DEFINED")) {
		sexpect('(');
		e = mk(E_DEFINED);
		e->sym = gintern(spatname());
		sexpect(')');
		return e;
	}
	if (!strcmp(tok, "MAX") || !strcmp(tok, "MIN")) {
		e = mk(tok[1] == 'A' ? E_MAX : E_MIN);
		sexpect('(');
		e->a = pexpr();
		sexpect(',');
		e->b = pexpr();
		sexpect(')');
		return e;
	}
	if (!strcmp(tok, "LOG2CEIL")) { e = mk(E_LOG2CEIL); e->a = pparen(); return e; }
	if (!strcmp(tok, "CONSTANT")) {
		sexpect('(');
		char *n = spatname();
		sexpect(')');
		e = mk(E_NUM);
		e->num = !strcmp(n, "MAXPAGESIZE") ? opt.max_page : 4096;
		return e;
	}
	uint32_t g = gintern(tok);
	note_ref(g);
	e = mk(E_SYM);
	e->sym = g;
	return e;
}
