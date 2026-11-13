/*
 * types.c: the C++ parser's types and records, built in the shared AST
 * (frontend/ast.h) with the C front end's code: type interning, record
 * layout, symbols (PLAN-cxx.md X1).
 *
 * References are TY_PTR types with ext 1 (T&) or 2 (T&&): distinct when
 * interned, passed as pointers (their ABI).  wchar_t, char8_t, char16_t and
 * char32_t get the C kinds of the same size and signedness for now (they
 * are distinct types in C++: overloading and mangling need that later).
 *
 * A class becomes a C record and is laid out by cc_layout_record when C's
 * rules are C++'s for it: no base classes, no virtual functions, every data
 * member's type known.  Otherwise it stays incomplete and the reason is
 * counted (VCC_CXX_PROGRESS).  An empty class has size 1, as in C++.
 */
#include <stdio.h>
#include "cxx-internal.h"

VPRIV qty cxx_spec_type(cxx *c, const cxx_spec *s)
{
	int k;

	if (s->v_unknown)
		return CXX_TY_UNKNOWN;
	if (s->v_named)
		return s->named == CXX_TY_UNKNOWN ? CXX_TY_UNKNOWN : cc_type_qualify(c->ast, s->named, s->quals);
	if (s->v_void) k = TY_VOID;
	else if (s->v_bool) k = TY_BOOL;
	else if (s->v_char) k = s->v_unsigned ? TY_UCHAR : s->v_signed ? TY_SCHAR : TY_CHAR;
	else if (s->v_char8) k = TY_UCHAR;
	else if (s->v_char16) k = TY_USHORT;
	else if (s->v_char32) k = TY_UINT;
	else if (s->v_wchar) k = TY_INT;
	else if (s->v_float) k = TY_FLOAT;
	else if (s->v_double) k = s->v_long ? TY_LDOUBLE : TY_DOUBLE;
	else if (s->v_float128) k = TY_FLOAT128;
	else if (s->v_float16) k = TY_FLOAT16;
	else if (s->v_bf16) k = TY_BFLOAT16;
	else if (s->v_int128) k = s->v_unsigned ? TY_UINT128 : TY_INT128;
	else if (s->v_short) k = s->v_unsigned ? TY_USHORT : TY_SHORT;
	else if (s->v_long >= 2) k = s->v_unsigned ? TY_ULLONG : TY_LLONG;
	else if (s->v_long) k = s->v_unsigned ? TY_ULONG : TY_LONG;
	else if (s->v_int || s->v_signed || s->v_unsigned) k = s->v_unsigned ? TY_UINT : TY_INT;
	else
		return CXX_TY_UNKNOWN;	/* (no type specifier: a constructor, auto) */
	return cc_type_qualify(c->ast, cc_type_basic(k), s->quals);
}

VPRIV qty cxx_type_pointer(cxx *c, qty base, int quals)
{
	if (base == CXX_TY_UNKNOWN || c->tentative)
		return CXX_TY_UNKNOWN;
	return cc_type_qualify(c->ast, cc_type_pointer(c->ast, base), quals);
}

VPRIV qty cxx_type_reference(cxx *c, qty base, bool rvalue)
{
	if (base == CXX_TY_UNKNOWN || c->tentative)
		return CXX_TY_UNKNOWN;
	{
		ctype t = { .kind = TY_PTR, .size = CC_PTRSIZE(c->ast), .align = CC_PTRSIZE(c->ast), .base = base,
			    .ext = (uint16_t)(rvalue ? 2 : 1) };

		return QT(cc_type_intern(c->ast, &t), 0);
	}
}

VPRIV qty cxx_type_array(cxx *c, qty elem, int64_t n)
{
	if (elem == CXX_TY_UNKNOWN || c->tentative)
		return CXX_TY_UNKNOWN;
	if (n < 0)
		return cc_type_array(c->ast, elem, 0, TF_INCOMPLETE);
	return cc_type_array(c->ast, elem, (uint64_t)n, 0);
}

