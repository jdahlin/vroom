/* vcc front end: the record layout self-check (-fdump-record-layouts) that clang verifies */
#include "ccimpl.h"

/* x86-64: functions clang 23 declares itself (TARGET_HEADER_BUILTIN): naming one other than in a
   call is an error there ("builtin functions must be directly called"), so no claim about its type */
static bool x86_header_builtin(cc *c, uint32_t name)
{
	static const char *const names[] = { "__rdtsc", "_m_prefetch", "_m_prefetchw", "_mm_clflush", "_mm_getcsr",
		"_mm_lfence", "_mm_mfence", "_mm_pause", "_mm_prefetch", "_mm_setcsr", "_mm_sfence" };

	if (!CC_X86_64(c))
		return false;
	for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++)
		if (idlen_(name) == strlen(names[i]) && !memcmp(idstr_(name), names[i], idlen_(name)))
			return true;
	return false;
}
#include "common/buffer.h"

/* ---------------- layout checks (clang verifies them) ---------------- */

static void field_checks(cc *c, vbuffer *o, const char *rname, uint32_t ti, uint64_t base, const char *prefix)
{
	const tag *r = &c->tags[ti];

	for (uint32_t i = 0; i < r->nfield; i++) {
		const field *f = &r->f[i];

		if (f->bitfield)
			continue;
		if (!f->name) {
			if (is_record(c, f->ty))
				field_checks(c, o, rname, T(c, f->ty)->aux, base + f->off / 8, prefix);
			continue;
		}
		vbuffer_format_append(o, "_Static_assert(__builtin_offsetof(%s, %s%.*s) == %llu, \"vcc\");\n", rname, prefix,
			IDF(f->name), (unsigned long long)(base + f->off / 8));
	}
}

VPRIV void layout_checks(cc *c)
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

		if (!(r->flags & RF_COMPLETE))
			continue;
		if (r->name) {
			uint32_t s = lookup_tag(c, r->name);

			if (!s || (uint32_t)c->syms[s].val != i)
				continue;
			snprintf(rn, sizeof(rn), "%s %.*s", k == TY_STRUCT ? "struct" : k == TY_UNION ? "union" : "enum",
				 IDF(r->name));
		} else if (r->typedef_name) {
			uint32_t s = lookup(c, r->typedef_name);

			if (!s || c->syms[s].kind != S_TYPEDEF || QI(c->syms[s].ty) != r->ty)
				continue;
			snprintf(rn, sizeof(rn), "%.*s", IDF(r->typedef_name));
		} else {
			continue;
		}
		vbuffer_format_append(&o, "_Static_assert(sizeof(%s) == %llu && _Alignof(%s) == %u, \"vcc\");\n", rn,
			(unsigned long long)r->size, rn, r->align);
		if (k == TY_ENUM) {
			const char *bn = int_name(KIND(c, c->ty[r->ty].base));

			if (bn)
				vbuffer_format_append(&o, "_Static_assert(__builtin_types_compatible_p(%s, %s), \"vcc\");\n", rn, bn);
			continue;
		}
		field_checks(c, &o, rn, i, 0, "");
	}
	for (uint32_t i = 0; i < c->ntoplevel; i++) {
		const sym *s = &c->syms[c->toplevel[i]];

		if (!s->name || lookup(c, s->name) != c->toplevel[i])
			continue;
		if (s->kind == S_TYPEDEF) {
			if (complete(c, s->ty) && !is_func(c, s->ty))
				vbuffer_format_append(&o, "_Static_assert(sizeof(%.*s) == %llu && _Alignof(%.*s) == %u, \"vcc\");\n",
					IDF(s->name), (unsigned long long)ty_size(c, s->ty), IDF(s->name), ty_align(c, s->ty));
		} else if (s->kind == S_VAR && complete(c, s->ty)) {
			vbuffer_format_append(&o, "_Static_assert(sizeof(%.*s) == %llu, \"vcc\");\n", IDF(s->name),
				(unsigned long long)ty_size(c, s->ty));
		}
		if ((s->kind == S_VAR || s->kind == S_FUNC) && !(s->kind == S_FUNC && x86_header_builtin(c, s->name))) {
			char b_bytes[4096];
			vbuffer b;
			bool ok = true;

			vbuffer_fixed(&b, b_bytes, sizeof(b_bytes));
			type_str(c, s->ty, "", &b, &ok);
			if (ok)
				vbuffer_format_append(&o, "_Static_assert(__builtin_types_compatible_p(__typeof__(%.*s), %s), \"vcc\");\n",
					IDF(s->name), b.p);
		}
	}
	/* enumerators: value and type */
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *s = &c->syms[i];
		const char *tn;

		if (s->kind != S_ENUMCONST || s->depth != 0 || lookup(c, s->name) != i)
			continue;
		tn = int_name(KIND(c, KIND(c, s->ty) == TY_ENUM ? T(c, s->ty)->base : s->ty));
		if (!tn)
			continue;
		vbuffer_format_append(&o, "_Static_assert((%.*s) == %lldLL && __builtin_types_compatible_p(__typeof__(%.*s), %s), \"vcc\");\n",
			IDF(s->name), (long long)s->val, IDF(s->name), tn);
	}
	vbuffer_flush(&o);
	free(o.p);
}
