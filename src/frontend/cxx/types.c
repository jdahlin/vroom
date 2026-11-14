/*
 * types.c: the C++ parser's types and records, built in the shared AST
 * (frontend/ast.h) with the C front end's code: type interning, record
 * layout, symbols (PLAN-cxx.md X1).
 *
 * References are TY_PTR types with ext 1 (T&) or 2 (T&&): distinct when
 * interned, passed as pointers (their ABI).  wchar_t, char8_t, char16_t and
 * char32_t are the C kinds of their size and signedness with ext 1: distinct
 * types (X<wchar_t> is its own instance), the same code.
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
	if (s->v_named) {
		/* (const T with T a reference type, a template argument: the qualifier is dropped) */
		if (s->named != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, s->named) == TY_PTR && AST_TYPE(c->ast, s->named)->ext)
			return s->named;
		return s->named == CXX_TY_UNKNOWN ? CXX_TY_UNKNOWN : cc_type_qualify(c->ast, s->named, s->quals);
	}
	if (s->v_void) k = TY_VOID;
	else if (s->v_bool) k = TY_BOOL;
	else if (s->v_char) k = s->v_unsigned ? TY_UCHAR : s->v_signed ? TY_SCHAR : TY_CHAR;
	else if (s->v_char8 || s->v_char16 || s->v_char32 || s->v_wchar) {
		/* distinct types (X<wchar_t> is not X<int>): the C kind of their size and signedness, ext 1 */
		ctype t = c->ast->ty[s->v_char8 ? TY_UCHAR : s->v_char16 ? TY_USHORT : s->v_char32 ? TY_UINT : TY_INT];

		t.ext = 1;
		return cc_type_qualify(c->ast, QT(cc_type_intern(c->ast, &t), 0), s->quals);
	}
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
	if (base == CXX_TY_UNKNOWN)	/* (in a tentative parse too: a type is interned, nothing to undo) */
		return CXX_TY_UNKNOWN;
	return cc_type_qualify(c->ast, cc_type_pointer(c->ast, base), quals);
}

VPRIV qty cxx_type_reference(cxx *c, qty base, bool rvalue)
{
	if (base == CXX_TY_UNKNOWN)
		return CXX_TY_UNKNOWN;
	/* a reference to a reference (T& with T = U&&, through a typedef or a template argument) collapses:
	   an rvalue reference only if both are */
	if (AST_TYPE_KIND(c->ast, base) == TY_PTR && AST_TYPE(c->ast, base)->ext) {
		if (rvalue || AST_TYPE(c->ast, base)->ext == 1)
			return QUNQ(base);
		base = AST_TYPE(c->ast, base)->base;
	}
	{
		ctype t = { .kind = TY_PTR, .size = CC_PTRSIZE(c->ast), .align = CC_PTRSIZE(c->ast), .base = base,
			    .ext = (uint16_t)(rvalue ? 2 : 1) };

		return QT(cc_type_intern(c->ast, &t), 0);
	}
}

VPRIV qty cxx_type_array(cxx *c, qty elem, int64_t n)
{
	if (elem == CXX_TY_UNKNOWN)
		return CXX_TY_UNKNOWN;
	if (n <= -3) {
		/* a pattern's T[N]: an incomplete array that remembers which value parameter is its bound (ext: its
		   index + 1); deduction binds it to an argument array's length */
		ctype t = { .kind = TY_ARRAY, .size = 0, .align = AST_TYPE(c->ast, elem)->align, .base = elem, .flags = TF_INCOMPLETE,
			    .ext = (uint16_t)(-3 - n + 1) };

		return QT(cc_type_intern(c->ast, &t), 0);
	}
	if (n < 0)
		return cc_type_array(c->ast, elem, 0, TF_INCOMPLETE);
	return cc_type_array(c->ast, elem, (uint64_t)n, 0);
}

