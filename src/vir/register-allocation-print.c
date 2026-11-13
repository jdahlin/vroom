/*
 * The allocation as text (PLAN-vir.md V4): the function as the printer writes
 * it, each instruction followed by `; operand registers -> result register`,
 * edits as `; reload r1 <- v3` lines before the instruction they precede,
 * block parameters' locations on the block's line, then the edges' moves,
 * spill slots, callee-saved registers and the counters.  Spill slots are
 * `ss0`, `ss1`, ...; `-` is no register (an immediate or memory operand).
 */
#include "vir-internal.h"
#include "vir/register-allocation.h"

void vir_alloc_register_name(char *o, uint32_t r)
{
	if (r == VIR_REGISTER_NONE)
		strcpy(o, "-");
	else if (r == VIR_REGISTER_SCRATCH0 || r == VIR_REGISTER_SCRATCH1)
		snprintf(o, 8, "s%u", r == VIR_REGISTER_SCRATCH1);
	else if (r < 128)
		snprintf(o, 8, "%c%u", VIR_REGISTER_CLASS(r) ? 'f' : 'r', VIR_REGISTER_NUMBER(r));
	else
		snprintf(o, 8, "?%u", r);
}

typedef struct vir_alloc_printer {
	vbuffer *buffer;
	const vir_unit *unit;
	vir_function *function;
	const vir_alloc_out *out;
	uint32_t *names;
} vir_alloc_printer;

static void vir_alloc_print_value_name(const vir_alloc_printer *d, char *o, size_t n, uint32_t v)
{
	const vir_insn *in = &d->function->insns[v];

	if (v >= d->function->constant_count) {
		snprintf(o, n, "v%u", d->names[v]);
		return;
	}
	switch (in->op) {
	case VIR_OP_ICONST: {
		uint64_t x = (uint64_t)in->c << 32 | in->b;

		snprintf(o, n, "%lld", in->type == VIR_TYPE_I32 ? (long long)(int32_t)x : (long long)x);
		break;
	}
	case VIR_OP_FCONST:
		snprintf(o, n, "bits:0x%llx", (unsigned long long)((uint64_t)in->c << 32 | in->b));
		break;
	case VIR_OP_SYMADDR:
		snprintf(o, n, "@%s", d->unit && in->a < d->unit->symbol_count ? d->unit->symbols[in->a].name : "?");
		break;
	case VIR_OP_SLOTADDR:
		snprintf(o, n, "$%u", in->a);
		break;
	case VIR_OP_LABELADDR:
		snprintf(o, n, "&b%u", in->a);
		break;
	default:
		snprintf(o, n, "?");
	}
}

static void vir_alloc_print_location_name(char *o, size_t n, uint32_t l)
{
	if (l & VIR_LOCATION_SLOT)
		snprintf(o, n, "ss%u", l & ~VIR_LOCATION_SLOT);
	else
		vir_alloc_register_name(o, l);
}

static void vir_alloc_print_column_pad(vbuffer *b, size_t start, size_t col)
{
	size_t w = b->n - start;

	if (w < col)
		vbuffer_append(b, "                                                            ", col - w < 60 ? col - w : 60);
	else
		vbuffer_string_append(b, " ");
}