VPRIV qty cxx_type_function(cxx *c, qty ret, const qty *params, uint32_t n, bool variadic)
{
	if (ret == CXX_TY_UNKNOWN || c->tentative)
		return CXX_TY_UNKNOWN;
	for (uint32_t i = 0; i < n; i++)
		if (params[i] == CXX_TY_UNKNOWN)
			return CXX_TY_UNKNOWN;
	return cc_type_function(c->ast, ret, params, n, variadic ? TF_VARIADIC : 0);
}

/* a parameter's type: arrays and functions become pointers, top-level cv goes */
VPRIV qty cxx_type_parameter_adjust(cxx *c, qty t)
{
	if (t == CXX_TY_UNKNOWN)
		return t;
	if (AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
		return cc_type_pointer(c->ast, AST_TYPE(c->ast, t)->base);
	if (AST_TYPE_KIND(c->ast, t) == TY_FUNC)
		return cc_type_pointer(c->ast, t);
	return QUNQ(t);
}

/* ---- classes ----------------------------------------------------------------------------- */

typedef struct cxx_class_build {
	struct cxx_class_build *outer;
	uint32_t tag;			/* AST tag index (0: none: the class is not built) */
	field *f;
	uint32_t nf, cf;
	int skip;			/* -1: lay it out; else CXX_SKIP_* */
	uint32_t path_len;		/* the name path before this class (layout dump) */
	uint8_t path_valid;
	/* the Itanium ABI's inputs: base subobjects, and whether the class is POD for layout (a non-POD
	   base's tail padding is reused by the derived class's members) */
	uint32_t bases[16], nbases;
	uint8_t pod, access_private, public_data, private_data;
} cxx_class_build;

/* per AST tag: what a derived class's layout needs of this class */
typedef struct cxx_class_info {
	uint64_t dsize;			/* data size: bytes up to the end of its last data, before tail padding */
	uint8_t valid, pod, empty, has_empty_base;
} cxx_class_info;

static cxx_class_info *cxx_info_of(cxx *c, uint32_t tag)
{
	static __thread cxx_class_info none;

	if (tag >= c->nclass_info) {
		uint32_t nn = tag + 1024;

		c->class_info = realloc(c->class_info, nn * sizeof(cxx_class_info));
		memset((cxx_class_info *)c->class_info + c->nclass_info, 0, (nn - c->nclass_info) * sizeof(cxx_class_info));
		c->nclass_info = nn;
	}
	memset(&none, 0, sizeof none);
	return c->class_info ? (cxx_class_info *)c->class_info + tag : &none;
}

static uint32_t cxx_record_tag(cxx *c, qty t)
{
	while (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
		t = AST_TYPE(c->ast, t)->base;
	return t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t) ? AST_TYPE(c->ast, t)->aux : 0;
}

VPRIV void cxx_class_begin(cxx *c, cxx_entity *e, uint32_t name, uint32_t loc, bool is_union, int skip, bool is_class)
{
	cxx_class_build *b = calloc(1, sizeof *b);

	b->outer = c->build;
	b->skip = skip;
	b->pod = 1;
	b->access_private = is_class;
	c->build = b;
	if (c->tentative)
		return;
	c->stats.classes++;
	if (skip >= 0)
		return;
	if (e && e->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, e->ty) &&
	    !(c->ast->tags[AST_TYPE(c->ast, e->ty)->aux].flags & RF_COMPLETE)) {
		b->tag = AST_TYPE(c->ast, e->ty)->aux;	/* declared before: its record, completed now */
		if (is_union && !(c->ast->tags[b->tag].flags & RF_UNION)) {	/* (an instance is declared as a struct) */
			c->ast->tags[b->tag].flags |= RF_UNION;
			c->ast->ty[c->ast->tags[b->tag].ty].kind = TY_UNION;
		}
		if (c->tag_instance && b->tag < c->ntag_instance)
			c->tag_instance[b->tag] = NULL;	/* (being completed: not again) */
		return;
	}
	b->tag = cc_tag_new(c->ast, is_union ? TY_UNION : TY_STRUCT, name, loc);
	if (e)
		e->ty = QT(c->ast->tags[b->tag].ty, 0);
}

