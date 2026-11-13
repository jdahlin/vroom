/*
 * The allocation as text (PLAN-vir.md V4): the function as the printer writes
 * it, each instruction followed by `; operand registers -> result register`,
 * edits as `; reload r1 <- v3` lines before the instruction they precede,
 * block parameters' locations on the block's line, then the edges' moves,
 * spill slots, callee-saved registers and the counters.  Spill slots are
 * `ss0`, `ss1`, ...; `-` is no register (an immediate or memory operand).
 */
#include "virimpl.h"
#include "alloc.h"

void vir_reg_name(char *o, uint32_t r)
{
	if (r == VR_NONE)
		strcpy(o, "-");
	else if (r == VR_SCR0 || r == VR_SCR1)
		snprintf(o, 8, "s%u", r == VR_SCR1);
	else if (r < 128)
		snprintf(o, 8, "%c%u", VR_CLASS(r) ? 'f' : 'r', VR_NUM(r));
	else
		snprintf(o, 8, "?%u", r);
}

typedef struct dctx {
	vbuf *b;
	const vir_unit *u;
	vir_fn *f;
	const vir_alloc_out *o;
	uint32_t *nm;
} dctx;

static void vn(const dctx *d, char *o, size_t n, uint32_t v)
{
	const vir_ins *in = &d->f->ins[v];

	if (v >= d->f->nconst) {
		snprintf(o, n, "v%u", d->nm[v]);
		return;
	}
	switch (in->op) {
	case VO_ICONST: {
		uint64_t x = (uint64_t)in->c << 32 | in->b;

		snprintf(o, n, "%lld", in->ty == VT_I32 ? (long long)(int32_t)x : (long long)x);
		break;
	}
	case VO_FCONST:
		snprintf(o, n, "bits:0x%llx", (unsigned long long)((uint64_t)in->c << 32 | in->b));
		break;
	case VO_SYMADDR:
		snprintf(o, n, "@%s", d->u && in->a < d->u->nsym ? d->u->sym[in->a].name : "?");
		break;
	case VO_SLOTADDR:
		snprintf(o, n, "$%u", in->a);
		break;
	case VO_LABELADDR:
		snprintf(o, n, "&b%u", in->a);
		break;
	default:
		snprintf(o, n, "?");
	}
}

static void loc(char *o, size_t n, uint32_t l)
{
	if (l & VL_SLOT)
		snprintf(o, n, "ss%u", l & ~VL_SLOT);
	else
		vir_reg_name(o, l);
}

static void pad(vbuf *b, size_t start, size_t col)
{
	size_t w = b->n - start;

	if (w < col)
		vbuf_put(b, "                                                            ", col - w < 60 ? col - w : 60);
	else
		vbuf_s(b, " ");
}

