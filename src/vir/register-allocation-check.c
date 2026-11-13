/*
 * vir_alloc_check (SPEC-vir.md 15.11): the allocation, checked by a forward
 * symbolic walk that knows nothing of how it was made.
 *
 * A location is a register byte (0-255: the two classes and the scratch
 * registers) or a spill slot (256 + slot).  The state maps each location to
 * the value it holds, 0 for none.  Effects, in the order lowering performs
 * them (15.3): a bparam with a register and a slot is stored at the top of
 * its block; the edits at an instruction; the operands are read (each must be
 * in its register, or for an operand the target lets be memory or an
 * immediate, a constant or in its slot); the clobbers; the result written to
 * its register and, when the value has a slot, stored there.  An edge applies
 * its moves in order, then each block parameter's location must hold its
 * argument and is renamed to the parameter.  Block entry states meet by
 * intersection (a location keeps a value only if every incoming edge agrees)
 * and are iterated to a fixed point; the checks run once more on the final
 * states, so a failure means some path reads a wrong value.
 *
 * Also checked: fixed operand and result registers, early clobbers, the
 * two-address rule (a result in a register other than the first operand's must
 * not be another operand's, since lowering copies the first operand there
 * first), registers are allocatable, and every callee-saved register written
 * is in the output's set.  Not a fast path: it allocates with calloc.
 */
#include "vir-internal.h"
#include "vir/register-allocation.h"

typedef struct vir_alloc_checker {
	vir_function *function;
	const vir_target_registers *registers;
	const vir_alloc_out *out;
	vbuffer *message;
	uint32_t location_count, error_count, *names;
	uint64_t written_count[2];
	bool in_worklist;		/* report (the last round) */
} vir_alloc_checker;

#define VIR_ALLOC_CHECK_SLOT_LOCATION(s) (256u + (s))

static void vir_alloc_check_value_name(const vir_alloc_checker *c, char *o, uint32_t v)
{
	const vir_insn *in = &c->function->insns[v];

	if (v < c->function->constant_count) {
		if (in->op == VIR_OP_ICONST || in->op == VIR_OP_FCONST)
			snprintf(o, 40, "%s 0x%llx", vir_opcode_table[in->op].name, (unsigned long long)((uint64_t)in->c << 32 | in->b));
		else
			snprintf(o, 40, "%s(%u)", vir_opcode_table[in->op].name, in->a);
	} else if (c->names[v] != VIR_BLOCK_NONE) {
		snprintf(o, 40, "v%u", c->names[v]);
	} else {
		snprintf(o, 40, "#%u", v);
	}
}

static void vir_alloc_check_location_name(char *o, uint32_t l)
{
	if (l >= 256)
		snprintf(o, 16, "ss%u", l - 256);
	else
		vir_alloc_register_name(o, l);
}

__attribute__((format(printf, 4, 5)))
static void vir_alloc_check_failure_add(vir_alloc_checker *c, uint32_t b, uint32_t i, const char *fmt, ...)
{
	char t[300];
	va_list ap;

	if (!c->in_worklist)
		return;
	if (c->error_count++ >= 10)
		return;
	va_start(ap, fmt);
	vsnprintf(t, sizeof(t), fmt, ap);
	va_end(ap);
	if (i)
		vbuffer_format_append(c->message, "alloc-check: b%u ins %u: %s\n", b, i, t);
	else
		vbuffer_format_append(c->message, "alloc-check: b%u: %s\n", b, t);
}

static bool vir_alloc_check_register_is_valid(const vir_alloc_checker *c, uint32_t r, bool scratch)
{
	if (r == VIR_REGISTER_SCRATCH0 || r == VIR_REGISTER_SCRATCH1)
		return scratch;
	return r < 128 && (c->registers->alloc[VIR_REGISTER_CLASS(r)] >> VIR_REGISTER_NUMBER(r) & 1);
}

static void vir_alloc_check_register_written_mark(vir_alloc_checker *c, uint32_t r)
{
	if (r < 128)
		c->written_count[VIR_REGISTER_CLASS(r)] |= 1ull << VIR_REGISTER_NUMBER(r);
}

