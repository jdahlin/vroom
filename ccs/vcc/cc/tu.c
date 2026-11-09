/* vcc front end: the per-TU tables and the entry points (cc_init, cc_run, cc_free) */
#include "ccimpl.h"

/* the per-TU tables, mapped once and reused (cc_init takes a set, cc_free gives it back) */
static vmpool ccpool = VMPOOL_INIT(21);
static _Thread_local vmreg *ccr;

VPRIV void *halloc(cc *c, size_t n)
{
	char *r;

	n = (n + 15) & ~(size_t)15;
	if (unlikely(c->hp + n > c->hend))
		fatal(c, c->prevloc, "translation unit too large (front-end heap full)");
	r = c->hp;
	c->hp += n;
	return r;
}

/* ---------------- driver ---------------- */

static void cc_init(cc *c, pp *p, const cc_opts *o)
{
	memset(c, 0, sizeof(*c));
	c->p = p;
	c->o = o;
	ccr = vmpool_take(&ccpool);
	c->ty = vmreg_get(&ccr[0], (size_t)1 << 30, "cc.ty");
	c->params = vmreg_get(&ccr[1], (size_t)1 << 28, "cc.params");
	c->tags = vmreg_get(&ccr[2], (size_t)1 << 28, "cc.tags");
	c->syms = vmreg_get(&ccr[3], (size_t)1 << 30, "cc.syms");
	c->nodes = vmreg_get(&ccr[4], (size_t)1 << 32, "cc.nodes");
	c->lists = vmreg_get(&ccr[5], (size_t)1 << 30, "cc.lists");
	c->ipool = vmreg_get(&ccr[6], (size_t)1 << 30, "cc.ipool");
	c->fpool = vmreg_get(&ccr[7], (size_t)1 << 26, "cc.fpool");
	c->strs = vmreg_get(&ccr[8], (size_t)1 << 28, "cc.strs");
	c->labels = vmreg_get(&ccr[9], (size_t)1 << 26, "cc.labels");
	c->attr = vmreg_get(&ccr[10], (size_t)1 << 28, "cc.attr");
	c->asms = vmreg_get(&ccr[11], (size_t)1 << 28, "cc.asms");
	c->sstack = vmreg_get(&ccr[12], (size_t)1 << 28, "cc.sstack");
	c->toplevel = vmreg_get(&ccr[13], (size_t)1 << 26, "cc.toplevel");
	c->tlasm = vmreg_get(&ccr[14], (size_t)1 << 24, "cc.tlasm");
	c->heap = c->hp = vmreg_get(&ccr[15], (size_t)1 << 32, "cc.heap");
	c->ptrc = vmreg_get(&ccr[16], (size_t)1 << 30, "cc.ptrc");
	c->span = vmreg_get(&ccr[17], (size_t)1 << 31, "cc.span");
	c->lzt = vmreg_get(&ccr[18], (size_t)1 << 32, "cc.lzt");
	c->lzf = vmreg_get(&ccr[19], (size_t)1 << 28, "cc.lzf");
	c->lzs = vmreg_get(&ccr[20], (size_t)1 << 31, "cc.lzs");
	c->hend = c->heap + ((size_t)1 << 32);
	c->dops = halloc(c, DOPS_MAX * sizeof(dop));
	c->ndops = 0;
	tl_fscr.n = 0;
	c->tyhcap = 1 << 16;
	c->tyhash = halloc(c, c->tyhcap * sizeof(uint32_t));
	memset(c->tyhash, 0, c->tyhcap * sizeof(uint32_t));
	c->fnhcap = 1 << 14;
	c->fnhash = halloc(c, c->fnhcap * sizeof(uint32_t));
	memset(c->fnhash, 0, c->fnhcap * sizeof(uint32_t));
	/* index 0 of every table is "none" */
	c->nsym = c->nnode = c->ntag = c->nlabel = c->nattr = c->nstr = 1;
	memset(c->cplx, 0, sizeof(c->cplx));
	c->nlist = 1;
	/* basic types: index == kind */
	for (int k = 0; k < TY_PTR; k++) {
		ctype t = { .kind = (uint8_t)k, .size = basic[k].size, .align = basic[k].align };

		if (k == TY_LDOUBLE && o->target == PP_DARWIN)
			t.size = 8, t.align = 8;
		if (k == TY_LDOUBLE && o->target == PP_WASM64)
			t.align = 8;	/* emscripten: binary128, 8-byte aligned */
		c->ty[k] = t;
		c->ty[k].canon = (uint32_t)k;
		c->nty++;
	}
	for (int k = 0; k < TY_PTR; k++) {	/* make them findable by ty_intern */
		uint32_t h = ty_hash(&c->ty[k]), m = c->tyhcap - 1, i;

		for (i = h & m; c->tyhash[i]; i = (i + 1) & m)
			;
		if (k)
			c->tyhash[i] = (uint32_t)k;
	}
	c->t_size = prim(TY_ULONG);
	c->t_ptrdiff = prim(TY_LONG);
	c->t_wchar = o->short_wchar ? prim(TY_USHORT) : o->target != PP_LINUX_AARCH64 ? prim(TY_INT) : prim(TY_UINT);
	c->t_char16 = prim(TY_USHORT);
	c->t_char32 = prim(TY_UINT);
	c->id_func = vintern_copy("__func__\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 8);
	c->id_FUNCTION = vintern_copy("__FUNCTION__\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 12);
	c->id_PRETTY = vintern_copy("__PRETTY_FUNCTION__\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 19);
	c->id_null[0] = vintern_copy("_Nonnull\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 8);
	c->id_null[1] = vintern_copy("_Nullable\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 9);
	c->id_null[2] = vintern_copy("_Null_unspecified\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 17);
	c->id_null[3] = vintern_copy("_Nullable_result\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16);
	c->depth = 0;
	/* predeclared: __builtin_va_list, __int128_t, __uint128_t */
	{
		uint32_t s;
		qty vl;

		if (o->target == PP_DARWIN) {
			vl = ty_ptr(c, prim(TY_CHAR));
		} else if (o->target == PP_WASM64) {
			vl = ty_ptr(c, prim(TY_VOID));
		} else {
			/* struct __va_list { void *__stack, *__gr_top, *__vr_top; int __gr_offs, __vr_offs; } */
			uint32_t ti = tag_new(c, TY_STRUCT, vintern_copy("__va_list\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 9), 0);
			tag *r = &c->tags[ti];
			qty vp = ty_ptr(c, prim(TY_VOID));

			r->f = halloc(c, 5 * sizeof(field));
			memset(r->f, 0, 5 * sizeof(field));
			r->nfield = 5;
			r->f[0].ty = r->f[1].ty = r->f[2].ty = vp;
			r->f[3].ty = r->f[4].ty = prim(TY_INT);
			lay_record(c, r, NULL, NULL);
			r->flags |= RF_COMPLETE;
			c->valist_tag = ti;
			vl = QT(r->ty, 0);
		}
		c->t_valist = vl;
		s = sym_new(c, S_TYPEDEF, vintern_copy("__builtin_va_list\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 17), vl, 0);
		sym_bind(c, s);
		s = sym_new(c, S_TYPEDEF, vintern_copy("__int128_t\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 10), prim(TY_INT128), 0);
		sym_bind(c, s);
		s = sym_new(c, S_TYPEDEF, vintern_copy("__uint128_t\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 11), prim(TY_UINT128), 0);
		sym_bind(c, s);
	}
}

/* the tables back to the thread, zeroed up to what the TU used (+ one element) */
static void cc_free(cc *c)
{
#define USED(n, a) (((size_t)(n) + 2) * sizeof(*(a)))
	vmreg_put(&ccr[0], USED(c->nty, c->ty));
	vmreg_put(&ccr[1], USED(c->nparams, c->params));
	vmreg_put(&ccr[2], USED(c->ntag, c->tags));
	vmreg_put(&ccr[3], USED(c->nsym, c->syms));
	vmreg_put(&ccr[4], USED(c->nnode, c->nodes));
	vmreg_put(&ccr[5], USED(c->nlist, c->lists));
	vmreg_put(&ccr[6], USED(c->nipool, c->ipool));
	vmreg_put(&ccr[7], USED(c->nfpool, c->fpool));
	vmreg_put(&ccr[8], USED(c->nstr, c->strs));
	vmreg_put(&ccr[9], USED(c->nlabel, c->labels));
	vmreg_put(&ccr[10], USED(c->nattr, c->attr));
	vmreg_put(&ccr[11], USED(c->nasm, c->asms));
	vmreg_put(&ccr[12], VMREG_SCRATCH);	/* a stack: written before read */
	vmreg_put(&ccr[13], USED(c->ntoplevel, c->toplevel));
	vmreg_put(&ccr[14], USED(c->ntlasm, c->tlasm));
	vmreg_put(&ccr[15], (size_t)(c->hp - c->heap) + 64);
	vmreg_put(&ccr[16], ((size_t)c->nty + 2) * 8 * sizeof(*c->ptrc));	/* indexed by qty */
	vmreg_put(&ccr[17], USED(c->nnode, c->span));	/* indexed by node */
	vmreg_put(&ccr[18], USED(c->nlzt, c->lzt));
	vmreg_put(&ccr[19], USED(c->nlzf, c->lzf));
	vmreg_put(&ccr[20], (size_t)c->nlzs + 64);
	free(c->cases);
	free(c->pweak);
#undef USED
	vmpool_give(&ccpool, ccr);
	ccr = NULL;
}

static void init_tables(void)
{
	init_keywords();
	init_builtins();
	init_attrs();
}

int cc_run(pp *p, const cc_opts *o)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	cc *c = malloc(sizeof(cc));
	int errors;

	pthread_once(&once, init_tables);	/* vccd runs TUs on many threads */
	kwmap = o->std == 23 ? kwmap_c23 : kwmap_gnu;	/* the TU's keywords (this thread parses it) */
	cc_init(c, p, o);
	if (!setjmp(c->bail)) {
		dc_begin(c);
		fetch(c, &c->tk[0]);
		c->ntk = 1;
		translation_unit(c);
		lazy_closure(c);
		if (unlikely(c->npweak))
			weak_finish(c);
		/* clang: a tentative definition of a never completed struct/union/enum */
		for (uint32_t i = 1; i < c->nsym; i++) {
			const sym *y = &c->syms[i];

			if ((y->flags & (SF_TENTATIVE | SF_DEFINED)) == SF_TENTATIVE && y->kind == S_VAR &&
			    (is_record(c, y->ty) || KIND(c, y->ty) == TY_ENUM) && !complete(c, y->ty) && lookup(c, y->name) == i) {
				cc_error(c, y->loc, "tentative definition has type '%s' that is never completed", tyname(c, y->ty));
				note(c, c->tags[T(c, y->ty)->aux].loc, "forward declaration of '%s'", tyname(c, y->ty));
			}
		}
	}
	if (o->dump_layouts && !c->errors)
		layout_checks(c);
	if (o->emit && !c->errors && !pp_errors(p))
		c->errors += emit_tu(c, o);
	errors = c->errors;
	dc_end(c);
	cc_free(c);
	free(c);
	return errors;
}
