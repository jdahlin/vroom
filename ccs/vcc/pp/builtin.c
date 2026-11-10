/* vcc preprocessor: builtin macros and the __has_* queries */
#include "ppimpl.h"
#include <sys/stat.h>
#include "ppdata.inc"

/* ---------------- builtin macros ---------------- */

VPRIV const ctx *cur_file(const pp *p)
{
	for (int d = p->depth - 1; d >= 0; d--)
		if (p->cx[d].type == C_FILE)
			return &p->cx[d];
	return NULL;
}

static void mknum(pp *p, ptok *t, unsigned long long v)
{
	char *b = varena_alloc(&p->tmp, 40);
	int n = utoa(b, v);

	memset(b + n, 0, 16);
	t->s = b;
	t->len = (uint32_t)n;
	t->kind = T_NUMBER;
	t->id = 0;
	t->ctr = 0;
	t->flags &= ~(TF_DIRTY | TF_RAW | PF_NOEXP);
}

static void mkstr(pp *p, ptok *t, const char *s, size_t n)
{
	char *b = varena_alloc(&p->tmp, 2 * n + 24), *w = b;

	*w++ = '"';
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\\' || s[i] == '"')
			*w++ = '\\';
		*w++ = s[i];
	}
	*w++ = '"';
	memset(w, 0, 16);
	t->s = b;
	t->len = (uint32_t)(w - b);
	t->kind = T_STRING;
	t->id = 0;
	t->ctr = 0;
	t->flags &= ~(TF_DIRTY | TF_RAW | PF_NOEXP);
}

/* put t back: it is read again next */
static void unread(pp *p, const ptok *t)
{
	ptok *c = varena_alloc(&p->tmp, sizeof(*c));

	*c = *t;
	push_ptok(p, c, 1, 0, 0, 0);
}

/*
 * A header name after '(' (has_include) or in #include: "x" or <x> as
 * written, or else from macro-expanded tokens.  Returns false if malformed.
 */
VPRIV bool header_name(pp *p, char *buf, size_t cap, size_t *len, bool *angled, ptok *t)
{
	size_t n = 0;
	bool expanded = false;

	if (t->kind == T_IDENT) {
		unread(p, t);
		next_exp(p, t);
		expanded = true;
	}
	if (t->kind == T_STRING) {
		size_t l;
		const char *s = pp_spell(t, buf, &l);

		if (l < 2 || l - 2 >= cap)
			return false;
		memmove(buf, s + 1, l - 2);
		*len = l - 2;
		*angled = false;
		return true;
	}
	if (t->kind != T_LESS)
		return false;
	{
		const ptok lt = *t;
		bool contiguous = true;
		const char *prev_end = lt.s + lt.len;

		/* <...> written out is one header-name token: no expansion inside */
		for (;;) {
			if (expanded)
				next_exp(p, t);
			else
				raw(p, t);
			if (t->kind == T_EOF)
				return false;
			if (t->kind == T_GREATER)
				break;
			if (n && (t->flags & (TF_SPACE | TF_SOL)) && n < cap)
				buf[n++] = ' ';
			if (t->s < prev_end || t->s > prev_end + 256)
				contiguous = false;
			prev_end = t->s + t->len;
			if (n + t->len >= cap)
				return false;
			n = (size_t)(spell_to(buf + n, t) - buf);
		}
		/* written in one piece in a file: take the text as is */
		if (contiguous && t->s > lt.s && (size_t)(t->s - lt.s - 1) < cap) {
			n = (size_t)(t->s - lt.s - 1);
			memcpy(buf, lt.s + 1, n);
		}
	}
	*len = n;
	*angled = true;
	return true;
}

/* "(" ... ")" as one spelling, for the feature tests */
static bool feature_arg(pp *p, char *buf, size_t cap, uint32_t loc)
{
	size_t n = 0;
	int depth = 0;
	ptok t;

	raw(p, &t);
	if (t.kind != T_LPAREN) {
		error(p, loc, "missing '(' after feature-test macro");
		return false;
	}
	for (;;) {
		raw(p, &t);
		if (t.kind == T_EOF)
			return false;
		if (t.kind == T_LPAREN)
			depth++;
		if (t.kind == T_RPAREN && !depth--)
			break;
		if (n + t.len + 1 >= cap)
			return false;
		n = (size_t)(spell_to(buf + n, &t) - buf);
	}
	buf[n] = 0;
	return true;
}

