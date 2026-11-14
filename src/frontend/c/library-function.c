/* vcc front end: clang's library builtins and standard functions, and implicit declarations of them */
#include "cc-internal.h"

/* ---------------- library builtins (frontend/c/libbuiltins.inc, from clang) ---------------- */

static const struct cc_library_builtin_entry { const char *name, *lib, *sig; } cc_library_builtins[] = {
#include "libbuiltins.inc"
};

/* one type of a clang builtin type string at *p (Builtins.def's encoding); false: not one vcc reads */
static bool cc_library_signature_type(cc *c, const char **p, qty *out)
{
	const char *s = *p;
	int lng = 0, sgn = 0, uns = 0, cplx = 0;
	qty t;

	for (;; s++) {
		if (*s == 'L') lng++;
		else if (*s == 'S') sgn = 1;
		else if (*s == 'U') uns = 1;
		else if (*s == 'I') ;	/* a required constant */
		else if (*s == 'X') cplx = 1;
		else break;
	}
	switch (*s++) {
	case 'v': t = cc_type_basic(TY_VOID); break;
	case 'b': t = cc_type_basic(TY_BOOL); break;
	case 'c': t = cc_type_basic(uns ? TY_UCHAR : sgn ? TY_SCHAR : TY_CHAR); break;
	case 's': t = cc_type_basic(uns ? TY_USHORT : TY_SHORT); break;
	case 'i':
		t = cc_type_basic(lng == 0 ? (uns ? TY_UINT : TY_INT) : lng == 1 ? (uns ? TY_ULONG : TY_LONG)
			 : lng == 2 ? (uns ? TY_ULLONG : TY_LLONG) : (uns ? TY_UINT128 : TY_INT128));
		lng = 0;
		break;
	case 'f': t = cc_type_basic(TY_FLOAT); break;
	case 'd':
		if (lng > 1)
			return false;	/* __float128 */
		t = cc_type_basic(lng ? TY_LDOUBLE : TY_DOUBLE);
		lng = 0;
		break;
	case 'z': t = cc_type_basic(TY_ULONG); break;	/* size_t */
	case 'Y': t = cc_type_basic(TY_LONG); break;	/* ptrdiff_t */
	case 'w': t = c->t_wchar; break;
	case 'P': t = cc_type_basic(TY_VOID); break;	/* FILE: only ever as P*, a void * */
	case 'W': t = cc_type_basic(CC_I64(c, false)); break;	/* int64_t */
	case 'Z': t = cc_type_basic(TY_INT); break;	/* int32_t */
	default: return false;
	}
	if (lng)
		return false;
	if (cplx)
		t = cc_type_complex(c, t);
	for (;;) {
		if (*s == '*') {
			t = cc_type_pointer(c, t);
			s++;
			while (*s >= '0' && *s <= '9')
				s++;	/* address space */
		} else if (*s == 'C' || *s == 'D' || *s == 'R') {
			t = QT(QI(t), QQ(t) | (*s == 'C' ? Q_CONST : *s == 'D' ? Q_VOLATILE : Q_RESTRICT));
			s++;
		} else if (*s == '&') {
			return false;
		} else {
			break;
		}
	}
	*p = s;
	*out = t;
	return true;
}

/* a function type from a clang builtin type string (its result in *ret); false: not readable */
static bool cc_library_signature_function_type(cc *c, const char *p, qty *fty, qty *ret)
{
	qty par[16];
	uint32_t np = 0;
	int fl = 0;

	if (!cc_library_signature_type(c, &p, ret))
		return false;
	while (*p) {
		if (*p == '.') {
			fl |= TF_VARIADIC;
			break;
		}
		if (np == 16 || !cc_library_signature_type(c, &p, &par[np]))
			return false;
		np++;
	}
	*fty = cc_type_function(c, *ret, par, np, fl);
	return true;
}

/*
 * __builtin_X for a library function X that vcc has no code for: declared on
 * first use with clang's prototype, its symbol X (clang calls X unless it
 * folds or inlines it).  0: not a library builtin, or a type vcc cannot read.
 */
