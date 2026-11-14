/* vcc code generator, x86-64: inline asm in AT&T syntax (for -S) */
#include "x64-internal.h"

/*
 * An operand: in a register (a temporary, or a fixed one a constraint or
 * a register variable names), in memory (an lvalue, or a slot holding an
 * input's value), or an immediate.  Fixed registers and clobbers are
 * reserved before any operand is evaluated: a temporary that is live in
 * one (the enclosing expression's) is saved to a slot and restored after.
 */
enum { X64_ASM_OPERAND_REGISTER, X64_ASM_OPERAND_MEMORY, X64_ASM_OPERAND_IMMEDIATE, X64_ASM_OPERAND_X87 };	/* X64_ASM_OPERAND_X87: on the x87 stack, reg 0 st(0) (t), 1 st(1) (u) */

typedef struct x64_asm_operand {
	uint8_t kind, register_number, is_output, is_input_output, is_float, fixed, is_stack_pointer;	/* sp: register ... asm("rsp") (ASM_CALL_CONSTRAINT): %rsp itself, nothing moved */
	int8_t tied_output;		/* an input: the output it shares a register with, -1 none */
	int size;		/* the operand's size in bytes (register names) */
	codegen_value value;			/* X64_ASM_OPERAND_IMMEDIATE: the value; an input's value until it is placed */
	codegen_lvalue lvalue;			/* X64_ASM_OPERAND_MEMORY: the memory; outputs: where the result goes */
	int64_t parked_slot;		/* a fixed input's value parked in this slot (0: none) */
	int32_t address_parked_slot;		/* a register output's address parked in this slot until the store (0: none) */
	ast_qualified_type type;
	uint32_t name;
} x64_asm_operand;

/* the register a constraint letter names, -1 for none */
static int x64_asm_constraint_register(int ch)
{
	switch (ch) {
	case 'a': return RAX;
	case 'b': return RBX;
	case 'c': return RCX;
	case 'd': return RDX;
	case 'S': return RSI;
	case 'D': return RDI;
	default: return -1;
	}
}

/* the register a clobber or register variable names (with or without %), -1 for none */
VPRIV int x64_asm_register_find(const char *s)
{
	if (*s == '%')
		s++;
	/* The legacy registers have one letter family and a small set of width
	 * spellings. Decode them directly instead of searching four name tables. */
	if ((s[0] == 'r' || s[0] == 'e') && s[1] && s[2] && !s[3]) {
		const char *p = s + 1;
		if (p[1] == 'x') {
			switch (p[0]) {
			case 'a': return RAX; case 'c': return RCX;
			case 'd': return RDX; case 'b': return RBX;
			}
		} else if (p[1] == 'p') {
			if (p[0] == 's') return RSP;
			if (p[0] == 'b') return RBP;
		} else if (p[1] == 'i') {
			if (p[0] == 's') return RSI;
			if (p[0] == 'd') return RDI;
		}
	}
	if (s[0] && s[1]) {
		int r = s[0] == 'a' ? RAX : s[0] == 'c' ? RCX :
			s[0] == 'd' ? RDX : s[0] == 'b' ? RBX : -1;
		if (r >= 0 && (s[1] == 'x' || s[1] == 'l' || s[1] == 'h') && !s[2])
			return r;
		if ((s[0] == 's' || s[0] == 'b' || s[0] == 'd') &&
		    (s[1] == 'p' || s[1] == 'i') &&
		    (!s[2] || (s[2] == 'l' && !s[3]))) {
			if (s[0] == 's') return s[1] == 'p' ? RSP : RSI;
			if (s[0] == 'b' && s[1] == 'p') return RBP;
			if (s[0] == 'd' && s[1] == 'i') return RDI;
		}
	}
	if (s[0] == 'r' && s[1] >= '0' && s[1] <= '9') {
		int r = atoi(s + 1);

		return r >= 8 && r <= 15 ? r : -1;
	}
	if (s[0] == 'x' && s[1] == 'm' && s[2] == 'm') {
		int r = atoi(s + 3);

		return r >= 0 && r <= 15 ? XMM0 + r : -1;
	}
	return -1;
}

/* a register's name for an operand of size bytes; h: the high byte (ah ...) */
static void x64_asm_register_append(tbuf *b, int r, int size, bool h)
{
	static const char *const n64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" };
	static const char *const n32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
	static const char *const n16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
	static const char *const n8[] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil" };
	static const char *const nh[] = { "ah", "ch", "dh", "bh" };

	vtext_char_append(b, '%');
	if (r >= XMM0) {
		vtext_string_append(b, "xmm");
		vtext_unsigned_append(b, (uint64_t)(r - XMM0));
		return;
	}
	if (h && r < 4) {
		vtext_string_append(b, nh[r]);
		return;
	}
	if (r < 8) {
		vtext_string_append(b, size == 8 ? n64[r] : size == 4 ? n32[r] : size == 2 ? n16[r] : n8[r]);
		return;
	}
	vtext_char_append(b, 'r');
	vtext_unsigned_append(b, (uint64_t)r);
	if (size != 8)
		vtext_char_append(b, size == 4 ? 'd' : size == 2 ? 'w' : 'b');
}