void vir_alloc_print(vbuffer *b, const vir_unit *u, vir_function *f, const vir_loops *lp, const vir_alloc_out *o)
{
	vbuffer t = { 0 };
	vir_alloc_printer d = { b, u, f, o, NULL };
	uint32_t blk = VIR_BLOCK_NONE, ins = 0, pos = 0, e = 0, nv = 0;
	char s1[48], s2[48], s3[48];

	d.names = malloc((size_t)f->insn_count * 4 + 4);
	if (!d.names)
		abort();
	for (uint32_t k = 0; k < f->block_count; k++)
		for (uint32_t i = f->blocks[k].first; i < f->blocks[k].first + f->blocks[k].insn_count; i++)
			if (f->insns[i].type != VIR_TYPE_VOID)
				d.names[i] = nv++;
	vir_print_function(&t, u, f);
	for (size_t i = 0; i < t.n;) {
		size_t j = i, ls;

		while (j < t.n && t.p[j] != '\n')
			j++;
		ls = b->n;
		vbuffer_append(b, t.p + i, j - i);
		if (j - i >= 2 && t.p[i] == 'b' && t.p[j - 1] == ':') {
			const vir_block *k;

			blk = blk == VIR_BLOCK_NONE ? 0 : blk + 1;
			k = &f->blocks[blk];
			ins = k->first + k->parameter_count;
			pos = o->block_offsets[blk];
			e = o->block_offsets[f->block_count + 1 + blk];
			if (lp && lp->inner[blk] != VIR_BLOCK_NONE && lp->loops[lp->inner[blk]].head == blk) {
				vir_alloc_print_column_pad(b, ls, 40);
				vbuffer_format_append(b, "; loop to b%u depth %u", lp->loops[lp->inner[blk]].end, lp->loops[lp->inner[blk]].depth);
			}
			for (uint32_t q = 0; q < k->parameter_count; q++) {
				uint8_t r = o->operand_registers[pos++];
				uint32_t v = k->first + q;

				if (!q && !(lp && lp->inner[blk] != VIR_BLOCK_NONE && lp->loops[lp->inner[blk]].head == blk))
					vir_alloc_print_column_pad(b, ls, 40), vbuffer_string_append(b, ";");
				else if (!q)
					vbuffer_string_append(b, ";");
				vir_alloc_print_value_name(&d, s1, sizeof(s1), v);
				if (r != VIR_REGISTER_NONE)
					vir_alloc_register_name(s2, r);
				else if (o->slot[v] != VIR_BLOCK_NONE)
					snprintf(s2, sizeof(s2), "ss%u", o->slot[v]);
				else
					strcpy(s2, "-");
				vbuffer_format_append(b, "%s %s %s", q ? "," : "", s1, s2);
			}
		} else if (blk != VIR_BLOCK_NONE && j - i > 2 && t.p[i] == ' ' && ins < f->blocks[blk].first + f->blocks[blk].insn_count) {
			const vir_insn *in = &f->insns[ins];
			vir_insn x = *in;
			vir_operand_iterator it;
			uint32_t *p, n = 0;
			size_t mark = b->n, back = ls;

			/* the edits go before the line: move the line after them */
			{
				vbuffer ed = { 0 };

				for (; e < o->block_offsets[f->block_count + 2 + blk] && o->edit[e].pos == ins; e++) {
					const vir_edit *x2 = &o->edit[e];

					vir_alloc_register_name(s1, x2->dst);
					vir_alloc_print_value_name(&d, s2, sizeof(s2), x2->value);
					switch (x2->kind) {
					case VIR_EDIT_RELOAD:
						vbuffer_format_append(&ed, "  ; reload %s <- %s (ss%u)\n", s1, s2, o->slot[x2->value]);
						break;
					case VIR_EDIT_REMATERIALIZE:
						vbuffer_format_append(&ed, "  ; remat %s <- %s\n", s1, s2);
						break;
					case VIR_EDIT_MOVE:
						vir_alloc_register_name(s3, x2->src);
						vbuffer_format_append(&ed, "  ; move %s <- %s (%s)\n", s1, s3, s2);
						break;
					case VIR_EDIT_SWAP:
						vbuffer_string_append(&ed, "  ; swap operands\n");
						break;
					case VIR_EDIT_STORE:
						vir_alloc_register_name(s3, x2->src);
						vbuffer_format_append(&ed, "  ; store ss%u <- %s (%s)\n", o->slot[x2->value], s3, s2);
						break;
					}
				}
				if (ed.n) {
					size_t ln = mark - back;
					char *line = malloc(ln + 1);

					memcpy(line, b->p + back, ln);
					b->n = back;
					vbuffer_append(b, ed.p, ed.n);
					ls = b->n;
					vbuffer_append(b, line, ln);
					free(line);
					free(ed.p);
				}
			}
			vir_alloc_print_column_pad(b, ls, 40);
			vbuffer_string_append(b, ";");
			vir_operand_iterator_init(&it, f, &x, false);
			while ((p = vir_operand_iterator_next(&it))) {
				if (f->insns[*p].type == VIR_TYPE_MULTI)
					continue;
				vir_alloc_register_name(s1, o->operand_registers[pos++]);
				vbuffer_format_append(b, "%s %s", n++ ? "," : "", s1);
			}
			if (in->type != VIR_TYPE_VOID && in->type != VIR_TYPE_MULTI) {
				uint8_t r = o->operand_registers[pos++];

				vir_alloc_register_name(s1, r);
				vbuffer_format_append(b, " -> %s", s1);
				if (o->slot[ins] != VIR_BLOCK_NONE)
					vbuffer_format_append(b, o->spill_defined[ins] ? " [ss%u]" : " [ss%u later]", o->slot[ins]);
			}
			ins++;
		}
		vbuffer_string_append(b, "\n");
		i = j + 1;
	}
	for (uint32_t x = 0; x < o->edge_count; x++) {
		const vir_alloc_edge *g = &o->edge[x];

		vbuffer_format_append(b, "; edge b%u.%u -> b%u:", g->from, g->successor_index, g->to);
		for (uint32_t m = 0; m < g->count; m++) {
			const vir_edge_move *mv = &o->move[g->first + m];

			vir_alloc_print_location_name(s1, sizeof(s1), mv->dst);
			vir_alloc_print_value_name(&d, s3, sizeof(s3), mv->value);
			if (mv->kind == VIR_EDIT_REMATERIALIZE) {
				vbuffer_format_append(b, "%s %s <- %s", m ? ";" : "", s1, s3);
			} else {
				vir_alloc_print_location_name(s2, sizeof(s2), mv->src);
				vbuffer_format_append(b, "%s %s %s %s (%s)", m ? ";" : "", s1, mv->kind == VIR_EDIT_EXCHANGE ? "<->" : "<-", s2, s3);
			}
		}
		vbuffer_string_append(b, "\n");
	}
	if (o->spill_count) {
		vbuffer_string_append(b, "; spill slots:");
		for (uint32_t s = 0; s < o->spill_count; s++) {
			vbuffer_format_append(b, " ss%u(%u):", s, o->slot_sizes[s]);
			for (uint32_t v = f->constant_count; v < f->insn_count; v++)
				if (f->insns[v].type != VIR_TYPE_VOID && f->insns[v].type != VIR_TYPE_MULTI && o->slot[v] == s)
					vbuffer_format_append(b, " v%u", d.names[v]);
		}
		vbuffer_string_append(b, "\n");
	}
	if (o->callee[0] | o->callee[1]) {
		vbuffer_string_append(b, "; callee-saved:");
		for (uint32_t r = 0; r < 128; r++)
			if (o->callee[VIR_REGISTER_CLASS(r)] >> VIR_REGISTER_NUMBER(r) & 1) {
				vir_alloc_register_name(s1, r);
				vbuffer_format_append(b, " %s (b%u)", s1, o->callee_first[r]);
			}
		vbuffer_string_append(b, "\n");
	}
	{
		const vir_alloc_statistics *s = &o->statistics;

		vbuffer_format_append(b, "; spills %u reloads %u remats %u moves %u swaps %u stores %u; edges %u: moves %u reloads %u remats %u stores %u xchg %u; slots %u callee %u%s\n",
		       s->spills, s->reloads, s->remats, s->moves, s->swaps, s->stores, s->edges, s->edge_moves, s->edge_reloads, s->edge_rematerializations, s->edge_stores,
		       s->exchanges, s->slots, s->callee, s->linear ? " linear" : "");
	}
	free(t.p);
	free(d.names);
}