static const struct cc_library_builtin_entry *cc_library_builtin_find(const char *n, size_t nl)
{
	size_t lo = 0, hi = sizeof(cc_library_builtins) / sizeof(cc_library_builtins[0]);

	if (nl < 11 || memcmp(n, "__builtin_", 10))
		return NULL;
	while (lo < hi) {
		size_t m = (lo + hi) / 2;
		int r = strncmp(cc_library_builtins[m].name, n, nl);

		if (r == 0 && cc_library_builtins[m].name[nl])
			r = 1;
		if (r == 0)
			return &cc_library_builtins[m];
		if (r < 0)
			lo = m + 1;
		else
			hi = m;
	}
	return NULL;
}

/* The generated table gives disable flags stable indices within this binary.
 * Unknown names have no builtin semantics to disable. */
_Static_assert(sizeof(cc_library_builtins) / sizeof(cc_library_builtins[0]) <= CC_DISABLED_BUILTIN_WORDS * 64,
               "increase the disabled-builtin bitset for libbuiltins.inc");

static const struct cc_library_builtin_entry *cc_library_disabled_find(const char *name, size_t len)
{
	char full[128];
	if (len + 11 > sizeof full)
		return NULL;
	memcpy(full, "__builtin_", 10);
	memcpy(full + 10, name, len);
	full[len + 10] = 0;
	return cc_library_builtin_find(full, len + 10);
}

void cc_disable_builtin(uint64_t *bits, const char *name)
{
	const struct cc_library_builtin_entry *b = cc_library_disabled_find(name, strlen(name));
	if (b) {
		size_t i = (size_t)(b - cc_library_builtins);
		bits[i / 64] |= UINT64_C(1) << (i % 64);
	}
}

CC_PRIVATE bool cc_library_builtin_is_disabled(const cc_opts *o, const char *name, size_t len)
{
	uint64_t any = 0;
	for (unsigned i = 0; i < CC_DISABLED_BUILTIN_WORDS; i++)
		any |= o->disabled_builtins[i];
	if (!any)
		return false;
	const struct cc_library_builtin_entry *b = cc_library_disabled_find(name, len);
	if (!b)
		return false;
	size_t i = (size_t)(b - cc_library_builtins);
	return (o->disabled_builtins[i / 64] >> (i % 64)) & 1;
}

/* __has_builtin: a library builtin vcc declares (the table holds only the readable ones) */
CC_PRIVATE bool cc_library_builtin_is_known(const char *n)
{
	return cc_library_builtin_find(n, strlen(n)) != NULL;
}

CC_PRIVATE const char *cc_library_builtin_signature(const char *n)
{
	const struct cc_library_builtin_entry *b = cc_library_builtin_find(n, strlen(n));

	return b ? b->sig : NULL;
}

/* a library builtin by name (__builtin_expl): its function type and the library function it calls (expl);
   false: not one, or a signature not readable (C++ calls it so: cxx_gen_builtin) */
CC_PRIVATE bool cc_library_builtin_info(cc *c, const char *name, size_t len, qty *fty, const char **lib)
{
	const struct cc_library_builtin_entry *b = cc_library_builtin_find(name, len);
	qty ret;

	if (!b || !b->lib || !cc_library_signature_function_type(c, b->sig, fty, &ret))
		return false;
	*lib = b->lib;
	return true;
}

CC_PRIVATE uint32_t cc_library_builtin_declare(cc *c, uint32_t id, uint32_t loc)
{
	const struct cc_library_builtin_entry *b = cc_library_builtin_find(cc_identifier_string(id), cc_identifier_length(id));

	if (!b)
		return 0;
	{
		qty fty, ret;

		if (!cc_library_signature_function_type(c, b->sig, &fty, &ret))
			return 0;
		{
			cc_declaration_specifiers ds = { .ty = ret, .sc = SC_EXTERN, .loc = loc };
			cc_declarator d = { .name = id, .loc = loc };
			uint32_t s = cc_declaration_bind(c, &ds, &d, fty, true);
			/* Integer-power builtins name compiler-runtime helpers, not C
			 * library functions named powi (which may be the caller itself). */
			const char *lib = b->lib;
			if (!strcmp(b->name, "__builtin_powi")) lib = "__powidf2";
			else if (!strcmp(b->name, "__builtin_powif")) lib = "__powisf2";
			else if (!strcmp(b->name, "__builtin_powil"))
				lib = c->o->target == PP_LINUX_X86_64 ? "__powixf2" :
				      cc_type_size(c, ret) == 8 ? "__powidf2" : "__powitf2";
			size_t ll = strlen(lib), pre = c->o->target == PP_DARWIN ? 1 : 0;
			char *keep = cc_heap_allocate(c, ll + pre + 1);
			attrs a;

			keep[0] = '_';
			memcpy(keep + pre, lib, ll + 1);
			memset(&a, 0, sizeof(a));
			a.asmname = c->nstr + 1;
			c->strs[c->nstr++] = (cstr){ keep, (uint32_t)(ll + pre + 1), 1, LEX_TOKEN_STRING };
			cc_attribute_apply_symbol(c, &c->syms[s], &a);
			return s;
		}
	}
}