/* a local label of this function, as print.c names it */
static void x64_asm_label_append(codegen_context *e, tbuf *b, uint32_t l)
{
	vtext_string_append(b, ".LBB");
	vtext_unsigned_append(b, e->walk_state->function_symbol);
	vtext_char_append(b, '_');
	vtext_unsigned_append(b, l);
}

/* a memory operand as AT&T text; mark: a local's displacement is the printer's to place (the
   Windows frame: X_ASMFRAME, then the offset below the save area) */
static void x64_asm_memory_append(cc *c, tbuf *b, const codegen_lvalue *l, bool mark)
{
	if (l->kind == CODEGEN_LVALUE_SYMBOL) {
		codegen_reference_offset_append(c, b, l->reference, l->offset);
		vtext_string_append(b, "(%rip)");
		return;
	}
	if (mark && l->kind == CODEGEN_LVALUE_FRAME && l->offset < 0)
		vtext_char_append(b, X_ASMFRAME);
	if (l->offset)
		vtext_signed_append(b, l->offset);
	vtext_char_append(b, '(');
	/* Resolve negative frame offsets after all local alignments are known. */
	if (l->kind == CODEGEN_LVALUE_FRAME && l->offset < 0)
		vtext_char_append(b, 0);
	else
		x64_asm_register_append(b, l->kind == CODEGEN_LVALUE_FRAME ? RBP : l->register_number, 8, false);
	vtext_char_append(b, ')');
}

/* a float value as an x87 long double object's address (fld of it) */
static codegen_value x64_asm_x87_value(codegen_context *e, codegen_value v, ast_qualified_type t)
{
	return walk_value_convert(e, v, t, codegen_type_primitive(AST_TYPE_LONG_DOUBLE));
}

static bool x64_asm_immediate_fits(int ch, i128 v)
{
	switch (ch) {
	case 'i': case 'n': case 'g': return true;
	case 'e': case 'Z': return ch == 'e' ? v >= INT32_MIN && v <= INT32_MAX : v >= 0 && v <= UINT32_MAX;
	case 'I': return v >= 0 && v <= 31;
	case 'J': return v >= 0 && v <= 63;
	case 'K': return v >= -128 && v <= 127;
	case 'L': return v == 0xff || v == 0xffff || v == 0xffffffff;
	case 'M': return v >= 0 && v <= 3;
	case 'N': return v >= 0 && v <= 255;
	case 'O': return v >= 0 && v <= 127;
	default: return false;
	}
}

/* A GPR constraint transports object bits, independently of the C value's
 * normal register class (SSE scalars and address-held small aggregates). */
static codegen_value x64_asm_bits_transport(codegen_context *e, codegen_value v, ast_qualified_type t, int size)
{
	if (size != 1 && size != 2 && size != 4 && size != 8)
		codegen_fatal_report(e, e->current_location, "x86-64: unsupported asm GPR operand size");
	if (walk_type_is_in_memory(e, t)) {
		int r = walk_value_to_register(e, &v, t), dst = x64_treg(e);
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)r };
		x64_load(e, dst, &l, size, false);
		walk_value_free(e, &v);
		return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)dst };
	}
	if (codegen_type_is_float(e->compile_context, t)) {
		int r = walk_value_to_register(e, &v, t);
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 8, 8) };
		x64_store(e, r, &l, size);
		walk_value_free(e, &v);
		r = x64_treg(e);
		x64_load(e, r, &l, size, false);
		return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
	}
	return v;
}

/* Fixed operands are loaded only after all expressions have been evaluated.
 * Park their input values immediately so several read/write outputs do not
 * consume the few temporaries left after reserving the fixed registers. */
static void x64_asm_operand_park(codegen_context *e, x64_asm_operand *o)
{
	if (!o->is_float) o->value = x64_asm_bits_transport(e, o->value, o->type, o->size);
	int r = walk_value_to_register(e, &o->value, o->is_float ? o->type : codegen_type_primitive(AST_TYPE_UNSIGNED_LONG_LONG));
	codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = o->parked_slot = walk_slot_temporary(e, 8, 8), .align = 8 };

	x64_store(e, r, &l, o->is_float ? o->size : 8);
	walk_value_free(e, &o->value);
	o->value = (codegen_value){ 0 };
}

/* a callee-saved register the asm uses: saved by the prologue */
VPRIV void x64_callee_save_use(codegen_context *e, int r)
{
	x64_lowering_context *X = e->x64_context;

	X->csmask |= UINT64_C(1) << r;
	if (r == R12 && e->walk_state->frame_alignment > 16)
		codegen_fatal_report(e, e->current_location, "x86-64: asm using r12 in a function with locals aligned above 16 is not supported");
	if (X->winframe)
		return;	/* (print.c saves what the body writes) */
	if (r == RBX || (r == RBP && X->stack_pointer_frame) || (r >= R12 && r <= R15)) {
		if (!X->callee_save_slots[r])
			X->callee_save_slots[r] = walk_slot_permanent(e, 8, 8);
	}
}

