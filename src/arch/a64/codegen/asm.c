/* vcc code generator: inline asm */
#include "../../../codegen/codegen-internal.h"

/* ---------------- inline asm ---------------- */

enum { A64_ASM_OPERAND_REGISTER, A64_ASM_OPERAND_IMMEDIATE, A64_ASM_OPERAND_MEMORY, A64_ASM_OPERAND_ZERO, A64_ASM_OPERAND_VECTOR_REGISTER };

typedef struct a64_asm_operand {
	uint8_t kind, reg, fixed, out, inout, wide;
	codegen_value v;			/* A64_ASM_OPERAND_IMMEDIATE: the constant / address */
	codegen_lvalue l;			/* outputs: where the result goes */
	ast_qualified_type t;
	uint32_t name;
} a64_asm_operand;

/* the register named by a clobber string, or -1 */
/*
 * Known bits of an integer expression of at most 64 bits: *k has a 1 for each
 * bit whose value is known, *v those values (0 elsewhere).  What clang gets
 * from LLVM's known-bits analysis at -O1 and above for an "i" operand that is
 * not a constant expression: the kernel's PIE_E1 ORs in
 * `arm64_use_ng_mappings ? PTE_NG : 0` and then masks those bits away.
 * The operand has been generated already (side effects, calls); this only
 * decides the value.
 */

static int a64_asm_clobber_register(const char *s)
{
	if ((s[0] == 'x' || s[0] == 'w') && s[1] >= '0' && s[1] <= '9') {
		int r = atoi(s + 1);

		return r <= 30 ? r : -1;
	}
	if (!strcmp(s, "lr"))
		return 30;
	return -1;
}

/* a vector register load or store as raw text: op qv|dv, [xr] (k 'z', 'p': an SVE register) */
static void a64_asm_vector_access_emit(codegen_context *e, const char *opn, int k, int v, int xr)
{
	tbuf *rb = &e->raw;
	size_t st = rb->n;
	a64_record *i;

	vtext_char_append(rb, '\t');
	vtext_string_append(rb, opn);
	vtext_char_append(rb, '\t');
	vtext_char_append(rb, (char)k);
	vtext_unsigned_append(rb, (uint64_t)v);
	vtext_string_append(rb, ", [");
	a64_register_name_append(rb, xr, 1);
	vtext_char_append(rb, ']');
	i = a64_record_emit(e, A64_OP_RAW);
	i->off = (int32_t)st;
	i->ref = (uint32_t)(rb->n - st);
}

/* the register name's letter of a vector operand's load and store */
static int a64_asm_vector_register_letter(cc *c, ast_qualified_type t)
{
	return codegen_type_is_sve_predicate(c, t) ? 'p' : codegen_type_is_sve(c, t) ? 'z' : codegen_type_size(c, t) == 16 ? 'q' : 'd';
}

/* the register of a vector operand: v16+ (z16+), an SVE predicate p0+ (a governing predicate must be p0-p7) */
static uint8_t a64_asm_sve_operand_register(codegen_context *e, uint32_t loc, ast_qualified_type t, int *nv, int *np)
{
	if (codegen_type_is_sve_predicate(e->compile_context, t)) {
		if (*np >= 8)
			codegen_fatal_report(e, loc, "too many SVE predicate asm operands");
		return (uint8_t)(*np)++;
	}
	if (*nv >= 15)
		codegen_fatal_report(e, loc, "too many vector asm operands");
	return (uint8_t)(16 + (*nv)++);
}