/* the location an operand of the edge or block-top logic means for value v in register byte r */
static uint32_t vir_alloc_check_parameter_location(const vir_alloc_checker *c, uint32_t q, uint8_t r)
{
	if (r != VIR_REGISTER_NONE)
		return r;
	return c->out->slot[q] == VIR_BLOCK_NONE ? VIR_BLOCK_NONE : VIR_ALLOC_CHECK_SLOT_LOCATION(c->out->slot[q]);
}

static bool vir_alloc_check_move_apply(vir_alloc_checker *c, uint32_t *st, const vir_edge_move *m, uint32_t b)
{
	uint32_t d = m->dst, s = m->src;
	char t[40];

	d = d & VIR_LOCATION_SLOT ? VIR_ALLOC_CHECK_SLOT_LOCATION(d & ~VIR_LOCATION_SLOT) : d;
	s = s & VIR_LOCATION_SLOT ? VIR_ALLOC_CHECK_SLOT_LOCATION(s & ~VIR_LOCATION_SLOT) : s;
	if (d >= c->location_count || (m->kind != VIR_EDIT_REMATERIALIZE && s >= c->location_count) || (d < 256 && !vir_alloc_check_register_is_valid(c, d, true))) {
		vir_alloc_check_failure_add(c, b, 0, "edge move with a bad location");
		return false;
	}
	vir_alloc_check_register_written_mark(c, d);
	switch (m->kind) {
	case VIR_EDIT_MOVE:
		st[d] = st[s];
		break;
	case VIR_EDIT_REMATERIALIZE:
		if (m->value >= c->function->constant_count || !m->value) {
			vir_alloc_check_value_name(c, t, m->value);
			vir_alloc_check_failure_add(c, b, 0, "edge rematerializes %s, not a constant", t);
		}
		st[d] = m->value;
		break;
	case VIR_EDIT_EXCHANGE: {
		uint32_t x = st[d];

		vir_alloc_check_register_written_mark(c, s);
		st[d] = st[s];
		st[s] = x;
		break;
	}
	default:
		vir_alloc_check_failure_add(c, b, 0, "edge move of kind %u", m->kind);
		return false;
	}
	return true;
}