VPRIV void x64_asm(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	x64_lowering_context *X = e->x64_context;
	const node *x = N(n);
	const ast_asm_statement *a = &c->asms[x->a];
	const char *tp = c->strs[a->tmpl - 1].s;
	uint32_t nops = a->nout + a->nin;
	bool basic = !a->extended;
	uint32_t serial = e->asm_statement_count++, nplus = 0;
	uint32_t edges[64], done = 0;
	const char *csop[64];
	const char *ccop[64] = { 0 };	/* "=@cc<cond>": the flag the output reads (set<cond> after the template) */
	uint32_t fixed = 0, saved = 0, reserved = 0;
	int64_t sslot[32] = { 0 };
	x64_asm_operand *op;
	tbuf *rb = &e->raw;
	size_t start;
	record *ri;

	if (nops > 64 || a->nlabel > 64)
		codegen_fatal_report(e, x->loc, "too many asm operands");
	/* Select one common alternative, before evaluating any operand. */
	unsigned alternatives = 1;
	for (uint32_t i = 0; i < nops; i++) {
		const char *cs = c->strs[a->ops[i].constraint - 1].s;
		unsigned count = 1;
		nplus += i < a->nout && strchr(cs, '+') != NULL;
		for (; *cs; cs++) count += *cs == ',';
		if (count > alternatives) alternatives = count;
	}
	bool selected = false;
	for (unsigned alt = 0; alt < alternatives; alt++) {
		bool valid = true;
		uint32_t occupied = 0, early = 0;
		const char *q_cc;
		for (uint32_t i = 0; i < a->nclob; i++) {
			int r = x64_asm_register_find(c->strs[a->clob[i] - 1].s);
			if (r >= 0) occupied |= 1u << r;
		}
		for (uint32_t i = 0; i < nops; i++) {
			const char *original = c->strs[a->ops[i].constraint - 1].s, *cs = original;
			for (unsigned k = 0; k < alt && cs; k++) {
				cs = strchr(cs, ',');
				if (cs) cs++;
			}
			if (!cs) { valid = false; break; }
			size_t len = strcspn(cs, ",");
			char *copy = alloca(len + 2);
			copy[0] = i < a->nout ? original[0] : ' ';
			memcpy(copy + 1, cs, len);
			copy[len + 1] = 0;
			csop[i] = copy;
			if (i < a->nout && (q_cc = strstr(copy, "@cc"))) {
				ccop[i] = q_cc + 3;
				csop[i] = "=r";
				continue;
			}
			int r = -1;
			for (const char *q = copy; *q && *q != '[' && r < 0; q++) r = x64_asm_constraint_register(*q);
			if (r >= 0) {
				if ((occupied | early) & (1u << r)) valid = false;
				if (i < a->nout && strchr(copy, '&')) early |= 1u << r;
				if (i >= a->nout) occupied |= 1u << r;
			}
		}
		if (valid) { selected = true; break; }
	}
	if (!selected) codegen_fatal_report(e, x->loc, "no allocatable x86 inline asm constraint alternative");
	for (uint32_t i = 0; i < a->nlabel; i++) edges[i] = walk_label_new(e);
	if (a->nlabel) done = walk_label_new(e);
	op = alloca((nops + 1) * sizeof(x64_asm_operand));
	memset(op, 0, (nops + 1) * sizeof(x64_asm_operand));
	/* the fixed registers: clobbers and register constraints */
	for (uint32_t i = 0; i < a->nclob; i++) {
		int r = x64_asm_register_find(c->strs[a->clob[i] - 1].s);

		if (r >= 0) {
			fixed |= 1u << r;
			x64_callee_save_use(e, r);
		}
	}
	for (uint32_t i = 0; i < nops; i++) {
		const char *cs = csop[i];
		uint32_t en = a->ops[i].expr;
		int r = -1;

		for (const char *p = cs; *p && *p != '[' && r < 0; p++)
			r = x64_asm_constraint_register(*p);
		if (r < 0 && N(en)->op == AST_NODE_VARIABLE && codegen_symbol_attributes(c, N(en)->a)->asmname && (c->syms[N(en)->a].sc == AST_STORAGE_REGISTER))
			r = x64_asm_register_find(c->strs[codegen_symbol_attributes(c, N(en)->a)->asmname - 1].s);	/* register long x asm("rdx") */
		if (r == RSP && N(en)->op == AST_NODE_VARIABLE && c->syms[N(en)->a].sc == AST_STORAGE_REGISTER) {
			op[i].fixed = op[i].is_stack_pointer = 1;
			op[i].register_number = RSP;
			continue;
		}
		if (r >= 0) {
			op[i].fixed = 1;
			op[i].register_number = (uint8_t)r;
			fixed |= 1u << r;
			x64_callee_save_use(e, r);
		}
	}
	/* Q and R restrict the GPR class; reserve a suitable register before
	   evaluating operands, exactly like an explicit register constraint. */
	for (uint32_t i = 0; i < nops; i++) {
		const char *cs = csop[i];
		if (op[i].fixed || strpbrk(cs, "rqgX[")) continue;
		uint32_t allowed = strchr(cs, 'Q') ? 15u : strchr(cs, 'R') ? 0xcfu : 0;
		if (!allowed) continue;
		allowed &= ~fixed;
		if (!allowed) codegen_fatal_report(e, x->loc, "no allocatable x86 inline asm register class");
		int r = __builtin_ctz(allowed);
		op[i].fixed = 1;
		op[i].register_number = (uint8_t)r;
		fixed |= 1u << r;
		x64_callee_save_use(e, r);
	}
	if (fixed & ((1u << RSP) | (1u << RBP)))
		codegen_fatal_report(e, x->loc, "x86-64: asm clobbering rsp or rbp is not supported");
	/* Preserve current register-local values independently of the prologue's
	   original caller values. These homes are not expression temporaries. */
	uint32_t homes = 0;
	for (unsigned i = 0; i < X->local_register_count; i++) homes |= 1u << X->local_registers[i];
	/* Fixed registers stay reserved while operands are evaluated. */
	for (int r = 0; r < 32; r++)
		if (fixed & (1u << r)) {
			if ((X->temporaries_in_use | homes) & (1u << r)) {
				codegen_lvalue s = { .kind = CODEGEN_LVALUE_FRAME, .offset = sslot[r] = walk_slot_temporary(e, r >= XMM0 ? 16 : 8, 16), .align = 8 };

				x64_record_emit_memory(e, X64_OP_ST, r >= XMM0 ? 16 : 8, r, &s, false);
				saved |= 1u << r;
			}
			/* A home gets a snapshot but no lasting temporary ownership. */
			if (!(X->temporaries_in_use & (1u << r))) reserved |= 1u << r;
			X->temporaries_in_use |= 1u << r;
		}
	X->asm_callee_saved = true;	/* (operands may take callee-saved registers from here) */
	/* inputs that are inlined parameters still holding their constant: read before an
	   output writes them (rip_rel_ptr: "=r"(p) : "i"(p)) */
	codegen_value pkv[64];
	uint64_t pkm = 0;

	for (uint32_t i = a->nout; i < nops && i < 64; i++)
		if (N(a->ops[i].expr)->op == AST_NODE_VARIABLE && strpbrk(c->strs[a->ops[i].constraint - 1].s, "in") &&
		    walk_parameter_constant_get(e, N(a->ops[i].expr)->a, &pkv[i]))
			pkm |= 1ull << i;
	/* outputs: lvalues; a register's value read first for + */
	for (uint32_t i = 0; i < a->nout; i++) {
		const char *cs = csop[i];
		uint32_t en = a->ops[i].expr;
		x64_asm_operand *o = &op[i];
		bool mem_ok = strpbrk(cs, "mogVX") != NULL, reg_ok = strpbrk(cs, "rqQRgXabcdSDxyl") != NULL || o->fixed;

		o->is_output = 1;
		o->tied_output = -1;
		o->is_input_output = strchr(cs, '+') != NULL;
		o->type = N(en)->ty;
		o->name = a->ops[i].name;
		o->size = walk_type_size(c, o->type);
		o->is_float = strpbrk(cs, "xy") != NULL && codegen_type_is_float(c, o->type);
		if (o->is_stack_pointer) {
			o->kind = X64_ASM_OPERAND_REGISTER;
			continue;
		}
		o->lvalue = walk_lvalue_emit(e, en);
		if (strpbrk(cs, "tu") && codegen_type_is_float(c, o->type)) {
			o->kind = X64_ASM_OPERAND_X87;
			o->register_number = strchr(cs, 'u') ? 1 : 0;
			if (o->is_input_output) o->value = x64_asm_x87_value(e, walk_lvalue_reload(e, &o->lvalue, o->type), o->type);
			continue;
		}
		if (mem_ok && !reg_ok) {
			o->kind = X64_ASM_OPERAND_MEMORY;
			if (o->lvalue.is_bitfield)
				codegen_fatal_report(e, x->loc, "x86-64: a bit-field as an asm memory operand");
			continue;
		}
		o->kind = X64_ASM_OPERAND_REGISTER;
		if (walk_type_is_in_memory(e, o->type) && (o->is_float || o->size > 8))
			codegen_fatal_report(e, x->loc, "x86-64: address-held asm register operand is not supported");
		if (o->is_input_output) {
			o->value = walk_lvalue_reload(e, &o->lvalue, o->type);
			if (o->fixed) x64_asm_operand_park(e, o);
		}
		if (o->lvalue.kind == CODEGEN_LVALUE_REGISTER && !o->lvalue.is_bitfield) {
			/* the address waits in a slot: four outputs through pointers and four fixed
			   registers (cpuid) would leave no temporaries */
			codegen_lvalue s = { .kind = CODEGEN_LVALUE_FRAME, .offset = o->address_parked_slot = walk_slot_temporary(e, 8, 8), .align = 8 };

			x64_record_emit_memory(e, X64_OP_ST, 8, o->lvalue.register_number, &s, false);
			x64_tfree(e, o->lvalue.register_number);
		}
	}
	/* inputs */
	for (uint32_t i = a->nout; i < nops; i++) {
		const char *cs = csop[i];
		uint32_t en = a->ops[i].expr;
		x64_asm_operand *o = &op[i];
		bool reg_ok = strpbrk(cs, "rqQRgXabcdSDxyl") != NULL || o->fixed, mem_ok = strpbrk(cs, "mogVX") != NULL;
		int tied = -1;
		codegen_value v;

		o->type = N(en)->ty;
		o->name = a->ops[i].name;
		o->size = walk_type_size(c, o->type);
		o->is_float = strpbrk(cs, "xy") != NULL && codegen_type_is_float(c, o->type);
		o->tied_output = -1;
		if (o->is_stack_pointer) {
			o->kind = X64_ASM_OPERAND_REGISTER;
			continue;
		}
		for (const char *p = cs; *p; p++) {
			if (*p >= '0' && *p <= '9') { tied = atoi(p); break; }
			if (*p == '[') {
				const char *end = strchr(p, ']');
				for (uint32_t j = 0; end && j < a->nout; j++)
					if (op[j].name && vintern_ents[op[j].name].len == (uint32_t)(end - p - 1) &&
					    !memcmp(vintern_ents[op[j].name].s, p + 1, (size_t)(end - p - 1))) tied = (int)j;
				break;
			}
		}
		if (tied >= 0 && (uint32_t)tied < a->nout && op[tied].kind == X64_ASM_OPERAND_X87) {
			o->kind = X64_ASM_OPERAND_X87;
			o->register_number = op[tied].register_number;
			o->tied_output = (int8_t)tied;
			o->value = x64_asm_x87_value(e, walk_value_emit(e, en), o->type);
			continue;
		}
		if (strpbrk(cs, "tu") && codegen_type_is_float(c, o->type)) {
			o->kind = X64_ASM_OPERAND_X87;
			o->register_number = strchr(cs, 'u') ? 1 : 0;
			o->value = x64_asm_x87_value(e, walk_value_emit(e, en), o->type);
			continue;
		}
		if (tied >= 0 && (uint32_t)tied < a->nout) {
			/* the output's register, holding this value before the template */
			x64_asm_operand *to = &op[tied];

			v = walk_value_emit(e, en);
			if (to->is_input_output)
				walk_value_free(e, &to->value);
			to->value = v;
			to->is_input_output = 1;
			if (to->fixed) x64_asm_operand_park(e, to);
			o->kind = X64_ASM_OPERAND_REGISTER;
			o->tied_output = (int8_t)tied;
			continue;
		}
		/* Flexible FP inputs may stay in memory. Reserving an XMM temporary
		   for every "xm" input can exhaust the register class even though
		   the instruction accepts the original scalar memory operand. */
		if (o->is_float && mem_ok && !o->fixed) reg_ok = false;
		if (mem_ok && !reg_ok && !strpbrk(cs, "inIJKLMNeZ")) {
			if (N(en)->op == AST_NODE_CAST && N(en)->aux == AST_CAST_ARRAY_DECAY)
				en = N(en)->a;	/* an array as a memory operand: the array itself */
			if (N(en)->flags & AST_NODE_FLAG_LVALUE || N(en)->op == AST_NODE_VARIABLE || N(en)->op == AST_NODE_DEREFERENCE || N(en)->op == AST_NODE_MEMBER ||
			    N(en)->op == AST_NODE_INDEX) {
				o->kind = X64_ASM_OPERAND_MEMORY;
				o->lvalue = walk_lvalue_emit(e, en);
				continue;
			}
		}
		{
			cc_value cv;
			uint32_t an = en;

			while (N(an)->op == AST_NODE_CAST && (N(an)->aux == AST_CAST_NOP || N(an)->aux == AST_CAST_POINTER_TO_POINTER || N(an)->aux == AST_CAST_ARRAY_DECAY ||
						       N(an)->aux == AST_CAST_FUNCTION_DECAY))
				an = N(an)->a;
			bool address = N(an)->op == AST_NODE_ADDRESS;
			if (address) an = N(an)->a;
			if (i < 64 && (pkm >> i & 1)) {
				v = pkv[i];	/* an inlined parameter still holding its constant argument */
			} else if (codegen_type_is_integer(e->compile_context, N(en)->ty) && cc_eval(e->compile_context, en, &cv) && cv.kind == CC_VALUE_INTEGER) {
				/* an integer constant expression as the front end folds it (__builtin_clzll(63), ...) */
				v = codegen_value_constant(codegen_integer_truncate(e->compile_context, cv.i, N(en)->ty));
			} else if (e->compile_context->o->pic && N(an)->op == AST_NODE_VARIABLE && (c->syms[N(an)->a].kind == AST_SYMBOL_FUNCTION ||
				   (c->syms[N(an)->a].kind == AST_SYMBOL_VARIABLE && (address || codegen_type_is_array(c, N(an)->ty)) && !codegen_symbol_is_local_variable(c, N(an)->a)))) {
				/* &sym under -fPIC: an immediate, as clang (the asm names the symbol) */
				v = (codegen_value){ .kind = CODEGEN_VALUE_ADDRESS, .reference = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, codegen_symbol_canonical(c, N(an)->a)) };
			} else {
				v = walk_value_emit(e, en);
			}
		}
		if (v.kind == CODEGEN_VALUE_CONSTANT || v.kind == CODEGEN_VALUE_ADDRESS) {	/* (an address is an immediate under -fPIC too, as clang) */
			bool imm = false;
			if (v.kind == CODEGEN_VALUE_ADDRESS) codegen_reference_note(e, v.reference);

			for (const char *p = cs; *p; p++)
				if (v.kind == CODEGEN_VALUE_ADDRESS ? (*p == 'i' || *p == 'g' || *p == 's') : x64_asm_immediate_fits(*p, v.integer))
					imm = true;
			if (imm) {
				o->kind = X64_ASM_OPERAND_IMMEDIATE;
				o->value = v;
				continue;
			}
		}
		if (!reg_ok) {
			if (!mem_ok)
				codegen_fatal_report(e, x->loc, "impossible constraint in asm: '%s'", cs);
			/* a value for a memory operand: in a slot */
			o->kind = X64_ASM_OPERAND_MEMORY;
			o->lvalue = (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, o->size > 8 ? (uint64_t)o->size : 8, 16), .align = 16 };
			if (walk_type_is_in_memory(e, o->type)) {
				v = walk_lvalue_store_memory(e, &o->lvalue, o->type, v);
				walk_value_free(e, &v);
			} else {
				int r = walk_value_to_register(e, &v, o->type);

				x64_store(e, r, &o->lvalue, o->size);
				x64_tfree(e, r);
			}
			continue;
		}
		o->kind = X64_ASM_OPERAND_REGISTER;
		if (walk_type_is_in_memory(e, o->type) && (o->is_float || o->size > 8))
			codegen_fatal_report(e, x->loc, "x86-64: address-held asm register operand is not supported");
		if (!o->is_float) v = x64_asm_bits_transport(e, v, o->type, o->size);
		if (!o->is_float && o->size > 8)
			codegen_fatal_report(e, x->loc, "x86-64: an asm register operand of this type is not supported");
		if (o->fixed) {
			/* parked until the fixed registers are loaded, last */
			codegen_lvalue s = { .kind = CODEGEN_LVALUE_FRAME, .offset = o->parked_slot = walk_slot_temporary(e, 8, 8), .align = 8 };
			int r = walk_value_to_register(e, &v, o->is_float ? o->type : codegen_type_primitive(AST_TYPE_UNSIGNED_LONG_LONG));

			x64_record_emit_memory(e, X64_OP_ST, o->is_float ? o->size : 8, r, &s, false);
			x64_tfree(e, r);
		} else {
			walk_value_to_register(e, &v, o->is_float ? o->type : codegen_type_primitive(AST_TYPE_UNSIGNED_LONG_LONG));
			o->register_number = v.register_number;
			o->value = v;
		}
	}
	/* output registers */
	for (uint32_t i = 0; i < a->nout; i++) {
		x64_asm_operand *o = &op[i];

		if (o->kind != X64_ASM_OPERAND_REGISTER || o->fixed)
			continue;
		if (o->is_input_output && !o->is_float) o->value = x64_asm_bits_transport(e, o->value, o->type, o->size);
		if (o->is_input_output) {
			walk_value_to_register(e, &o->value, o->is_float ? o->type : codegen_type_primitive(AST_TYPE_UNSIGNED_LONG_LONG));
			o->register_number = o->value.register_number;
		} else {
			o->register_number = (uint8_t)(o->is_float ? x64_ftreg(e) : x64_treg(e));
			o->value = (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = o->register_number };
		}
	}
	/* the fixed registers loaded last: nothing evaluated after can clobber them */
	for (uint32_t i = 0; i < nops; i++)
		if (op[i].parked_slot) {
			codegen_lvalue s = { .kind = CODEGEN_LVALUE_FRAME, .offset = op[i].parked_slot, .align = 8 };

			x64_record_emit_memory(e, X64_OP_LD, op[i].is_float ? op[i].size : 8, op[i].register_number, &s, false);
		}
	/* x87 inputs pushed: st(1) first, then st(0) */
	for (int st = 1; st >= 0; st--)
		for (uint32_t i = 0; i < nops; i++)
			if (op[i].kind == X64_ASM_OPERAND_X87 && (!op[i].is_output || op[i].is_input_output) && op[i].register_number == st) {
				codegen_lvalue m = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = op[i].value.register_number };

				x64_x87(e, X64_X87_LDT, &m, 10);
				x64_tfree(e, op[i].value.register_number);
				break;
			}
	/* the template: {att|intel} alternatives keep the first */
	start = rb->n;
	if (basic) {
		vtext_char_append(rb, '\t');
		for (const char *p = tp; *p; p++) {
			vtext_char_append(rb, *p);
			if (*p == '\n' && p[1])
				vtext_char_append(rb, '\t');
		}
	} else {
		int alt = 0;

		vtext_char_append(rb, '\t');
		for (const char *p = tp; *p; p++) {
			char mod = 0;
			int idx = -1;

			if (*p == '{' && !alt) { alt = 1; continue; }
			if (*p == '|' && alt == 1) { alt = 2; continue; }
			if (*p == '}' && alt) { alt = 0; continue; }
			if (alt == 2)
				continue;
			if (*p != '%') {
				vtext_char_append(rb, *p);
				if (*p == '\n' && p[1])
					vtext_char_append(rb, '\t');
				continue;
			}
			p++;
			if (*p == '%') { vtext_char_append(rb, '%'); continue; }
			if (*p == '=') {
				vtext_unsigned_append(rb, ((uint64_t)e->top_symbol << 32) | serial);
				continue;
			}
			if (*p == '{' || *p == '|' || *p == '}') { vtext_char_append(rb, *p); continue; }
			if (*p && strchr("bwkqhcPalzVHn", *p) && (p[1] == '[' || (p[1] >= '0' && p[1] <= '9')))
				mod = *p++;
			if (*p == '[') {
				const char *q = strchr(p, ']');
				uint32_t len = q ? (uint32_t)(q - p - 1) : 0;

				for (uint32_t i = 0; i < nops; i++)
					if (op[i].name && vintern_ents[op[i].name].len == len && !memcmp(vintern_ents[op[i].name].s, p + 1, len))
						idx = (int)i;
				if (idx < 0)
					for (uint32_t k = 0; k < a->nlabel; k++) {
						const label *lb = &c->labels[a->labels[k]];

						if (vintern_ents[lb->name].len == len && !memcmp(vintern_ents[lb->name].s, p + 1, len))
							idx = (int)(nops + nplus + k);
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
			if (mod == 'l') {
				uint32_t k = (uint32_t)idx - nops - nplus;

				if (k >= a->nlabel)
					codegen_fatal_report(e, x->loc, "invalid operand number %d in inline asm string", idx);
				x64_asm_label_append(e, rb, edges[k]);
				continue;
			}
			if ((uint32_t)idx >= nops) {
				uint32_t extra = (uint32_t)idx - nops;
				idx = -1;
				for (uint32_t j = 0; j < a->nout; j++)
					if (op[j].is_input_output && !extra--) { idx = (int)j; break; }
				if (idx < 0) codegen_fatal_report(e, x->loc, "invalid inline asm operand number");
			}
			{
				x64_asm_operand *o = &op[idx];

				if (o->tied_output >= 0)
					o = &op[o->tied_output];
				switch (o->kind) {
				case X64_ASM_OPERAND_REGISTER: {
					int sz = mod == 'b' || mod == 'h' ? 1 : mod == 'w' ? 2 : mod == 'k' ? 4 : mod == 'q' ? 8 :
						 o->size > 8 || o->size < 1 ? 8 : o->size == 3 ? 4 : o->size > 4 ? 8 : o->size;

					if (mod == 'a') {
						vtext_char_append(rb, '(');
						x64_asm_register_append(rb, o->register_number, 8, false);
						vtext_char_append(rb, ')');
					} else if (mod == 'z') {
						vtext_char_append(rb, sz == 8 ? 'q' : sz == 4 ? 'l' : sz == 2 ? 'w' : 'b');
					} else {
						x64_asm_register_append(rb, o->register_number, sz, mod == 'h');
					}
					break;
				}
				case X64_ASM_OPERAND_MEMORY:
					x64_asm_memory_append(c, rb, &o->lvalue, X->winframe && e->walk_state->frame_alignment <= 16);
					break;
				case X64_ASM_OPERAND_X87:
					vtext_string_append(rb, o->register_number ? "%st(1)" : "%st");
					break;
				case X64_ASM_OPERAND_IMMEDIATE:
					if (mod != 'c' && mod != 'P' && mod != 'a' && mod != 'n')
						vtext_char_append(rb, '$');
					if (o->value.kind == CODEGEN_VALUE_ADDRESS)
						codegen_reference_offset_append(c, rb, o->value.reference, (int64_t)o->value.integer), mod == 'a' ? vtext_string_append(rb, "(%rip)") : (void)0;	/* (clang: %a of a symbol is RIP-relative) */
					else
						vtext_signed_append(rb, mod == 'n' ? (int64_t)(0 - (uint64_t)o->value.integer) : (int64_t)o->value.integer);
					break;
				}
			}
		}
	}
	/* flag outputs: the condition as 0 or 1 in the output's register */
	for (uint32_t i = 0; i < a->nout; i++)
		if (ccop[i]) {
			if (op[i].kind != X64_ASM_OPERAND_REGISTER || op[i].fixed)
				codegen_fatal_report(e, x->loc, "x86-64: unsupported asm flag output");
			if (rb->n > start && rb->p[rb->n - 1] != '\n')
				vtext_char_append(rb, '\n');
			vtext_string_append(rb, "\tset");
			vtext_string_append(rb, ccop[i]);
			vtext_char_append(rb, ' ');
			x64_asm_register_append(rb, op[i].register_number, 1, false);
			vtext_string_append(rb, "\n\tmovzbl ");
			x64_asm_register_append(rb, op[i].register_number, 1, false);
			vtext_string_append(rb, ", ");
			x64_asm_register_append(rb, op[i].register_number, 4, false);
		}
	ri = x64_record_emit(e, X64_OP_RAW);
	ri->offset = (int32_t)start;
	ri->reference = (uint32_t)(rb->n - start);
	ri->condition = !!(fixed & (1u << R12));
	/* the x87 stack after the asm: its outputs from st(0) up, then the inputs it did not pop
	   (one in st(1) is popped by the asm when it clobbers st(1), as GCC's rules) */
	{
		int depth = 0, popped = 0;
		bool in0 = false, in1 = false, out0 = false, out1 = false;

		for (uint32_t i = 0; i < nops; i++)
			if (op[i].kind == X64_ASM_OPERAND_X87) {
				if (op[i].is_output && op[i].register_number)
					out1 = true;
				else if (op[i].is_output)
					out0 = true;
				else if (op[i].register_number)
					in1 = true;
				else
					in0 = true;
			}
		for (uint32_t i = 0; i < a->nclob; i++) {
			const char *cl = c->strs[a->clob[i] - 1].s;

			if (!strcmp(cl, "st(1)"))
				popped |= 2;
			if (!strcmp(cl, "st") || !strcmp(cl, "st(0)"))
				popped |= 1;
		}
		depth = (in0 || out0 ? 1 : 0) + (in1 || out1 ? 1 : 0);
		if (in1 && !out1 && (popped & 2))
			depth--;
		for (int st = 0; st < 2 && depth > 0; st++) {
			for (uint32_t i = 0; i < a->nout; i++)
				if (op[i].kind == X64_ASM_OPERAND_X87 && op[i].register_number == st) {
					int sz = walk_type_size(c, op[i].type);
					codegen_lvalue l = op[i].lvalue;

					if (KIND(c, op[i].type) == AST_TYPE_LONG_DOUBLE)
						x64_x87(e, X64_X87_STPT, &l, 10);
					else
						x64_x87(e, X64_X87_STP, &l, sz);
					depth--;
					goto next;
				}
			x64_x87(e, X64_X87_POP, NULL, 0);
			depth--;
		next:;
		}
	}
	uint32_t asm_live = X->temporaries_in_use;
	/* Register inputs are dead after the template. Output
	   writeback can reuse them while all output values stay live. */
	uint32_t writeback_live = asm_live;
	for (uint32_t i = a->nout; i < nops; i++) {
		if (op[i].kind == X64_ASM_OPERAND_REGISTER && op[i].tied_output < 0 && !op[i].is_stack_pointer)
			writeback_live &= ~(1u << op[i].register_number);
	}
	for (uint32_t i = 0; i < a->nout; i++)
		if (op[i].kind == X64_ASM_OPERAND_REGISTER) writeback_live |= 1u << op[i].register_number;
	for (uint32_t edge = 0; edge <= a->nlabel; edge++) {
		X->temporaries_in_use = writeback_live;
		if (edge) x64_label(e, edges[edge - 1]);
		/* The prepass keeps every output address-backed (lnoreg). Its
		   address was evaluated after fixed registers were reserved, so it
		   uses an independent temporary, never a persistent home. Store the
		   physical result before restoring any overlapping saved home. */
		for (uint32_t i = 0; i < a->nout; i++) {
			x64_asm_operand *o = &op[i];

			if (o->kind != X64_ASM_OPERAND_REGISTER || o->is_stack_pointer)
				continue;
			if (o->address_parked_slot) {
				codegen_lvalue s = { .kind = CODEGEN_LVALUE_FRAME, .offset = o->address_parked_slot, .align = 8 };

				o->lvalue.register_number = (uint8_t)x64_treg(e);
				x64_record_emit_memory(e, X64_OP_LD, 8, o->lvalue.register_number, &s, false);
			}
			if (o->lvalue.is_bitfield) {
				codegen_value v = { .kind = CODEGEN_VALUE_REGISTER, .register_number = o->register_number };

				(void)v;
				walk_lvalue_store_register(e, &o->lvalue, o->type, o->register_number);
			} else {
				x64_store(e, o->register_number, &o->lvalue, o->size);
			}
			if (o->address_parked_slot) x64_tfree(e, o->lvalue.register_number);
		}
		for (int r = 0; r < 32; r++)
			if (saved & (1u << r)) {
				codegen_lvalue s = { .kind = CODEGEN_LVALUE_FRAME, .offset = sslot[r], .align = 8 };

				x64_record_emit_memory(e, X64_OP_LD, r >= XMM0 ? 16 : 8, r, &s, false);
			}
		if (edge) walk_goto_emit(e, a->labels[edge - 1]);
		else if (done) x64_jump(e, done);
	}
	if (done) x64_label(e, done);
	X->temporaries_in_use = asm_live;
	/* temporaries given back, the saved ones restored */
	for (uint32_t i = 0; i < nops; i++) {
		x64_asm_operand *o = &op[i];

		if (o->kind == X64_ASM_OPERAND_REGISTER && !o->fixed && o->tied_output < 0)
			x64_tfree(e, o->register_number);
		if (o->kind == X64_ASM_OPERAND_X87 && o->is_output && o->lvalue.kind == CODEGEN_LVALUE_REGISTER)
			x64_tfree(e, o->lvalue.register_number);
		if (o->is_output && o->lvalue.kind == CODEGEN_LVALUE_REGISTER && !o->lvalue.is_bitfield && !o->address_parked_slot)
			x64_tfree(e, o->lvalue.register_number);
		if (!o->is_output && o->kind == X64_ASM_OPERAND_MEMORY && o->lvalue.kind == CODEGEN_LVALUE_REGISTER)
			x64_tfree(e, o->lvalue.register_number);
		if (o->is_output && o->lvalue.is_bitfield && o->lvalue.kind == CODEGEN_LVALUE_REGISTER)
			x64_tfree(e, o->lvalue.register_number);
	}
	X->temporaries_in_use &= ~reserved;
	X->asm_callee_saved = false;

}