VPRIV qty cxx_type_function(cxx *c, qty ret, const qty *params, uint32_t n, bool variadic)
{
	if (ret == CXX_TY_UNKNOWN)
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
	uint8_t *nua;			/* per field: [[no_unique_address]] */
	uint32_t nf, cf;
	uint8_t packed;			/* __attribute__((packed)) on the class */
	uint32_t skip_name;		/* the member whose type is not known (VCC_CXX_WHY_LAYOUT) */
	int skip;			/* -1: lay it out; else CXX_SKIP_* */
	uint8_t unverifiable;		/* a member type of an instance did not resolve: clang may reject the instance */
	uint32_t path_len;		/* the name path before this class (layout dump) */
	uint8_t path_valid;
	/* the Itanium ABI's inputs: base subobjects, and whether the class is POD for layout (a non-POD
	   base's tail padding is reused by the derived class's members) */
	uint32_t bases[16], nbases;
	uint8_t pod, access_private, public_data, private_data;
	uint8_t virt;			/* it declares a virtual function */
	uint8_t nsdmi;			/* a member has a default initializer */
} cxx_class_build;

/* per AST tag: what a derived class's layout needs of this class */
typedef struct cxx_class_info {
	uint64_t dsize;			/* data size: bytes up to the end of its last data, before tail padding */
	uint8_t valid, pod, empty, has_empty_base;
	uint8_t polymorphic;		/* it has a vptr: its own (at offset 0) or its primary base's */
	uint8_t nsdmi;			/* a member has a default initializer (its constructors apply it) */
	/* its direct base classes: their tags and offsets (code generation: a derived object as its base) */
	uint8_t nbases;
	uint32_t base_tag[16], base_off[16];
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
	/* (a class nested in a non-public part of another, or in one clang cannot check: not checked either) */
	b->unverifiable = b->outer && c->class_depth > 0 && (b->outer->access_private || b->outer->unverifiable);	/* (class_depth: inside a class body, not an instance completed while one is being built) */
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

VPRIV void cxx_class_packed(cxx *c)
{
	if (c->build)
		c->build->packed = 1;
}

/* a class's data size (bytes before its tail padding); a type that is not a class: its size */
VPRIV bool cxx_class_data_size(cxx *c, qty t, uint64_t *dsize)
{
	const cxx_class_info *i;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
		return false;
	cxx_type_require_complete(c, QUNQ(t));
	i = cxx_info_of(c, AST_TYPE(c->ast, t)->aux);
	if (!i->valid)
		return false;
	/* (POD for layout: its tail padding is not reused, the data size is the size) */
	*dsize = i->empty || i->pod ? AST_TYPE(c->ast, t)->size : i->dsize;
	return true;
}

/* the offset in bits of the data member `name` of the laid-out class t (in an anonymous struct or union
   member too; not in a base) */
VPRIV bool cxx_class_field_offset(cxx *c, qty t, uint32_t name, uint64_t *bits, int depth)
{
	const tag *r;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == TY_ARRAY || depth > 8)
		return false;
	if (!depth)
		cxx_type_require_complete(c, QUNQ(t));
	r = &c->ast->tags[AST_TYPE(c->ast, t)->aux];
	if (!(r->flags & RF_COMPLETE))
		return false;
	for (uint32_t i = 0; i < r->nfield; i++) {
		uint64_t inner;

		if (r->f[i].name == name && name) {
			*bits = r->f[i].off;
			return true;
		}
		if (!r->f[i].name && !r->f[i].bitfield && cxx_class_field_offset(c, r->f[i].ty, name, &inner, depth + 1)) {
			*bits = r->f[i].off + inner;
			return true;
		}
	}
	return false;
}

/*
 * The offset of the base class subobject `base` in an object of class `derived` (a direct or indirect
 * base, the first found); false: not a base of it, or a layout not known.
 */
VPRIV bool cxx_class_base_offset(cxx *c, qty derived, qty base, uint64_t *off, int depth)
{
	const cxx_class_info *i;
	uint32_t dt, bt;

	if (derived == CXX_TY_UNKNOWN || base == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, derived) || !cc_type_is_record(c->ast, base) ||
	    AST_TYPE_KIND(c->ast, derived) == TY_ARRAY || AST_TYPE_KIND(c->ast, base) == TY_ARRAY || depth > 8)
		return false;
	if (!depth)
		cxx_type_require_complete(c, QUNQ(derived));
	dt = AST_TYPE(c->ast, derived)->aux, bt = AST_TYPE(c->ast, base)->aux;
	i = cxx_info_of(c, dt);
	if (!i->valid)
		return false;
	for (uint32_t k = 0; k < i->nbases; k++) {
		uint64_t inner;

		if (i->base_tag[k] == bt) {
			*off = i->base_off[k];
			return true;
		}
		if (cxx_class_base_offset(c, QT(c->ast->tags[i->base_tag[k]].ty, 0), base, &inner, depth + 1)) {
			*off = i->base_off[k] + inner;
			return true;
		}
	}
	return false;
}