static int has_query(pp *p, int b, const char *name, uint32_t loc)
{
	static const char *const q[] = {
		[B_HAS_ATTRIBUTE] = "__has_attribute", [B_HAS_BUILTIN] = "__has_builtin",
		[B_HAS_FEATURE] = "__has_feature", [B_HAS_EXTENSION] = "__has_extension",
		[B_HAS_C_ATTRIBUTE] = "__has_c_attribute", [B_HAS_CPP_ATTRIBUTE] = "__has_cpp_attribute",
		[B_HAS_DECLSPEC] = "__has_declspec_attribute", [B_HAS_WARNING] = "__has_warning",
		[B_HAS_CONSTEXPR_BUILTIN] = "__has_constexpr_builtin",
		[B_IS_TARGET_ARCH] = "__is_target_arch", [B_IS_TARGET_OS] = "__is_target_os",
		[B_IS_TARGET_VENDOR] = "__is_target_vendor",
		[B_IS_TARGET_ENVIRONMENT] = "__is_target_environment",
		[B_IS_TARGET_VARIANT_OS] = "__is_target_variant_os",
		[B_IS_TARGET_VARIANT_ENVIRONMENT] = "__is_target_variant_environment",
		[B_BUILDING_MODULE] = "__building_module",
	};

	size_t lo = 0, hi = sizeof(has_tab) / sizeof(has_tab[0]);

	if (b == B_BUILDING_MODULE)
		return 0;	/* no modules */
	/* the table holds the nonzero answers, sorted by (query, name) */
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = strcmp(has_tab[mid].q, q[b]);

		if (!c)
			c = strcmp(has_tab[mid].name, name);
		if (!c)
			return has_tab[mid].v[p->o->target];
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	/* clang: the __is_target_* macros answer 1 too (AvailabilityInternal.h asks) */
	if (b == B_HAS_BUILTIN && !strncmp(name, "__is_target_", 12))
		for (int k = B_IS_TARGET_ARCH; k <= B_IS_TARGET_VARIANT_ENVIRONMENT; k++)
			if (!strcmp(name, q[k]))
				return 1;
	/* the table is what the kernel and SDK headers asked clang; the front end knows more */
	if (b == B_HAS_BUILTIN && pp_builtin_known && pp_builtin_known(name))
		return 1;
	return 0;
}

int (*pp_builtin_known)(const char *name);

/* -Wc2y-extensions (-pedantic): __COUNTER__; out of line, builtin() is hot */
static __attribute__((noinline, cold)) void counter_ext(pp *p, uint32_t loc)
{
	static const char *const g[] = { "pedantic", NULL };
	pp_dinfo d = { "warning", loc, NULL, 0, NULL, 0, "__COUNTER__", 11 };

	if (pp_warnx(p, "c2y-extensions", false, g, &d, "'__COUNTER__' is a C2y extension"))
		HC_BAD(p);
}

