/* vcc preprocessor: header cache: recording what a header reads and writes */
#include "ppimpl.h"

VPRIV uint64_t *rslot(pp *p, uint32_t id)
{
	uint64_t **pg = &p->rst[id >> MPAGE_BITS];

	if (unlikely(!*pg)) {
		*pg = varena_alloc(&p->perm, MPAGE * sizeof(uint64_t));
		memset(*pg, 0, MPAGE * sizeof(uint64_t));
	}
	return &(*pg)[id & (MPAGE - 1)];
}

/* a read of id (entry e) while recording; def: only definedness matters */
VPRIV __attribute__((noinline)) void rec_read(pp *p, uint32_t id, uintptr_t e, int def)
{
	e &= ~(uintptr_t)1;
	touch_read(p, id, def ? (e ? HDEF : 0) : e, def == 2 ? HT_G : 0);
}

VPRIV uint16_t new_cprov(pp *p, uint32_t val, uint32_t off, uint32_t len, uint16_t next)
{
	if (p->rec.ncp >= HC_MAXCP || len > 0xffff) {
		HC_BAD(p);
		return 0;
	}
	p->rec.cp[++p->rec.ncp] = (cprov){ val, off, (uint16_t)len, next };
	return (uint16_t)p->rec.ncp;
}

/* chain a shifted by d, followed by chain tail (shared) */
VPRIV uint16_t cp_chain(pp *p, uint16_t a, uint32_t d, uint16_t tail)
{
	if (!a)
		return tail;
	{
		const cprov c = p->rec.cp[a];
		uint16_t n = cp_chain(p, c.next, d, tail);

		return new_cprov(p, c.val, c.off + d, c.len, n);
	}
}

/* a read of an identifier with __COUNTER__ digits: at replay its name differs */
VPRIV __attribute__((noinline)) void rec_ctr_read(pp *p, const ptok *t, uintptr_t e)
{
	const cprov *c = &p->rec.cp[t->ctr];
	char *d;

	if (e || c->next || p->rec.ncrd >= HC_MAXCRD || t->len > 4096) {
		HC_BAD(p);	/* such a name defined as a macro, or two runs: give up */
		return;
	}
	d = varena_alloc(&p->perm, t->len + 1);
	memcpy(d, t->s, t->len);	/* pasted: the spelling is clean */
	p->rec.crd[p->rec.ncrd++] = (hcrd){ d, t->len, c->off, c->len, c->val };
}

/* #define / #undef / pop_macro */
VPRIV void mdef(pp *p, uint32_t id, const pmacro *m)
{
	if (unlikely(p->rec.on))
		touch_write(p, id);
	mset(p, id, m);
}