VPRIV void a64_asm_emit(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	const ast_asm_statement *a = &c->asms[x->a];
	const char *tp = c->strs[a->tmpl - 1].s;
	uint32_t nops = a->nout + a->nin;
	a64_asm_operand *op;
	uint32_t clobs = 0, reserved = 0;
	uint32_t tramp[64];
	bool basic = nops == 0 && a->nclob == 0 && a->nlabel == 0;
	a64_abi_spill sp = { 0 };
	tbuf *rb = &e->raw;
	size_t start;
	uint64_t uniq;
	int nvreg = 0, npreg = 0;

	if (!e->is_reachable)
		return;
	if (nops > 64 || a->nlabel > 64)
		codegen_fatal_report(e, x->loc, "too many asm operands");
	op = alloca((nops + 1) * sizeof(a64_asm_operand));
	memset(op, 0, (nops + 1) * sizeof(a64_asm_operand));
	/* clobbered registers: keep temporaries out of them */
	for (uint32_t i = 0; i < a->nclob; i++) {
		int r = a64_asm_clobber_register(c->strs[a->clob[i] - 1].s);

		if (r >= 0)
			clobs |= 1u << r;
	}
	e->callee_saved_used |= clobs & ((1u << 29) - (1u << 19));	/* callee-saved ones it clobbers: the prologue saves them */
	for (int i = 0; i < CODEGEN_TEMPORARY_COUNT; i++)
		if ((clobs >> codegen_temporary_registers[i]) & 1) {
			if (e->temporaries_in_use & (1u << i)) {
				/* live across a clobber: save it */
				if (!sp.mask)
					memset(&sp, 0, sizeof(sp));
				sp.slots[i] = a64_frame_slot_allocate(e, 8, 8);
				a64_record_emit_memory(e, A64_OP_ST, 3, 0, codegen_temporary_registers[i], A64_REGISTER_FRAME, sp.slots[i]);
				sp.mask |= 1u << i;
			} else {
				e->temporaries_in_use |= 1u << i;
				reserved |= 1u << i;
			}
		}
	/* outputs: lvalues and registers */
	for (uint32_t i = 0; i < a->nout; i++) {
		const ast_asm_operand *ao = &a->ops[i];
		const char *cs = c->strs[ao->constraint - 1].s;
		a64_asm_operand *o = &op[i];
		bool mem_ok = strpbrk(cs, "mQog") != NULL, reg_ok = strpbrk(cs, "rg") != NULL;
		uint32_t en = ao->expr;

		o->out = 1;
		o->inout = strchr(cs, '+') != NULL;
		o->t = N(en)->ty;
		o->name = ao->name;
		if ((codegen_type_is_vector(c, o->t) || codegen_type_is_sve(c, o->t)) && strpbrk(cs, "wxU")) {
			/* a vector in v16+ (an SVE predicate in p0+): loaded before the template if read, stored after it */
			o->kind = A64_ASM_OPERAND_VECTOR_REGISTER;
			o->reg = a64_asm_sve_operand_register(e, x->loc, o->t, &nvreg, &npreg);
			o->l = gen_lval(e, en);
			continue;
		}
		if (strpbrk(cs, "wxy") && !reg_ok)
			codegen_fatal_report(e, x->loc, "floating point asm operands are not supported");
		if (mem_ok && !reg_ok) {
			codegen_lvalue l = gen_lval(e, en);
			codegen_value av = laddr(e, &l);

			o->kind = A64_ASM_OPERAND_MEMORY;
			o->reg = (uint8_t)toreg(e, &av);
			continue;
		}
		o->kind = A64_ASM_OPERAND_REGISTER;
		o->l = gen_lval(e, en);
		if (N(en)->op == AST_NODE_VARIABLE && codegen_symbol_is_local_variable(c, N(en)->a) && codegen_symbol_binding_get(e, N(en)->a)->register_number != 0xff &&
		    codegen_symbol_binding_get(e, N(en)->a)->kind == CODEGEN_BINDING_SLOT) {
			o->fixed = 1;
			o->reg = codegen_symbol_binding_get(e, N(en)->a)->register_number;
		}
		o->wide = codegen_type_is_wide(c, o->t);
		if (o->inout) {
			codegen_lvalue l2 = o->l;
			codegen_value v = load(e, &l2, o->t);

			o->v = v;
		}
	}
	/* inputs */
	for (uint32_t i = a->nout; i < nops; i++) {
		const ast_asm_operand *ao = &a->ops[i];
		const char *cs = c->strs[ao->constraint - 1].s;
		a64_asm_operand *o = &op[i];
		uint32_t en = ao->expr;
		bool reg_ok = strpbrk(cs, "rpg") != NULL, mem_ok = strpbrk(cs, "mQog") != NULL;	/* p: address in a register */
		int tied = -1;

		o->t = N(en)->ty;
		o->name = ao->name;
		for (const char *p = cs; *p; p++)
			if (*p >= '0' && *p <= '9')
				tied = atoi(p);
		if ((codegen_type_is_vector(c, o->t) || codegen_type_is_sve(c, o->t)) && strpbrk(cs, "wxU") && tied < 0) {
			codegen_value v = gen(e, en);	/* the vector's address */

			toreg(e, &v);
			o->kind = A64_ASM_OPERAND_VECTOR_REGISTER;
			o->reg = a64_asm_sve_operand_register(e, x->loc, o->t, &nvreg, &npreg);
			o->v = v;
			continue;
		}
		if (tied >= 0 && (uint32_t)tied < a->nout) {
			/* the input goes into the output's register */
			codegen_value v = gen(e, en);
			a64_asm_operand *to = &op[tied];

			val_free(e, &to->v);
			to->v = v;
			to->inout = 1;
			o->kind = A64_ASM_OPERAND_REGISTER;
			o->reg = 0xff;
			o->fixed = 2;	/* alias of the output */
			o->inout = (uint8_t)tied;
			continue;
		}
		if (mem_ok && !reg_ok && !strpbrk(cs, "inIJKLMNZ")) {
			codegen_lvalue l;

			if (N(en)->op == AST_NODE_CAST && N(en)->aux == AST_CAST_ARRAY_DECAY)
				en = N(en)->a;	/* an array as a memory operand: the array itself */
			l = gen_lval(e, en);
			codegen_value av = laddr(e, &l);

			o->kind = A64_ASM_OPERAND_MEMORY;
			o->reg = (uint8_t)toreg(e, &av);
			continue;
		}
		{
			codegen_value v = gen(e, en);
			bool imm = false;

			if (v.kind != CODEGEN_VALUE_CONSTANT && v.kind != CODEGEN_VALUE_ADDRESS && !reg_ok && !mem_ok && strpbrk(cs, "iInJKLMN")) {
				/* not a constant expression, but every bit known (clang: LLVM's known bits) */
				uint64_t k, kv;
				int w = codegen_type_is_integer(c, N(en)->ty) ? (int)codegen_type_size(c, N(en)->ty) * 8 : 0;

				codegen_expression_known_bits(e, en, &k, &kv);
				if (w && w <= 64 && k == (w >= 64 ? ~0ull : (1ull << w) - 1)) {
					val_free(e, &v);
					v = codegen_value_constant(codegen_integer_truncate(c, (i128)kv, N(en)->ty));
				}
			}
			if (v.kind == CODEGEN_VALUE_CONSTANT || v.kind == CODEGEN_VALUE_ADDRESS) {
				if (strchr(cs, 'Z') && v.kind == CODEGEN_VALUE_CONSTANT && v.integer == 0) {
					o->kind = A64_ASM_OPERAND_ZERO;
					continue;
				}
				for (const char *p = cs; *p; p++)
					if (strchr("iInJKLMNsg", *p) && (v.kind == CODEGEN_VALUE_ADDRESS ? (*p == 'i' || *p == 's' || *p == 'g') : *p == 'g' || codegen_constraint_immediate_fits(*p, v.integer)))
						imm = true;
			}
			if (imm) {
				o->kind = A64_ASM_OPERAND_IMMEDIATE;
				o->v = v;
				if (v.kind == CODEGEN_VALUE_ADDRESS)
					codegen_reference_note(e, v.reference);
				continue;
			}
			if (!reg_ok && !strchr(cs, 'Z')) {
				if (mem_ok) {
					/* a value for a memory operand: put it in a slot */
					int64_t sl = a64_frame_slot_allocate(e, codegen_type_size(c, o->t) ? codegen_type_size(c, o->t) : 8, 16);
					codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = sl, .align = 16 };
					int r;

					store(e, &l, o->t, &v);
					val_free(e, &v);
					r = treg(e);
					a64_record_emit_immediate(e, A64_OP_ADDI, 1, r, A64_REGISTER_FRAME, sl);
					o->kind = A64_ASM_OPERAND_MEMORY;
					o->reg = (uint8_t)r;
					continue;
				}
				codegen_fatal_report(e, x->loc, "impossible constraint in asm: '%s'", cs);
			}
			o->kind = A64_ASM_OPERAND_REGISTER;
			o->wide = codegen_type_is_wide(c, o->t);
			if (o->wide)
				toreg2(e, &v, codegen_type_is_signed(c, o->t));
			else
				toreg(e, &v);
			o->v = v;
			if (N(en)->op == AST_NODE_VARIABLE && codegen_symbol_is_local_variable(c, N(en)->a) && codegen_symbol_binding_get(e, N(en)->a)->register_number != 0xff &&
			    codegen_symbol_binding_get(e, N(en)->a)->kind == CODEGEN_BINDING_SLOT) {
				o->fixed = 1;
				o->reg = codegen_symbol_binding_get(e, N(en)->a)->register_number;
			} else {
				o->reg = v.register_number;
			}
		}
	}
	/* output registers */
	for (uint32_t i = 0; i < a->nout; i++) {
		a64_asm_operand *o = &op[i];

		if (o->kind != A64_ASM_OPERAND_REGISTER)
			continue;
		if (o->inout) {
			if (o->wide)
				toreg2(e, &o->v, false);
			else if (o->v.kind != CODEGEN_VALUE_REGISTER)
				toreg(e, &o->v);
			if (!o->fixed)
				o->reg = o->v.register_number;
		} else if (!o->fixed) {
			o->reg = (uint8_t)treg(e);
			o->v = codegen_value_register(o->reg);
			if (o->wide) {
				o->v.kind = CODEGEN_VALUE_REGISTER_PAIR;
				o->v.register_high = (uint8_t)treg(e);
			}
		}
	}
	/* fixed registers last, so evaluating the others cannot clobber them */
	for (uint32_t i = 0; i < nops; i++) {
		a64_asm_operand *o = &op[i];

		if (o->fixed == 1 && (o->inout || !o->out) && o->v.kind != CODEGEN_VALUE_NONE)
			a64_record_emit_move(e, 1, o->reg, o->v.register_number);
	}
	/* asm goto with outputs: the labels go through trampolines that store them */
	for (uint32_t k = 0; k < a->nlabel; k++)
		tramp[k] = a->nout ? a64_label_new(e) : user_label(e, a->labels[k]);
	/* vector operands into their registers, last so no call in between clobbers them */
	for (uint32_t i = 0; i < nops; i++) {
		a64_asm_operand *o = &op[i];
		int q = a64_asm_vector_register_letter(c, o->t);

		if (o->kind != A64_ASM_OPERAND_VECTOR_REGISTER)
			continue;
		if (!o->out) {
			a64_asm_vector_access_emit(e, "ldr", q, o->reg, o->v.register_number);
			val_free(e, &o->v);
		} else if (o->inout) {
			codegen_lvalue l2 = o->l;
			codegen_value av = laddr(e, &l2);
			int r = toreg(e, &av);

			a64_asm_vector_access_emit(e, "ldr", q, o->reg, r);
			val_free(e, &av);
		}
	}
	/* the template */
	start = rb->n;
	/* %=: unique in the object and the same with any thread count: the
	   function's symbol and the place among its emitted asms that use %=
	   (inlined copies counted) */
	uniq = UINT64_MAX;	/* taken at the first %= */
	if (basic) {
		vtext_string_append(rb, tp);
	} else {
		for (const char *p = tp; *p; p++) {
			char mod = 0;
			int idx = -1;

			if (*p != '%') {
				vtext_char_append(rb, *p);
				continue;
			}
			p++;
			if (*p == '%') { vtext_char_append(rb, '%'); continue; }
			if (*p == '=') {
				if (uniq == UINT64_MAX) {
					if (e->asm_statement_count >= 1024)
						codegen_fatal_report(e, x->loc, "more than 1024 inline asm statements with %%= in one function");
					uniq = (uint64_t)e->top_symbol * 1024 + e->asm_statement_count++;
				}
				vtext_unsigned_append(rb, uniq);
				continue;
			}
			if (*p == '{' || *p == '|' || *p == '}') { vtext_char_append(rb, *p); continue; }
			if (*p && strchr("wxcalHnPQRsqdbh", *p) && (p[1] == '[' || (p[1] >= '0' && p[1] <= '9')))
				mod = *p++;
			if (*p == '[') {
				const char *q = strchr(p, ']');
				uint32_t len = q ? (uint32_t)(q - p - 1) : 0;

				for (uint32_t i = 0; i < nops; i++)
					if (op[i].name && vintern_ents[op[i].name].len == len &&
					    !memcmp(vintern_ents[op[i].name].s, p + 1, len))
						idx = (int)i;
				if (idx < 0)
					for (uint32_t k = 0; k < a->nlabel; k++) {
						const label *lb = &c->labels[a->labels[k]];

						if (vintern_ents[lb->name].len == len && !memcmp(vintern_ents[lb->name].s, p + 1, len))
							idx = (int)(nops + k);
					}
				if (idx < 0)
					codegen_fatal_report(e, x->loc, "unknown asm operand name '%.*s'", (int)len, p + 1);
				p = q;
			} else if (*p >= '0' && *p <= '9') {
				idx = 0;
				while (p[0] >= '0' && p[0] <= '9')
					idx = idx * 10 + (*p++ - '0');
				p--;
			} else {
				codegen_fatal_report(e, x->loc, "invalid %% escape in inline assembly string");
			}
			if ((uint32_t)idx >= nops) {
				uint32_t k = (uint32_t)idx - nops;

				if (k >= a->nlabel)
					codegen_fatal_report(e, x->loc, "invalid operand number %d in inline asm string (%u operands, %u labels)", idx, nops, a->nlabel);
				a64_print_label(rb, e->top_symbol, tramp[k]);
				continue;
			}
			{
				a64_asm_operand *o = &op[idx];

				if (o->fixed == 2)
					o = &op[o->inout];
				switch (o->kind) {
				case A64_ASM_OPERAND_VECTOR_REGISTER:
					if (codegen_type_is_sve(c, o->t)) {
						vtext_char_append(rb, (char)a64_asm_vector_register_letter(c, o->t));
						vtext_unsigned_append(rb, o->reg);
						break;
					}
					vtext_char_append(rb, mod == 'q' || mod == 'd' || mod == 's' || mod == 'h' || mod == 'b' ? mod : 'v');
					vtext_unsigned_append(rb, o->reg);
					break;
				case A64_ASM_OPERAND_REGISTER:
					if (mod == 'H' && o->wide) {
						a64_register_name_append(rb, o->v.register_high, 1);
					} else if (mod == 'a') {	/* address operand: [xN] */
						vtext_char_append(rb, '[');
						a64_register_name_append(rb, o->reg, 1);
						vtext_char_append(rb, ']');
					} else {
						a64_register_name_append(rb, o->reg, mod != 'w');
					}
					break;
				case A64_ASM_OPERAND_ZERO:
					a64_register_name_append(rb, ZR, mod != 'w');
					break;
				case A64_ASM_OPERAND_MEMORY:
					vtext_char_append(rb, '[');
					a64_register_name_append(rb, o->reg, 1);
					vtext_char_append(rb, ']');
					break;
				case A64_ASM_OPERAND_IMMEDIATE:
					if (o->v.kind == CODEGEN_VALUE_ADDRESS) {
						codegen_reference_offset_append(c, rb, o->v.reference, (int64_t)o->v.integer);
					} else {	/* clang prints a bare number, as the assembler reads it */
						vtext_signed_append(rb, mod == 'n' ? -(int64_t)o->v.integer : (int64_t)o->v.integer);
					}
					break;
				}
			}
		}
	}
	{
		a64_record *i = a64_record_emit(e, A64_OP_RAW);

		i->off = (int32_t)start;
		i->ref = (uint32_t)(rb->n - start);
		i->cond = 1;
		/*
		 * Extended asm reads and writes only its operands and clobbers:
		 * the registers go after the text (cond 2, A64_RAW_HAS_REGISTER_MASKS) for opt.c.
		 * Basic asm and asm goto (its labels need what is live there)
		 * stay unknown.
		 */
		if (!basic && !a->nlabel) {
			uint64_t m[2] = { (1ull << 18) | (1ull << 29) | (1ull << 30), clobs };	/* reads, writes */

			for (uint32_t k = 0; k < nops; k++) {
				const a64_asm_operand *o = &op[k];
				uint64_t r;

				if (o->kind == A64_ASM_OPERAND_MEMORY) {
					m[0] |= 1ull << o->reg;
					continue;
				}
				if (o->kind != A64_ASM_OPERAND_REGISTER || o->reg == 0xff)
					continue;
				r = 1ull << o->reg;
				if (o->v.kind == CODEGEN_VALUE_REGISTER_PAIR)
					r |= 1ull << o->v.register_high;
				if (o->out)
					m[1] |= r;
				if (!o->out || o->inout)
					m[0] |= r;
			}
			vtext_append(rb, (const char *)m, sizeof(m));
			i->cond = A64_RAW_HAS_REGISTER_MASKS;
		}
	}
	for (uint32_t i = 0; i < a->nout; i++) {
		a64_asm_operand *o = &op[i];

		if (o->kind == A64_ASM_OPERAND_VECTOR_REGISTER) {
			codegen_lvalue l2 = o->l;
			codegen_value av = laddr(e, &l2);
			int r = toreg(e, &av);

			a64_asm_vector_access_emit(e, "str", a64_asm_vector_register_letter(c, o->t), o->reg, r);
			val_free(e, &av);
		}
	}
	/* fixed output registers back into their temporaries */
	for (uint32_t i = 0; i < a->nout; i++) {
		a64_asm_operand *o = &op[i];

		if (o->kind == A64_ASM_OPERAND_REGISTER && o->fixed == 1) {
			if (o->v.kind != CODEGEN_VALUE_REGISTER) {
				o->v = codegen_value_register(treg(e));
			}
			a64_record_emit_move(e, 1, o->v.register_number, o->reg);
		}
	}
	if (sp.mask)
		a64_abi_temporaries_restore(e, &sp);
	e->temporaries_in_use &= ~reserved;
	/* store the outputs (on the fallthrough path and through each trampoline) */
	{
		uint32_t lafter = a->nlabel && a->nout ? a64_label_new(e) : 0;

		for (int pass = 0; pass <= (int)(a->nout ? a->nlabel : 0); pass++) {
			if (pass > 0) {
				e->label_reference_counts[tramp[pass - 1]] |= CODEGEN_LABEL_EXTERNAL;	/* (the asm jumps here) */
				a64_label_define_forced(e, tramp[pass - 1]);
			}
			for (uint32_t i = 0; i < a->nout; i++) {
				a64_asm_operand *o = &op[i];

				if (o->kind != A64_ASM_OPERAND_REGISTER)
					continue;
				{
					codegen_value v = o->v;
					codegen_lvalue l = o->l;

					store(e, &l, o->t, &v);
				}
			}
			if (pass > 0)
				a64_jump_emit(e, user_label(e, a->labels[pass - 1]));
			else if (lafter)
				a64_jump_emit(e, lafter);
		}
		if (lafter)
			deflabel(e, lafter);
	}
	for (uint32_t i = 0; i < nops; i++) {
		a64_asm_operand *o = &op[i];

		if (o->kind == A64_ASM_OPERAND_REGISTER && o->fixed != 2)
			val_free(e, &o->v);
		if (o->kind == A64_ASM_OPERAND_MEMORY)
			tfree(e, o->reg);
		if (o->out && o->kind == A64_ASM_OPERAND_REGISTER)
			lfree(e, &o->l);
	}
	/* asm goto without outputs: the labels are branch targets */
	if (!a->nout)
		for (uint32_t k = 0; k < a->nlabel; k++) {
			a64_label_edge_add(e, tramp[k]);
			e->label_reference_counts[tramp[k]]++;
			e->label_reference_counts[tramp[k]] |= CODEGEN_LABEL_EXTERNAL;
		}
}