/* one block from its entry state (in st, changed); calls meet for each edge */
static void vir_alloc_check_block(vir_alloc_checker *c, uint32_t b, uint32_t *st, uint32_t *tmp, uint32_t **ent, bool *seen, uint32_t *wl, uint32_t *nwl,
		  bool *inwl)
{
	vir_function *f = c->function;
	const vir_alloc_out *o = c->out;
	const vir_block *k = &f->blocks[b];
	uint32_t pos = o->block_offsets[b], e = o->block_offsets[f->block_count + 1 + b], eend = o->block_offsets[f->block_count + 2 + b];
	uint32_t ops[512];
	char t1[40], t2[40], t3[16];

	for (uint32_t j = 0; j < k->parameter_count; j++) {
		uint32_t q = k->first + j;
		uint8_t r = o->operand_registers[pos++];

		if (r != VIR_REGISTER_NONE && o->slot[q] != VIR_BLOCK_NONE && o->spill_defined[q])
			st[VIR_ALLOC_CHECK_SLOT_LOCATION(o->slot[q])] = st[r] == q ? q : 0;	/* the store at the top */
	}
	for (uint32_t i = k->first + k->parameter_count; i < k->first + k->insn_count; i++) {
		vir_insn in = f->insns[i];
		vir_register_constraint fx;
		vir_operand_iterator it;
		uint32_t *p, n = 0;
		bool swapped = false;
		uint8_t rr[512];

		for (; e < eend && o->edit[e].pos == i; e++) {
			const vir_edit *d = &o->edit[e];

			if (d->kind == VIR_EDIT_STORE) {
				if (d->value >= f->insn_count || o->slot[d->value] == VIR_BLOCK_NONE) {
					vir_alloc_check_value_name(c, t1, d->value);
					vir_alloc_check_failure_add(c, b, i, "store of %s, which has no slot", t1);
				} else if (!vir_alloc_check_register_is_valid(c, d->src, false)) {
					vir_alloc_check_failure_add(c, b, i, "store from register byte %u", d->src);
				} else {
					/* on a path where v is not defined (15.5: liveness over loop ranges
					 * keeps it live there) the register holds something else: the slot
					 * then holds nothing, which only a read can get wrong */
					st[VIR_ALLOC_CHECK_SLOT_LOCATION(o->slot[d->value])] = st[d->src] == d->value ? d->value : 0;
				}
				continue;
			}
			if (d->kind != VIR_EDIT_SWAP && !vir_alloc_check_register_is_valid(c, d->dst, false)) {
				vir_alloc_check_failure_add(c, b, i, "edit writes register byte %u", d->dst);
				continue;
			}
			switch (d->kind) {
			case VIR_EDIT_RELOAD:
				if (d->value >= f->insn_count || o->slot[d->value] == VIR_BLOCK_NONE) {
					vir_alloc_check_value_name(c, t1, d->value);
					vir_alloc_check_failure_add(c, b, i, "reload of %s, which has no slot", t1);
					st[d->dst] = 0;
				} else {
					st[d->dst] = st[VIR_ALLOC_CHECK_SLOT_LOCATION(o->slot[d->value])];
				}
				vir_alloc_check_register_written_mark(c, d->dst);
				break;
			case VIR_EDIT_REMATERIALIZE:
				if (!d->value || d->value >= f->constant_count)
					vir_alloc_check_failure_add(c, b, i, "rematerializes a non-constant");
				st[d->dst] = d->value;
				vir_alloc_check_register_written_mark(c, d->dst);
				break;
			case VIR_EDIT_MOVE:
				if (!vir_alloc_check_register_is_valid(c, d->src, false))
					vir_alloc_check_failure_add(c, b, i, "move from register byte %u", d->src);
				else
					st[d->dst] = st[d->src];
				vir_alloc_check_register_written_mark(c, d->dst);
				break;
			case VIR_EDIT_SWAP:
				swapped = true;
				break;
			default:
				vir_alloc_check_failure_add(c, b, i, "edit of kind %u", d->kind);
			}
		}
		vir_fix_clear(&fx);
		if (!c->registers->operand_fixes || c->registers->operand_fixes[in.op])
			c->registers->fixed(f, i, &fx, c->registers->context);
		vir_operand_iterator_init(&it, f, &in, false);
		while ((p = vir_operand_iterator_next(&it)) && n < 512) {
			if (f->insns[*p].type == VIR_TYPE_MULTI)
				continue;	/* a call's results are read by its projs, from registers */
			ops[n] = *p;
			rr[n] = o->operand_registers[pos++];
			n++;
		}
		if (in.op != VIR_OP_BR)
			for (uint32_t j = 0; j < n; j++) {
				uint32_t v = ops[j], r = rr[j];
				bool fixd = j < fx.operand_count && fx.operands[j] != VIR_REGISTER_NONE;
				bool mem = (j < 32 && (fx.mem >> j & 1)) || (j >= fx.operand_count && (fx.flags & VIR_CONSTRAINT_FLAG_REST_MEMORY));

				vir_alloc_check_value_name(c, t1, v);
				if (r == VIR_REGISTER_NONE) {
					if (!mem)
						vir_alloc_check_failure_add(c, b, i, "operand %u (%s) has no register", j, t1);
					else if (v >= f->constant_count && (o->slot[v] == VIR_BLOCK_NONE || st[VIR_ALLOC_CHECK_SLOT_LOCATION(o->slot[v])] != v))
						vir_alloc_check_failure_add(c, b, i, "memory operand %u: %s is not in its slot", j, t1);
					continue;
				}
				if (!vir_alloc_check_register_is_valid(c, r, false)) {
					vir_alloc_check_failure_add(c, b, i, "operand %u in register byte %u", j, r);
					continue;
				}
				if (st[r] != v) {
					vir_alloc_register_name(t3, r);
					if (st[r])
						vir_alloc_check_value_name(c, t2, st[r]);
					else
						strcpy(t2, "nothing");
					vir_alloc_check_failure_add(c, b, i, "operand %u: %s holds %s, not %s", j, t3, t2, t1);
				}
				if (fixd && r != fx.operands[j]) {
					vir_alloc_register_name(t3, fx.operands[j]);
					vir_alloc_check_failure_add(c, b, i, "operand %u must be in %s", j, t3);
				}
				if (!fixd && (fx.edge_clobbered[VIR_REGISTER_CLASS(r)] >> VIR_REGISTER_NUMBER(r) & 1))
					vir_alloc_check_failure_add(c, b, i, "operand %u is in an early-clobbered register", j);
			}
		for (int cl = 0; cl < 2; cl++) {
			uint64_t m = fx.clobbered[cl] | fx.edge_clobbered[cl];

			while (m) {
				uint32_t r = VIR_REGISTER_MAKE(cl, __builtin_ctzll(m));

				m &= m - 1;
				st[r] = 0;
				vir_alloc_check_register_written_mark(c, r);
			}
		}
		if (in.type != VIR_TYPE_VOID && in.type != VIR_TYPE_MULTI) {
			uint8_t r = o->operand_registers[pos++];

			if (fx.result != VIR_REGISTER_NONE && r != VIR_REGISTER_NONE && r != fx.result) {
				vir_alloc_register_name(t3, fx.result);
				vir_alloc_check_failure_add(c, b, i, "result must be in %s", t3);
			}
			if (r != VIR_REGISTER_NONE) {
				if (!vir_alloc_check_register_is_valid(c, r, false) || VIR_REGISTER_CLASS(r) != vir_register_class(in.type)) {
					vir_alloc_check_failure_add(c, b, i, "result in register byte %u", r);
				} else {
					if ((fx.flags & VIR_CONSTRAINT_FLAG_TIED) && n >= 1) {
						uint32_t first = swapped && n >= 2 ? 1 : 0;

						if (r != rr[first])
							for (uint32_t j = 0; j < n; j++)
								if (j != first && rr[j] == r)
									vir_alloc_check_failure_add(c, b, i, "two-address result in operand %u's register", j);
					}
					st[r] = i;
					vir_alloc_check_register_written_mark(c, r);
				}
			}
			if (o->slot[i] != VIR_BLOCK_NONE && o->spill_defined[i]) {
				if (r == VIR_REGISTER_NONE)
					vir_alloc_check_failure_add(c, b, i, "a value with a slot is defined without a register");
				st[VIR_ALLOC_CHECK_SLOT_LOCATION(o->slot[i])] = r == VIR_REGISTER_NONE ? 0 : i;
			}
		}
		if (swapped && !((fx.flags & VIR_CONSTRAINT_FLAG_COMMUTATIVE) && n >= 2))
			vir_alloc_check_failure_add(c, b, i, "operands swapped on a non-commutative instruction");
		if (i + 1 == k->first + k->insn_count) {	/* the terminator: the edges */
			vir_operand_iterator bi;
			uint32_t kk = 0;

			vir_operand_iterator_init(&bi, f, &in, true);
			while ((p = vir_operand_iterator_next(&bi))) {
				uint32_t to = *p, ei = o->edge_indices[o->slot_offsets[b] + kk];
				const vir_block *tk = &f->blocks[to];

				memcpy(tmp, st, (size_t)c->location_count * 4);
				if (ei != VIR_BLOCK_NONE)
					for (uint32_t m = 0; m < o->edge[ei].count; m++)
						vir_alloc_check_move_apply(c, tmp, &o->move[o->edge[ei].first + m], b);
				for (uint32_t j = 0; j < tk->parameter_count; j++) {
					uint32_t q = tk->first + j, l = vir_alloc_check_parameter_location(c, q, o->operand_registers[o->block_offsets[to] + j]);
					uint32_t a = in.op == VIR_OP_BR ? f->extra[in.c + j] : 0;

					if (l == VIR_BLOCK_NONE)
						continue;	/* unused parameter: nowhere */
					if (tmp[l] != a) {
						vir_alloc_check_value_name(c, t1, q);
						vir_alloc_check_value_name(c, t2, a);
						vir_alloc_check_location_name(t3, l);
						vir_alloc_check_failure_add(c, b, 0, "edge to b%u: %s (%s) does not hold the argument %s", to, t1, t3, t2);
					}
					tmp[l] = q;
				}
				/* meet */
				{
					uint32_t *en = ent[to];
					bool ch = false;

					if (!seen[to]) {
						memcpy(en, tmp, (size_t)c->location_count * 4);
						seen[to] = ch = true;
					} else {
						for (uint32_t l = 0; l < c->location_count; l++)
							if (en[l] && en[l] != tmp[l]) {
								en[l] = 0;
								ch = true;
							}
					}
					if (ch && !inwl[to]) {
						inwl[to] = true;
						wl[(*nwl)++] = to;
					}
				}
				kk++;
			}
		}
	}
}