VPRIV void cxx_class_access(cxx *c, int non_public)
{
	if (c->build)
		c->build->access_private = (uint8_t)non_public;
}

VPRIV void cxx_class_member_traits(cxx *c, int what)
{
	if (c->build && (what == CXX_MEMBER_USER_SPECIAL || what == CXX_MEMBER_INITIALIZER))
		c->build->pod = 0;
}

/* a base class: complete, non-virtual (a virtual base: a vtable layout, not built yet) */
VPRIV void cxx_class_base(cxx *c, qty t, bool is_virtual)
{
	cxx_class_build *b = c->build;
	uint32_t tag;

	if (!b || b->skip >= 0 || c->tentative)
		return;
	if (is_virtual) {
		b->skip = CXX_SKIP_VIRTUAL;
		return;
	}
	if (t != CXX_TY_UNKNOWN)
		cxx_type_require_complete(c, t);
	tag = cxx_record_tag(c, t);
	if (!tag || AST_TYPE_KIND(c->ast, t) == TY_ARRAY || !(c->ast->tags[tag].flags & RF_COMPLETE) || !cxx_info_of(c, tag)->valid ||
	    b->nbases == 16) {
		b->skip = CXX_SKIP_BASES;	/* (a base vcc did not lay out) */
		return;
	}
	b->bases[b->nbases++] = tag;
}

VPRIV void cxx_class_skip(cxx *c, int reason)
{
	if (c->build && c->build->skip < 0)
		c->build->skip = reason;
}

VPRIV void cxx_class_field(cxx *c, uint32_t name, qty t, uint32_t loc, int bitfield_width)
{
	cxx_class_build *b = c->build;
	field *f;

	if (!b || b->skip >= 0 || c->tentative)
		return;
	if (t == CXX_TY_UNKNOWN || bitfield_width == -2) {
		b->skip = t == CXX_TY_UNKNOWN ? CXX_SKIP_UNKNOWN_MEMBER : CXX_SKIP_BITFIELD;
		return;
	}
	if (AST_TYPE_KIND(c->ast, t) == TY_VOID || AST_TYPE_KIND(c->ast, t) == TY_FUNC) {
		b->skip = CXX_SKIP_UNKNOWN_MEMBER;	/* (ill-formed: never laid out) */
		return;
	}
	{
		qty et = t;	/* (an array's element must be complete too) */

		while (AST_TYPE_KIND(c->ast, et) == TY_ARRAY)
			et = AST_TYPE(c->ast, et)->base;
		if (cc_type_is_record(c->ast, et) && !(c->ast->tags[AST_TYPE(c->ast, et)->aux].flags & RF_COMPLETE))
			cxx_type_require_complete(c, et);	/* a member by value: an instance is completed now */
		if (AST_TYPE_KIND(c->ast, et) == TY_VOID || AST_TYPE_KIND(c->ast, et) == TY_FUNC ||
		    (cc_type_is_record(c->ast, et) && !(c->ast->tags[AST_TYPE(c->ast, et)->aux].flags & RF_COMPLETE))) {
			b->skip = CXX_SKIP_UNKNOWN_MEMBER;
			return;
		}
	}
	if (cc_type_is_record(c->ast, t) && !(c->ast->tags[AST_TYPE(c->ast, t)->aux].flags & RF_COMPLETE)) {
		b->skip = CXX_SKIP_UNKNOWN_MEMBER;	/* a member of a class that was not laid out */
		return;
	}
	if (b->nf == b->cf)
		b->f = realloc(b->f, (b->cf = b->cf ? b->cf * 2 : 8) * sizeof *b->f);
	f = &b->f[b->nf++];
	memset(f, 0, sizeof *f);
	f->name = name, f->ty = t, f->loc = loc;
	if (b->access_private)
		b->private_data = 1;
	else
		b->public_data = 1;
	if (AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext)
		b->pod = 0;	/* a reference member */
	{
		uint32_t mt = cxx_record_tag(c, t);

		if (mt && !cxx_info_of(c, mt)->pod)
			b->pod = 0;	/* a member of a non-POD class */
	}
	if (bitfield_width >= 0)
		f->bitfield = 1, f->width = (uint16_t)bitfield_width;
}