VPRIV int builtin(pp *p, const pmacro *m, ptok *t)
{
	uint8_t nf = t->flags & (TF_SOL | TF_SPACE);
	const ctx *fc = cur_file(p);
	char buf[4096];

	switch (m->builtin) {
	case B_LINE:
		mknum(p, t, loc_line(p, t->loc));
		break;
	case B_FILE: {
		uint32_t l, loc = t->loc;
		const char *s;

		if (loc < p->fe[fc->fe].base || loc > p->fe[fc->fe].base + p->fe[fc->fe].f->size)
			loc = p->fe[fc->fe].base + fc->vt[fc->i ? fc->i - 1 : 0].off;
		s = presumed_name(p, fc->fe, loc, &l);
		mkstr(p, t, s, l);
		break;
	}
	case B_FILE_NAME: {
		const fent *e = &p->fe[fc->fe];
		const char *s = e->name + e->namelen;

		while (s > e->name && s[-1] != '/')
			s--;
		mkstr(p, t, s, e->namelen - (size_t)(s - e->name));
		break;
	}
	case B_BASE_FILE:
		HC_BAD(p);
		mkstr(p, t, p->base_name, p->base_namelen);
		break;
	case B_COUNTER:
		if (unlikely(p->o->nwargs) && pp_wc_maybe(p, PPW_C2Y))
			counter_ext(p, t->loc);	/* (the kernel's __UNIQUE_ID: keep the common case one test) */
		if (p->rec.on) {
			if (p->in_dir)
				HC_BAD(p);	/* the value decides something */
			mknum(p, t, p->counter);
			t->ctr = new_cprov(p, p->counter, 0, t->len, 0);
			p->counter++;
		} else {
			mknum(p, t, p->counter++);
		}
		break;
	case B_INCLUDE_LEVEL:
		for (int i = 0; i < p->rec.n; i++)
			p->rec.lev[i].uses_level = 1;
		mknum(p, t, p->nfile > 1 ? (unsigned)p->nfile - 1 : 0);
		break;
	case B_DATE: case B_TIME: case B_TIMESTAMP: {
		time_t now = p->o->has_epoch ? (time_t)p->o->epoch : time(NULL);

		HC_BAD(p);
		struct tm tm;
		struct stat st;

		/* clang: __TIMESTAMP__ is the current file's mtime */
		if (m->builtin == B_TIMESTAMP && !p->o->has_epoch && fc) {
			if (stat(p->fe[fc->fe].f->path, &st) != 0) {
				mkstr(p, t, "??? ??? ?? ??:??:?? ????", 24);
				break;
			}
			now = st.st_mtime;
		}

		if (p->o->has_epoch)	/* reproducible builds: UTC, as clang */
			gmtime_r(&now, &tm);
		else
			localtime_r(&now, &tm);
		strftime(buf, sizeof(buf), m->builtin == B_DATE ? "%b %e %Y" :
			 m->builtin == B_TIME ? "%H:%M:%S" : "%a %b %e %H:%M:%S %Y", &tm);
		mkstr(p, t, buf, strlen(buf));
		break;
	}
	case B_PRAGMA: {
		ptok s, r;
		size_t l;
		const char *sp;
		char *d;
		size_t dn = 0;

		if (p->preexp)
			return 1;	/* runs when the result is rescanned */
		next_exp(p, &r);
		next_exp(p, &s);
		if (r.kind != T_LPAREN || !is_strlit(s.kind)) {
			error(p, t->loc, "_Pragma takes a parenthesized string literal");
			return 1;
		}
		if (s.ctr)
			HC_BAD(p);
		next_exp(p, &r);
		if (r.kind != T_RPAREN)
			error(p, t->loc, "_Pragma takes a parenthesized string literal");
		{
			char tmp[s.len + 1];

			sp = pp_spell(&s, tmp, &l);
			while (l && *sp != '"') {	/* encoding prefix */
				sp++;
				l--;
			}
			d = varena_alloc(&p->tmp, l + 16);
			memcpy(d, "pragma ", 7);
			dn = 7;
			for (size_t i = 1; i + 1 < l; i++) {
				if (sp[i] == '\\' && (sp[i + 1] == '\\' || sp[i + 1] == '"'))
					i++;
				d[dn++] = sp[i];
			}
		}
		if (pragma_text(p, d, dn, t->loc, t)) {
			t->flags = TF_SOL;
			return 1;
		}
		return 0;
	}
	case B_HAS_INCLUDE: case B_HAS_INCLUDE_NEXT: {
		ptok r;
		size_t n;
		bool angled;
		int32_t sd;
		const char *path;

		raw(p, &r);
		if (r.kind != T_LPAREN) {
			error(p, t->loc, "missing '(' after __has_include");
			return 1;
		}
		raw(p, &r);
		if (!header_name(p, buf, sizeof(buf), &n, &angled, &r)) {
			error(p, t->loc, "invalid __has_include argument");
			return 1;
		}
		raw(p, &r);
		if (r.kind != T_RPAREN)
			error(p, t->loc, "missing ')' after __has_include");
		mknum(p, t, find_include(p, fc, buf, n, angled, m->builtin == B_HAS_INCLUDE_NEXT, &sd, &path, NULL) != NULL);
		break;
	}
	case B_IS_IDENTIFIER:
		if (!feature_arg(p, buf, sizeof(buf), t->loc))
			return 1;
		mknum(p, t, 1);
		break;
	default:
		if (!feature_arg(p, buf, sizeof(buf), t->loc)) {
			error(p, t->loc, "malformed feature test");
			return 1;
		}
		mknum(p, t, (unsigned long long)has_query(p, m->builtin, buf, t->loc));
		break;
	}
	t->flags = (t->flags & ~(TF_SOL | TF_SPACE)) | nf;
	return 1;
}