uint32_t vir_alloc_check(vir_function *f, const vir_target_registers *rg, const vir_alloc_out *o, vbuffer *msg)
{
	vir_alloc_checker c = { f, rg, o, msg, 256 + o->spill_count, 0, NULL, { 0, 0 }, false };
	uint32_t nb = f->block_count, *wl, nwl = 0, *st, *tmp, **ent, n = 0;
	bool *seen, *inwl;

	if (!nb)
		return 0;
	c.names = calloc(f->insn_count, 4);
	ent = calloc(nb, sizeof(*ent));
	seen = calloc(nb, 1);
	inwl = calloc(nb, 1);
	wl = calloc(nb, 4);
	st = calloc(c.location_count, 4);
	tmp = calloc(c.location_count, 4);
	if (!c.names || !ent || !seen || !inwl || !wl || !st || !tmp)
		abort();
	for (uint32_t i = 0; i < f->insn_count; i++)
		c.names[i] = VIR_BLOCK_NONE;
	for (uint32_t b = 0; b < nb; b++) {
		for (uint32_t i = f->blocks[b].first; i < f->blocks[b].first + f->blocks[b].insn_count; i++)
			if (f->insns[i].type != VIR_TYPE_VOID)
				c.names[i] = n++;
		ent[b] = calloc(c.location_count, 4);
		if (!ent[b])
			abort();
	}
	seen[0] = inwl[0] = true;
	wl[nwl++] = 0;
	while (nwl) {	/* LIFO; terminates because states only lose entries */
		uint32_t b = wl[--nwl];

		inwl[b] = false;
		memcpy(st, ent[b], (size_t)c.location_count * 4);
		vir_alloc_check_block(&c, b, st, tmp, ent, seen, wl, &nwl, inwl);
	}
	c.in_worklist = true;
	c.written_count[0] = c.written_count[1] = 0;
	for (uint32_t b = 0; b < nb; b++) {
		if (!seen[b]) {
			vir_alloc_check_failure_add(&c, b, 0, "not reached");
			continue;
		}
		memcpy(st, ent[b], (size_t)c.location_count * 4);
		vir_alloc_check_block(&c, b, st, tmp, ent, seen, wl, &nwl, inwl);
	}
	for (int cl = 0; cl < 2; cl++) {
		uint64_t m = c.written_count[cl] & rg->callee[cl] & ~o->callee[cl];

		if (m)
			vir_alloc_check_failure_add(&c, 0, 0, "callee-saved %s%d written but not in the used set", cl ? "f" : "r", __builtin_ctzll(m));
	}
	if (c.error_count > 10)
		vbuffer_format_append(msg, "alloc-check: ... %u failures in all\n", c.error_count);
	for (uint32_t b = 0; b < nb; b++)
		free(ent[b]);
	free(ent);
	free(seen);
	free(inwl);
	free(wl);
	free(st);
	free(tmp);
	free(c.names);
	return c.error_count;
}