VPRIV void cxx_class_end(cxx *c)
{
	cxx_class_build *b = c->build;

	c->build = b->outer;
	if (c->tentative) {
		free(b->f);
		free(b);
		return;
	}
	c->anonymous_class = CXX_TY_UNKNOWN;
	if (b->tag && c->name_path_valid && c->name_path_len)	/* its name as source: the type printer's */
		cxx_tag_spelling_set(c, b->tag, c->name_path, c->name_path_len);
	if (b->skip >= 0 || !b->tag) {
		c->stats.classes_skipped[b->skip >= 0 ? b->skip : CXX_SKIP_TEMPLATE]++;
		if (c->build && b->skip >= 0)
			;	/* (a member of this type makes the enclosing class skip in its turn) */
	} else {
		tag *r = &c->ast->tags[b->tag];
		cxx_class_info *bi;
		uint64_t off = 0, data_end = 0;
		uint32_t balign = 1, nonempty = 0, empty_tags[16], nempty = 0;
		bool all_bases_pod = true, ok = true, any_empty_base = false;
		field *f;
		uint32_t nf;

		/* base subobjects (Itanium ABI, no virtual functions): in order; an empty one takes no
		   space at offset 0; a non-POD one's tail padding goes to what follows (its data size) */
		for (uint32_t i = 0; i < b->nbases; i++) {
			const tag *br = &c->ast->tags[b->bases[i]];
			cxx_class_info *ib = cxx_info_of(c, b->bases[i]);

			all_bases_pod &= ib->pod;
			if (br->align > balign)
				balign = br->align;
			if (ib->empty) {
				for (uint32_t k = 0; k < nempty; k++)
					ok &= empty_tags[k] != b->bases[i];	/* two of a type at one address: not here */
				if (nempty < 16)
					empty_tags[nempty++] = b->bases[i];
				any_empty_base = true;
				continue;
			}
			if (any_empty_base && (nonempty == 0 && (ib->has_empty_base || ib->empty)))
				ok = false;	/* (an empty base and a subobject at 0 that may hold the same type) */
			off = (off + br->align - 1) / br->align * br->align;
			off += ib->pod ? br->size : ib->dsize;
			nonempty++;
		}
		if (any_empty_base && !nonempty && b->nf) {
			uint32_t ft = cxx_record_tag(c, b->f[0].ty);

			for (uint32_t k = 0; k < nempty && ft; k++)
				ok &= empty_tags[k] != ft;	/* the first member of an empty base's type: displaced */
			if (ft && (cxx_info_of(c, ft)->has_empty_base || cxx_info_of(c, ft)->empty))
				ok = false;
		}
		if (!ok) {
			c->stats.classes_skipped[CXX_SKIP_EMPTY_BASE]++;
			goto done;
		}
		/* the members after them: a char[off] stands for the bases' bytes */
		nf = b->nf + (off ? 1 : 0);
		f = cc_heap_allocate(c->ast, (nf ? nf : 1) * sizeof(field));
		if (off) {
			memset(&f[0], 0, sizeof *f);
			f[0].ty = cc_type_array(c->ast, cc_type_basic(TY_CHAR), off, 0);
		}
		if (b->nf)
			memcpy(f + (off ? 1 : 0), b->f, b->nf * sizeof(field));
		r->nfield = nf;
		r->f = f;
		if (balign > 1 && balign > r->maxalign)
			r->maxalign = balign;	/* (the bases' alignment) */
		cc_layout_record(c->ast, r, NULL, NULL);
		for (uint32_t i = 0; i < nf; i++) {
			uint64_t end = f[i].bitfield ? f[i].off + f[i].width : f[i].off + AST_TYPE(c->ast, f[i].ty)->size * 8;

			if (end > data_end)
				data_end = end;
		}
		bi = cxx_info_of(c, b->tag);
		bi->valid = 1;
		bi->empty = !b->nf && !nonempty;
		bi->has_empty_base = any_empty_base;
		bi->dsize = (data_end + 7) / 8;
		/* POD for layout: trivial and standard-layout, as far as the parser sees it */
		bi->pod = b->pod && all_bases_pod && !(b->public_data && b->private_data) && nonempty <= 1 && !(b->nf && nonempty) &&
			  !(r->flags & RF_UNION ? 0 : 0);
		if (bi->empty) {	/* C++: an empty class has size 1 */
			r->size = 1, r->align = 1;
			c->ast->ty[r->ty].size = 1, c->ast->ty[r->ty].align = 1;
			bi->dsize = 0;
		}
		r->flags |= RF_COMPLETE;
		c->stats.classes_laid_out++;
		if (!r->name)
			c->anonymous_class = QT(r->ty, 0);
		if (c->layout_fd >= 0 && c->name_path_valid && c->name_path_len) {
			/* elaborated: struct stat / struct timezone share their names with a function or variable */
			const char *key = r->flags & RF_UNION ? "union" : "struct";

			dprintf(c->layout_fd, "static_assert(sizeof(%s %.*s) == %llu && alignof(%s %.*s) == %u, \"vcc layout\");\n",
				key, (int)c->name_path_len, c->name_path, (unsigned long long)r->size, key, (int)c->name_path_len,
				c->name_path, r->align);
		}
	}
done:
	free(b->f);
	free(b);
}