/* the type clang gives an implicitly declared library function (asin: double
   (double)), 0: not one (or -fno-builtin) */
CC_PRIVATE qty cc_library_implicit_type(cc *c, uint32_t id)
{
	char nm[128];
	const char *p;
	qty ret, par[16];
	uint32_t np = 0;
	int fl = 0, enc;

	if (c->o->no_builtin || cc_library_builtin_is_disabled(c->o, cc_identifier_string(id), cc_identifier_length(id)) ||
	    (size_t)cc_identifier_length(id) >= sizeof nm)
		return 0;
	memcpy(nm, cc_identifier_string(id), cc_identifier_length(id));
	nm[cc_identifier_length(id)] = 0;
	if (!(p = cc_lib_sig(nm, &enc)))
		return 0;
	if (!enc)
		return cc_builtin_signature_function_type(c, p);
	if (!cc_library_signature_type(c, &p, &ret))
		return 0;
	while (*p) {
		if (*p == '.') {
			fl |= TF_VARIADIC;
			break;
		}
		if (np == 16 || !cc_library_signature_type(c, &p, &par[np]))
			return 0;
		np++;
	}
	return cc_type_function(c, ret, par, np, fl);
}

CC_PRIVATE uint32_t cc_library_implicit_declare(cc *c, uint32_t id, uint32_t loc, qty t)
{
	cc_declaration_specifiers ds = { .ty = AST_TYPE(c, t)->base, .sc = SC_EXTERN, .loc = loc };
	cc_declarator d = { .name = id, .loc = loc };

	return cc_declaration_bind(c, &ds, &d, t, c->depth == 0);
}

/* C89: a call to an undeclared id declares "extern int id();" in this scope */

CC_PRIVATE uint32_t cc_implicit_function_declare(cc *c, uint32_t id, uint32_t loc)
{
	cc_declaration_specifiers ds = { .ty = cc_type_basic(TY_INT), .sc = SC_EXTERN, .loc = loc };
	cc_declarator d = { .name = id, .loc = loc };

	return cc_declaration_bind(c, &ds, &d, cc_type_function(c, cc_type_basic(TY_INT), NULL, 0, TF_NOPROTO), c->depth == 0);
}

/* ---------------- library functions and names with a header (D5.4) ---------------- */

/* clang's library functions with their header (frontend/c/libfuncs.inc, gen-libbuiltins.py) */
static const struct cc_library_function_entry { const char *name, *hdr, *sig; uint8_t gnu; } cc_library_functions[] = {
#include "libfuncs.inc"
};
/* other names standard headers declare (frontend/c/libnames.inc, by hand) */
static const struct cc_library_name_entry { const char *name, *hdr; } cc_library_names[] = {
#include "libnames.inc"
};

#define CC_LIBRARY_BSEARCH(tab, n, nl, out) do {						\
	size_t lo_ = 0, hi_ = sizeof(tab) / sizeof((tab)[0]);			\
	(out) = NULL;								\
	while (lo_ < hi_) {							\
		size_t m_ = (lo_ + hi_) / 2;					\
		int r_ = strncmp((tab)[m_].name, (n), (nl));			\
		if (r_ == 0 && (tab)[m_].name[nl])				\
			r_ = 1;							\
		if (r_ == 0) { (out) = &(tab)[m_]; break; }			\
		if (r_ < 0) lo_ = m_ + 1; else hi_ = m_;			\
	}									\
} while (0)