/* a class's direct bases: how many, and the k-th one's type */
VPRIV uint32_t cxx_class_bases(cxx *c, qty t, qty *bases, uint32_t cap)
{
	const cxx_class_info *i;
	uint32_t n = 0;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
		return 0;
	i = cxx_info_of(c, AST_TYPE(c->ast, t)->aux);
	if (!i->valid)
		return 0;
	for (; n < i->nbases && n < cap; n++)
		bases[n] = QT(c->ast->tags[i->base_tag[n]].ty, 0);
	return n;
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
	if (c->build && what == CXX_MEMBER_INITIALIZER)
		c->build->nsdmi = 1;
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

/* the class declares a virtual function */
VPRIV void cxx_class_virtual(cxx *c)
{
	if (c->build && !c->tentative)
		c->build->virt = 1, c->build->pod = 0;
}

/* does a member of the class have a default initializer? */
VPRIV bool cxx_class_has_member_initializer(cxx *c, qty t)
{
	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
		return false;
	return cxx_info_of(c, AST_TYPE(c->ast, t)->aux)->nsdmi;
}

/* does the class have a vptr (a virtual function of its own or of a base)? */
VPRIV bool cxx_class_polymorphic(cxx *c, qty t)
{
	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
		return false;
	return cxx_info_of(c, AST_TYPE(c->ast, t)->aux)->polymorphic;
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
		b->skip_name = name;
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
	if (b->nf == b->cf) {
		b->f = realloc(b->f, (b->cf = b->cf ? b->cf * 2 : 8) * sizeof *b->f);
		b->nua = realloc(b->nua, b->cf);
	}
	b->nua[b->nf] = c->attr_nua && bitfield_width < 0;
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
		if (b->skip != CXX_SKIP_TEMPLATE && b->skip >= 0 && getenv("VCC_CXX_WHY_LAYOUT"))
			fprintf(stderr, "no layout: %.*s: %s%s%.*s\n", c->name_path_valid ? (int)c->name_path_len : 1, c->name_path_valid ? c->name_path : "?",
				(const char *[]){ "template", "base", "virtual", "member", "bit-field", "specialization", "attribute", "empty base" }[b->skip],
				b->skip_name ? " " : "", b->skip_name ? (int)vintern_ents[b->skip_name].len : 0, b->skip_name ? vintern_ents[b->skip_name].s : "");
		if (c->build && b->skip >= 0)
			;	/* (a member of this type makes the enclosing class skip in its turn) */
	} else {
		tag *r = &c->ast->tags[b->tag];
		cxx_class_info *bi;
		uint64_t off = 0, data_end = 0;
		uint32_t balign = 1, nonempty = 0, empty_tags[16], nempty = 0;
		bool all_bases_pod = true, ok = true, any_empty_base = false;
		uint32_t base_offs[16] = { 0 };
		field *f;
		uint32_t nf;

		/* base subobjects (Itanium ABI, no virtual functions): in order; an empty one takes no
		   space at offset 0; a non-POD one's tail padding goes to what follows (its data size) */
		/* a class with virtual functions: its primary base (the first base with a vptr) comes first, at
		   offset 0, and shares its vptr; with no such base the class's own vptr is its first 8 bytes */
		int primary = -1;
		bool polymorphic = b->virt;

		for (uint32_t i = 0; i < b->nbases; i++)
			if (cxx_info_of(c, b->bases[i])->polymorphic) {
				if (primary < 0)
					primary = (int)i;
				polymorphic = true;
			}
		if (polymorphic && primary < 0)
			off = 8, balign = 8, nonempty++;
		for (uint32_t k = 0; k < b->nbases; k++) {
			uint32_t i = primary < 0 ? k : k == 0 ? (uint32_t)primary : k <= (uint32_t)primary ? k - 1 : k;
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
				base_offs[i] = 0;
				continue;
			}
			if (any_empty_base && (nonempty == 0 && (ib->has_empty_base || ib->empty)))
				ok = false;	/* (an empty base and a subobject at 0 that may hold the same type) */
			off = (off + br->align - 1) / br->align * br->align;
			base_offs[i] = (uint32_t)off;
			off += ib->pod ? br->size : ib->dsize;
			nonempty++;
		}
		/* (the first member that takes space: an empty [[no_unique_address]] one is checked with them below) */
		uint32_t first = 0;

		while (first < b->nf && b->nua[first] && AST_TYPE_KIND(c->ast, b->f[first].ty) != TY_ARRAY && cxx_record_tag(c, b->f[first].ty) &&
		       cxx_info_of(c, cxx_record_tag(c, b->f[first].ty))->empty)
			first++;
		if (any_empty_base && !nonempty && first < b->nf) {
			uint32_t ft = cxx_record_tag(c, b->f[first].ty);

			for (uint32_t k = 0; k < nempty && ft; k++)
				ok &= empty_tags[k] != ft;	/* the first member of an empty base's type: displaced */
			if (ft && (cxx_info_of(c, ft)->has_empty_base || cxx_info_of(c, ft)->empty))
				ok = false;
		}
		if (!ok) {
			c->stats.classes_skipped[CXX_SKIP_EMPTY_BASE]++;
			goto done;
		}
		/* [[no_unique_address]] members of class type: an empty one takes no space (at offset 0; two of
		   a type there, or one of an empty base's type, is not done here); one that is not POD for
		   layout gives its tail padding to what follows, as a base does */
		uint32_t nlay = 0, nfields_nonempty = 0;
		field *lay = malloc((b->nf + 2) * sizeof *lay);
		uint32_t *fa = calloc(b->nf + 2, sizeof *fa), *at = malloc((b->nf + 2) * sizeof *at);
		qty *orig = malloc((b->nf + 2) * sizeof *orig);

		if (off) {
			memset(&lay[0], 0, sizeof *lay);
			lay[0].ty = cc_type_array(c->ast, cc_type_basic(TY_CHAR), off, 0);
			orig[0] = lay[0].ty, at[0] = UINT32_MAX;
			nlay = 1;
		}
		for (uint32_t i = 0; i < b->nf; i++) {
			uint32_t mt = b->nua[i] && AST_TYPE_KIND(c->ast, b->f[i].ty) != TY_ARRAY ? cxx_record_tag(c, b->f[i].ty) : 0;
			const cxx_class_info *mi = mt ? cxx_info_of(c, mt) : NULL;

			if (mi && mi->valid && mi->empty) {
				for (uint32_t k = 0; k < nempty; k++)
					ok &= empty_tags[k] != mt;
				if (nempty < 16)
					empty_tags[nempty++] = mt;
				else
					ok = false;
				continue;
			}
			if (mi && mi->has_empty_base)
				ok &= !nempty;	/* (its empty bases might share an address with ours) */
			lay[nlay] = b->f[i], orig[nlay] = b->f[i].ty, at[nlay] = i;
			if (mi && mi->valid && !mi->pod && mi->dsize && mi->dsize < AST_TYPE(c->ast, b->f[i].ty)->size) {
				lay[nlay].ty = cc_type_array(c->ast, cc_type_basic(TY_CHAR), mi->dsize, 0);
				fa[nlay] = AST_TYPE(c->ast, b->f[i].ty)->align;
			}
			nlay++, nfields_nonempty++;
		}
		if (!ok) {
			c->stats.classes_skipped[CXX_SKIP_EMPTY_BASE]++;
			free(lay), free(fa), free(at), free(orig);
			goto done;
		}
		nf = b->nf + (off ? 1 : 0);
		f = cc_heap_allocate(c->ast, (nf ? nf : 1) * sizeof(field));
		r->nfield = nlay;
		r->f = lay;
		if (b->packed)
			r->flags |= RF_PACKED;
		if (balign > 1 && balign > r->maxalign)
			r->maxalign = balign;	/* (the bases' alignment) */
		cc_layout_record(c->ast, r, fa, NULL);
		for (uint32_t i = 0; i < nlay; i++) {
			uint64_t end = lay[i].bitfield ? lay[i].off + lay[i].width : lay[i].off + AST_TYPE(c->ast, lay[i].ty)->size * 8;

			if (end > data_end)
				data_end = end;
		}
		/* the record's fields: the bases' bytes, then every member in declaration order (the empty ones at 0) */
		{
			uint32_t o = off ? 1 : 0;

			if (off)
				f[0] = lay[0];
			for (uint32_t i = 0; i < b->nf; i++)
				f[o + i] = b->f[i], f[o + i].off = 0, f[o + i].align = 1;
			for (uint32_t i = o; i < nlay; i++) {
				f[o + at[i]] = lay[i];
				f[o + at[i]].ty = orig[i];
			}
		}
		r->nfield = nf;
		r->f = f;
		free(lay), free(fa), free(at), free(orig);
		bi = cxx_info_of(c, b->tag);
		bi->valid = 1;
		bi->nbases = (uint8_t)b->nbases;
		memcpy(bi->base_tag, b->bases, b->nbases * sizeof *b->bases);
		memcpy(bi->base_off, base_offs, b->nbases * sizeof *base_offs);
		bi->empty = !nfields_nonempty && !nonempty;
		bi->has_empty_base = any_empty_base;
		bi->polymorphic = polymorphic;
		bi->nsdmi = b->nsdmi;
		bi->dsize = (data_end + 7) / 8;
		/* POD for layout: trivial and standard-layout, as far as the parser sees it */
		bi->pod = !polymorphic && b->pod && all_bases_pod && !(b->public_data && b->private_data) && nonempty <= 1 && !(nfields_nonempty && nonempty) &&
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
		if (c->layout_fd >= 0 && c->name_path_valid && c->name_path_len && !b->unverifiable) {
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

/* sizeof and alignof t, when vcc knows them: scalars, pointers, arrays of those, classes it laid out */
VPRIV bool cxx_type_size_known(cxx *c, qty t, uint64_t *size, uint32_t *align)
{
	const ctype *x;

	if (t == CXX_TY_UNKNOWN)
		return false;
	x = AST_TYPE(c->ast, t);
	if (x->kind == TY_PTR && x->ext)	/* a reference: its referent's */
		return cxx_type_size_known(c, x->base, size, align);
	if ((x->kind >= TY_BOOL && x->kind <= TY_FLOAT128) || x->kind == TY_PTR || x->kind == TY_ENUM) {
		*size = x->size, *align = x->align;
		return x->size != 0;
	}
	if (x->kind == TY_ARRAY) {
		uint64_t es;

		if (!x->n || !cxx_type_size_known(c, x->base, &es, align) || x->size != es * x->n)
			return false;
		*size = x->size;
		return true;
	}
	if (x->kind == TY_STRUCT || x->kind == TY_UNION) {
		cxx_type_require_complete(c, QUNQ(t));
		x = AST_TYPE(c->ast, t);
		if (!cxx_info_of(c, x->aux)->valid || !x->size)
			return false;
		*size = x->size, *align = x->align;
		return true;
	}
	return false;
}

/* an instance's member typedef has no type: its layout is not asserted (the instance may be ill-formed
   and only named where that is harmless, std::__to_address_arrow_result<const char *>) */
VPRIV void cxx_class_unverifiable(cxx *c)
{
	if (c->build && c->instantiating && !c->tentative)
		c->build->unverifiable = 1;
}

/* a class vcc laid out: is it POD for layout (trivial and standard-layout, as far as vcc sees), is it empty? */
VPRIV bool cxx_class_layout_info(cxx *c, qty t, bool *pod, bool *empty)
{
	const cxx_class_info *i;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t))
		return false;
	cxx_type_require_complete(c, QUNQ(t));
	i = cxx_info_of(c, AST_TYPE(c->ast, t)->aux);
	if (!i->valid)
		return false;
	*pod = i->pod, *empty = i->empty;
	return true;
}