VPRIV void cxx_enum_type(cxx *c, cxx_entity *e, uint32_t name, uint32_t loc, qty underlying)
{
	uint32_t ti;
	ctype *t;

	if (c->tentative || !e || e->ty != CXX_TY_UNKNOWN)
		return;
	ti = cc_tag_new(c->ast, TY_ENUM, name, loc);
	t = &c->ast->ty[c->ast->tags[ti].ty];
	if (underlying != CXX_TY_UNKNOWN && underlying != 0) {
		const ctype *u = AST_TYPE(c->ast, underlying);

		t->base = QUNQ(underlying), t->size = u->size, t->align = u->align;
	} else
		t->base = cc_type_basic(TY_INT);	/* (C++: int unless the values need more: not evaluated yet) */
	c->ast->tags[ti].flags |= RF_COMPLETE;
	e->ty = QT(c->ast->tags[ti].ty, 0);
}

VPRIV uint32_t cxx_symbol(cxx *c, int kind, uint32_t name, qty t, uint32_t loc)
{
	uint32_t s;

	if (c->tentative || t == CXX_TY_UNKNOWN)
		return 0;
	s = cc_symbol_new(c->ast, kind, name, t, loc);
	c->ast->syms[s].flags |= SF_GLOBAL;
	c->stats.symbols++;
	return s;
}

/* ---- qualified names, for the layout dump -------------------------------------------------- */

VPRIV void cxx_name_path_push(cxx *c, uint32_t name, uint32_t *saved_len, uint8_t *saved_valid)
{
	*saved_len = c->name_path_len;
	*saved_valid = c->name_path_valid;
	if (!name) {
		c->name_path_valid = 0;
		return;
	}
	{
		const vident *v = &vintern_ents[name];
		uint32_t n = c->name_path_len;

		if (n + 2 + v->len >= sizeof c->name_path) {
			c->name_path_valid = 0;
			return;
		}
		if (n)
			c->name_path[n++] = ':', c->name_path[n++] = ':';
		memcpy(c->name_path + n, v->s, v->len);
		c->name_path_len = n + v->len;
	}
}

VPRIV void cxx_name_path_pop(cxx *c, uint32_t saved_len, uint8_t saved_valid)
{
	c->name_path_len = saved_len;
	c->name_path_valid = saved_valid;
}
