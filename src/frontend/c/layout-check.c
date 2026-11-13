/* vcc front end: the record layout self-check (-fdump-record-layouts) that clang verifies */
#include "cc-internal.h"

/* x86-64: functions clang 23 declares itself (TARGET_HEADER_BUILTIN): naming one other than in a
   call is an error there ("builtin functions must be directly called"), so no claim about its type */
static bool x86_header_builtin(cc *c, uint32_t name)
{
	static const char *const names[] = { "__rdtsc", "_m_prefetch", "_m_prefetchw", "_mm_clflush", "_mm_getcsr",
		"_mm_lfence", "_mm_mfence", "_mm_pause", "_mm_prefetch", "_mm_setcsr", "_mm_sfence" };

	if (!CC_X86_64(c))
		return false;
	for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++)
		if ((size_t)cc_identifier_length(name) == strlen(names[i]) && !memcmp(cc_identifier_string(name), names[i], cc_identifier_length(name)))
			return true;
	return false;
}
#include "common/buffer.h"

/* ---------------- layout checks (clang verifies them) ---------------- */

static void cc_layout_check_fields(cc *c, vbuffer *o, const char *rname, uint32_t ti, uint64_t base, const char *prefix)
{
	const tag *r = &c->tags[ti];

	for (uint32_t i = 0; i < r->nfield; i++) {
		const field *f = &r->f[i];

		if (f->bitfield)
			continue;
		if (!f->name) {
			if (cc_type_is_record(c, f->ty))
				cc_layout_check_fields(c, o, rname, AST_TYPE(c, f->ty)->aux, base + f->off / 8, prefix);
			continue;
		}
		vbuffer_format_append(o, "_Static_assert(__builtin_offsetof(%s, %s%.*s) == %llu, \"vcc\");\n", rname, prefix,
			CC_IDENTIFIER(f->name), (unsigned long long)(base + f->off / 8));
	}
}

CC_PRIVATE void cc_layout_check(cc *c)
{
	char *storage = malloc(1 << 20);
	vbuffer o;
	char rn[512];

	if (!storage)
		vout_of_memory();
	vbuffer_fd(&o, storage, 1 << 20, c->o->outfd);
	vbuffer_format_append(&o, "\n/* vcc layout checks */\n");
	for (uint32_t i = 0; i < c->ntag; i++) {
		const tag *r = &c->tags[i];
		int k = c->ty[r->ty].kind;

		if (!(r->flags & AST_RECORD_FLAG_COMPLETE))
			continue;
		if (r->name) {
			uint32_t s = cc_tag_lookup(c, r->name);

			if (!s || (uint32_t)c->syms[s].val != i)
				continue;
			snprintf(rn, sizeof(rn), "%s %.*s", k == AST_TYPE_STRUCT ? "struct" : k == AST_TYPE_UNION ? "union" : "enum",
				 CC_IDENTIFIER(r->name));
		} else if (r->typedef_name) {
			uint32_t s = cc_symbol_lookup(c, r->typedef_name);

			if (!s || c->syms[s].kind != AST_SYMBOL_TYPEDEF || AST_QUALIFIED_TYPE_INDEX(c->syms[s].ty) != r->ty)
				continue;
			snprintf(rn, sizeof(rn), "%.*s", CC_IDENTIFIER(r->typedef_name));
		} else {
			continue;
		}
		vbuffer_format_append(&o, "_Static_assert(sizeof(%s) == %llu && _Alignof(%s) == %u, \"vcc\");\n", rn,
			(unsigned long long)r->size, rn, r->align);
		if (k == AST_TYPE_ENUM) {
			const char *bn = cc_type_integer_name(AST_TYPE_KIND(c, c->ty[r->ty].base));

			if (bn)
				vbuffer_format_append(&o, "_Static_assert(__builtin_types_compatible_p(%s, %s), \"vcc\");\n", rn, bn);
			continue;
		}
		cc_layout_check_fields(c, &o, rn, i, 0, "");
	}
	for (uint32_t i = 0; i < c->ntoplevel; i++) {
		const sym *s = &c->syms[c->toplevel[i]];

		if (!s->name || cc_symbol_lookup(c, s->name) != c->toplevel[i])
			continue;
		if (s->kind == AST_SYMBOL_TYPEDEF) {
			if (cc_type_complete(c, s->ty) && !cc_type_is_function(c, s->ty))
				vbuffer_format_append(&o, "_Static_assert(sizeof(%.*s) == %llu && _Alignof(%.*s) == %u, \"vcc\");\n",
					CC_IDENTIFIER(s->name), (unsigned long long)cc_type_size(c, s->ty), CC_IDENTIFIER(s->name), cc_type_alignment(c, s->ty));
		} else if (s->kind == AST_SYMBOL_VARIABLE && cc_type_complete(c, s->ty)) {
			vbuffer_format_append(&o, "_Static_assert(sizeof(%.*s) == %llu, \"vcc\");\n", CC_IDENTIFIER(s->name),
				(unsigned long long)cc_type_size(c, s->ty));
		}
		if ((s->kind == AST_SYMBOL_VARIABLE || s->kind == AST_SYMBOL_FUNCTION) && !(s->kind == AST_SYMBOL_FUNCTION && x86_header_builtin(c, s->name))) {
			char b_bytes[4096];
			vbuffer b;
			bool ok = true;

			vbuffer_fixed(&b, b_bytes, sizeof(b_bytes));
			cc_type_spell(c, s->ty, "", &b, &ok);
			if (ok)
				vbuffer_format_append(&o, "_Static_assert(__builtin_types_compatible_p(__typeof__(%.*s), %s), \"vcc\");\n",
					CC_IDENTIFIER(s->name), b.p);
		}
	}
	/* enumerators: value and type */
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *s = &c->syms[i];
		const char *tn;

		if (s->kind != AST_SYMBOL_ENUMERATOR || s->depth != 0 || cc_symbol_lookup(c, s->name) != i)
			continue;
		tn = cc_type_integer_name(AST_TYPE_KIND(c, AST_TYPE_KIND(c, s->ty) == AST_TYPE_ENUM ? AST_TYPE(c, s->ty)->base : s->ty));
		if (!tn)
			continue;
		vbuffer_format_append(&o, "_Static_assert((%.*s) == %lldLL && __builtin_types_compatible_p(__typeof__(%.*s), %s), \"vcc\");\n",
			CC_IDENTIFIER(s->name), (long long)s->val, CC_IDENTIFIER(s->name), tn);
	}
	vbuffer_flush(&o);
	free(o.p);
}