void vir_alloc_print(vbuf *b, const vir_unit *u, vir_fn *f, const vir_loops *lp, const vir_alloc_out *o)
{
	vbuf t = { 0 };
	dctx d = { b, u, f, o, NULL };
	uint32_t blk = VIR_NOBLK, ins = 0, pos = 0, e = 0, nv = 0;
	char s1[48], s2[48], s3[48];

	d.nm = malloc((size_t)f->nins * 4 + 4);
	if (!d.nm)
		abort();
	for (uint32_t k = 0; k < f->nblk; k++)
		for (uint32_t i = f->blk[k].first; i < f->blk[k].first + f->blk[k].n; i++)
			if (f->ins[i].ty != VT_VOID)
				d.nm[i] = nv++;
	vir_print_fn(&t, u, f);
	for (size_t i = 0; i < t.n;) {
		size_t j = i, ls;

		while (j < t.n && t.p[j] != '\n')
			j++;
		ls = b->n;
		vbuf_put(b, t.p + i, j - i);
		if (j - i >= 2 && t.p[i] == 'b' && t.p[j - 1] == ':') {
			const vir_blk *k;

			blk = blk == VIR_NOBLK ? 0 : blk + 1;
			k = &f->blk[blk];
			ins = k->first + k->nparam;
			pos = o->boff[blk];
			e = o->boff[f->nblk + 1 + blk];
			if (lp && lp->inner[blk] != VIR_NOBLK && lp->l[lp->inner[blk]].head == blk) {
				pad(b, ls, 40);
				vbuf_f(b, "; loop to b%u depth %u", lp->l[lp->inner[blk]].end, lp->l[lp->inner[blk]].depth);
			}
			for (uint32_t q = 0; q < k->nparam; q++) {
				uint8_t r = o->opr[pos++];
				uint32_t v = k->first + q;

				if (!q && !(lp && lp->inner[blk] != VIR_NOBLK && lp->l[lp->inner[blk]].head == blk))
					pad(b, ls, 40), vbuf_s(b, ";");
				else if (!q)
					vbuf_s(b, ";");
				vn(&d, s1, sizeof(s1), v);
				if (r != VR_NONE)
					vir_reg_name(s2, r);
				else if (o->slot[v] != VIR_NOBLK)
					snprintf(s2, sizeof(s2), "ss%u", o->slot[v]);
				else
					strcpy(s2, "-");
				vbuf_f(b, "%s %s %s", q ? "," : "", s1, s2);
			}
		} else if (blk != VIR_NOBLK && j - i > 2 && t.p[i] == ' ' && ins < f->blk[blk].first + f->blk[blk].n) {
			const vir_ins *in = &f->ins[ins];
			vir_ins x = *in;
			vir_oit it;
			uint32_t *p, n = 0;
			size_t mark = b->n, back = ls;

			/* the edits go before the line: move the line after them */
			{
				vbuf ed = { 0 };

				for (; e < o->boff[f->nblk + 2 + blk] && o->edit[e].pos == ins; e++) {
					const vir_edit *x2 = &o->edit[e];

					vir_reg_name(s1, x2->dst);
					vn(&d, s2, sizeof(s2), x2->v);
					switch (x2->kind) {
					case VE_RELOAD:
						vbuf_f(&ed, "  ; reload %s <- %s (ss%u)\n", s1, s2, o->slot[x2->v]);
						break;
					case VE_REMAT:
						vbuf_f(&ed, "  ; remat %s <- %s\n", s1, s2);
						break;
					case VE_MOVE:
						vir_reg_name(s3, x2->src);
						vbuf_f(&ed, "  ; move %s <- %s (%s)\n", s1, s3, s2);
						break;
					case VE_SWAP:
						vbuf_s(&ed, "  ; swap operands\n");
						break;
					case VE_STORE:
						vir_reg_name(s3, x2->src);
						vbuf_f(&ed, "  ; store ss%u <- %s (%s)\n", o->slot[x2->v], s3, s2);
						break;
					}
				}
				if (ed.n) {
					size_t ln = mark - back;
					char *line = malloc(ln + 1);

					memcpy(line, b->p + back, ln);
					b->n = back;
					vbuf_put(b, ed.p, ed.n);
					ls = b->n;
					vbuf_put(b, line, ln);
					free(line);
					free(ed.p);
				}
			}
			pad(b, ls, 40);
			vbuf_s(b, ";");
			vir_oit_init(&it, f, &x, false);
			while ((p = vir_oit_next(&it))) {
				if (f->ins[*p].ty == VT_MULTI)
					continue;
				vir_reg_name(s1, o->opr[pos++]);
				vbuf_f(b, "%s %s", n++ ? "," : "", s1);
			}
			if (in->ty != VT_VOID && in->ty != VT_MULTI) {
				uint8_t r = o->opr[pos++];

				vir_reg_name(s1, r);
				vbuf_f(b, " -> %s", s1);
				if (o->slot[ins] != VIR_NOBLK)
					vbuf_f(b, o->sdef[ins] ? " [ss%u]" : " [ss%u later]", o->slot[ins]);
			}
			ins++;
		}
		vbuf_s(b, "\n");
		i = j + 1;
	}
	for (uint32_t x = 0; x < o->nedge; x++) {
		const vir_aedge *g = &o->edge[x];

		vbuf_f(b, "; edge b%u.%u -> b%u:", g->from, g->k, g->to);
		for (uint32_t m = 0; m < g->n; m++) {
			const vir_emove *mv = &o->move[g->first + m];

			loc(s1, sizeof(s1), mv->dst);
			vn(&d, s3, sizeof(s3), mv->v);
			if (mv->kind == VE_REMAT) {
				vbuf_f(b, "%s %s <- %s", m ? ";" : "", s1, s3);
			} else {
				loc(s2, sizeof(s2), mv->src);
				vbuf_f(b, "%s %s %s %s (%s)", m ? ";" : "", s1, mv->kind == VE_XCHG ? "<->" : "<-", s2, s3);
			}
		}
		vbuf_s(b, "\n");
	}
	if (o->nspill) {
		vbuf_s(b, "; spill slots:");
		for (uint32_t s = 0; s < o->nspill; s++) {
			vbuf_f(b, " ss%u(%u):", s, o->ssz[s]);
			for (uint32_t v = f->nconst; v < f->nins; v++)
				if (f->ins[v].ty != VT_VOID && f->ins[v].ty != VT_MULTI && o->slot[v] == s)
					vbuf_f(b, " v%u", d.nm[v]);
		}
		vbuf_s(b, "\n");
	}
	if (o->callee[0] | o->callee[1]) {
		vbuf_s(b, "; callee-saved:");
		for (uint32_t r = 0; r < 128; r++)
			if (o->callee[VR_CLASS(r)] >> VR_NUM(r) & 1) {
				vir_reg_name(s1, r);
				vbuf_f(b, " %s (b%u)", s1, o->cfirst[r]);
			}
		vbuf_s(b, "\n");
	}
	{
		const vir_astats *s = &o->st;

		vbuf_f(b, "; spills %u reloads %u remats %u moves %u swaps %u stores %u; edges %u: moves %u reloads %u remats %u stores %u xchg %u; slots %u callee %u%s\n",
		       s->spills, s->reloads, s->remats, s->moves, s->swaps, s->stores, s->edges, s->emoves, s->ereloads, s->eremats, s->estores,
		       s->xchg, s->slots, s->callee, s->linear ? " linear" : "");
	}
	free(t.p);
	free(d.nm);
}