static const struct cc_library_function_entry *cc_library_function_find(cc *c, uint32_t id)
{
	const struct cc_library_function_entry *f;

	if (c->o->no_builtin || cc_library_builtin_is_disabled(c->o, cc_identifier_string(id), cc_identifier_length(id)))
		return NULL;	/* disabled builtins have no implicit library declaration */
	CC_LIBRARY_BSEARCH(cc_library_functions, cc_identifier_string(id), (size_t)cc_identifier_length(id), f);
	return f && f->gnu && c->o->strict_std ? NULL : f;
}

/* the standard header that declares id, or NULL */
CC_PRIVATE const char *cc_library_header(cc *c, uint32_t id)
{
	const struct cc_library_function_entry *f = cc_library_function_find(c, id);
	const struct cc_library_name_entry *n;

	if (f)
		return f->hdr;
	CC_LIBRARY_BSEARCH(cc_library_names, cc_identifier_string(id), (size_t)cc_identifier_length(id), n);
	return n ? n->hdr : NULL;
}

/* "#include <hdr>" as a fix-it on d, where the file's includes are (or at its top) */
CC_PRIVATE void cc_library_include_fixit(cc *c, diag_record *d, uint32_t loc, const char *hdr)
{
	uint32_t at = pp_include_point_find(c->p, loc);
	char b[96];

	if (at == UINT32_MAX)
		return;
	snprintf(b, sizeof b, "#include <%s>\n", hdr);
	diag_record_fixit_add(c->p, d, at, at, b, DIAG_APPLICABILITY_MAYBE);
}

/*
 * A call to an undeclared library function (clang's Builtins with a header):
 * declared with the library's prototype, as clang, after clang's diagnostic
 * ("call to undeclared library function 'memcpy' with type ...", in C89 a
 * warning) and the note naming the header.  0: not one (or a prototype vcc
 * cannot read): the caller declares "int id()".
 */
CC_PRIVATE uint32_t cc_library_function_declare(cc *c, uint32_t id, uint32_t loc)
{
	static const char *const none[] = { NULL };
	const struct cc_library_function_entry *f = cc_library_function_find(c, id);
	qty fty, ret;
	int lv;

	if (!f || strchr(f->sig, 'P') || !cc_library_signature_function_type(c, f->sig, &fty, &ret) || !strcmp(f->name, "alloca"))
		return 0;	/* FILE * parameters: vcc has no FILE; alloca is the builtin */
	/* C89, or -Wno-error=implicit-function-declaration: clang's warning; else an error */
	lv = c->o->std == 89 || c->o->implicit_fn ? diag_warning_level(c->p, "implicit-function-declaration", true, none, loc)
		: DIAG_LEVEL_ERROR;
	if (lv >= 0) {
		const char *t = tyname(c, fty);
		diag_record *d;

		diag_report_site_pc = __builtin_return_address(0);
		if (lv == DIAG_LEVEL_ERROR)
			cc_report_error_limit(c), c->errors++;
		c->ndiag++;
		if (c->o->std == 89)
			d = diag_record_begin(c->p, lv, loc, "implicitly declaring library function '%.*s' with type '%s'", CC_IDENTIFIER(id), t);
		else
			d = diag_record_begin(c->p, lv, loc, "call to undeclared library function '%.*s' with type '%s'; ISO C99 and "
					  "later do not support implicit function declarations", CC_IDENTIFIER(id), t);
		diag_record_flag_set(d, "implicit-function-declaration");
		d->werror = lv == DIAG_LEVEL_ERROR && (c->o->std == 89 || c->o->implicit_fn);
		cc_library_include_fixit(c, d, loc, f->hdr);
		diag_record_emit(c->p, d);
		cc_report_note(c, loc, "include the header <%s> or explicitly provide a declaration for '%.*s'", f->hdr, CC_IDENTIFIER(id));
	}
	{
		cc_declaration_specifiers ds = { .ty = ret, .sc = SC_EXTERN, .loc = loc };
		cc_declarator d = { .name = id, .loc = loc };

		return cc_declaration_bind(c, &ds, &d, fty, c->depth == 0);
	}
}

/* the declarations between a K&R definition's identifier list and its body */
