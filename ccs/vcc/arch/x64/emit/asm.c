/* vcc code generator, x86-64: inline asm in AT&T syntax (for -S) */
#include "x64impl.h"

/*
 * An operand: in a register (a temporary, or a fixed one a constraint or
 * a register variable names), in memory (an lvalue, or a slot holding an
 * input's value), or an immediate.  Fixed registers and clobbers are
 * reserved before any operand is evaluated: a temporary that is live in
 * one (the enclosing expression's) is saved to a slot and restored after.
 */
enum { XO_REG, XO_MEM, XO_IMM, XO_X87 };	/* XO_X87: on the x87 stack, reg 0 st(0) (t), 1 st(1) (u) */

typedef struct xopnd {
	uint8_t kind, reg, out, inout, flt, fixed, sp;	/* sp: register ... asm("rsp") (ASM_CALL_CONSTRAINT): %rsp itself, nothing moved */
	int8_t tied;		/* an input: the output it shares a register with, -1 none */
	int size;		/* the operand's size in bytes (register names) */
	val v;			/* XO_IMM: the value; an input's value until it is placed */
	lval l;			/* XO_MEM: the memory; outputs: where the result goes */
	int32_t park;		/* a fixed input's value parked in this slot (0: none) */
	int32_t lpark;		/* a register output's address parked in this slot until the store (0: none) */
	qty t;
	uint32_t name;
} xopnd;

/* the register a constraint letter names, -1 for none */
static int x64_creg(int ch)
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
static int x64_rname(const char *s)
{
	static const char *const n64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" };
	static const char *const n32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
	static const char *const n16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
	static const char *const n8[] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil" };

	if (*s == '%')
		s++;
	for (int i = 0; i < 8; i++)
		if (!strcmp(s, n64[i]) || !strcmp(s, n32[i]) || !strcmp(s, n16[i]) || !strcmp(s, n8[i]))
			return i;
	if (!strcmp(s, "ah"))
		return RAX;
	if (!strcmp(s, "bh"))
		return RBX;
	if (!strcmp(s, "ch"))
		return RCX;
	if (!strcmp(s, "dh"))
		return RDX;
	if (s[0] == 'r' && s[1] >= '0' && s[1] <= '9') {
		int r = atoi(s + 1);

		return r >= 8 && r <= 15 ? r : -1;
	}
	if (!strncmp(s, "xmm", 3)) {
		int r = atoi(s + 3);

		return r >= 0 && r <= 15 ? XMM0 + r : -1;
	}
	return -1;
}

/* a register's name for an operand of size bytes; h: the high byte (ah ...) */
static void x64_asmreg(tbuf *b, int r, int size, bool h)
{
	static const char *const n64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" };
	static const char *const n32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
	static const char *const n16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
	static const char *const n8[] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil" };
	static const char *const nh[] = { "ah", "ch", "dh", "bh" };

	tb_c(b, '%');
	if (r >= XMM0) {
		tb_s(b, "xmm");
		tb_u(b, (uint64_t)(r - XMM0));
		return;
	}
	if (h && r < 4) {
		tb_s(b, nh[r]);
		return;
	}
	if (r < 8) {
		tb_s(b, size == 8 ? n64[r] : size == 4 ? n32[r] : size == 2 ? n16[r] : n8[r]);
		return;
	}
	tb_c(b, 'r');
	tb_u(b, (uint64_t)r);
	if (size != 8)
		tb_c(b, size == 4 ? 'd' : size == 2 ? 'w' : 'b');
}

/* a local label of this function, as print.c names it */
static void x64_asmlabel(ecx *e, tbuf *b, uint32_t l)
{
	tb_s(b, ".LBB");
	tb_u(b, e->top);
	tb_c(b, '_');
	tb_u(b, l);
}

/* a memory operand as AT&T text */
static void x64_asmmem(cc *c, tbuf *b, const lval *l)
{
	if (l->k == L_SYM) {
		ref_off(c, b, l->ref, l->off);
		tb_s(b, "(%rip)");
		return;
	}
	if (l->off)
		tb_i(b, l->off);
	tb_c(b, '(');
	x64_asmreg(b, l->k == L_FRAME ? RBP : l->r, 8, false);
	tb_c(b, ')');
}

/* a float value as an x87 long double object's address (fld of it) */
static val wk_x87val(ecx *e, val v, qty t)
{
	return wk_conv(e, v, t, prim(TY_LDOUBLE));
}

static bool x64_immok(int ch, i128 v)
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

/* a callee-saved register the asm uses: saved by the prologue */
VPRIV void x64_csused(ecx *e, int r)
{
	xst *X = e->x64;

	if (r == RBX || (r >= R12 && r <= R15)) {
		if (r == RBX && e->wk->falign > 16)
			efatal(e, e->curloc, "x86-64: asm using rbx in a function with locals aligned above 16 is not supported");
		if (!X->csslot[r])
			X->csslot[r] = wk_slot(e, 8, 8);
	}
}

VPRIV void x64_asm(ecx *e, uint32_t n)
{
	cc *c = e->c;
	xst *X = e->x64;
	const node *x = N(n);
	const casm *a = &c->asms[x->a];
	const char *tp = c->strs[a->tmpl - 1].s;
	uint32_t nops = a->nout + a->nin;
	bool basic = nops == 0 && a->nclob == 0 && a->nlabel == 0;
	uint32_t fixed = 0, saved = 0, reserved = 0;
	int32_t sslot[32] = { 0 };
	xopnd *op;
	tbuf *rb = &e->raw;
	size_t start;
	rec *ri;

	if (nops > 64 || a->nlabel > 64)
		efatal(e, x->loc, "too many asm operands");
	if (a->nlabel)
		for (uint32_t i = 0; i < a->nout; i++) {
			const char *cs = c->strs[a->ops[i].constraint - 1].s;

			if (strpbrk(cs, "rqQRgabcdSDxyl"))
				efatal(e, x->loc, "x86-64: asm goto with register outputs is not supported yet (PLAN-x86.md X6)");
		}
	op = alloca((nops + 1) * sizeof(xopnd));
	memset(op, 0, (nops + 1) * sizeof(xopnd));
	/* the fixed registers: clobbers and register constraints */
	for (uint32_t i = 0; i < a->nclob; i++) {
		int r = x64_rname(c->strs[a->clob[i] - 1].s);

		if (r >= 0) {
			fixed |= 1u << r;
			x64_csused(e, r);
		}
	}
	for (uint32_t i = 0; i < nops; i++) {
		const char *cs = c->strs[a->ops[i].constraint - 1].s;
		uint32_t en = a->ops[i].expr;
		int r = -1;

		for (const char *p = cs; *p && r < 0; p++)
			r = x64_creg(*p);
		if (r < 0 && N(en)->op == N_VAR && sym_attrs(c, N(en)->a)->asmname && (c->syms[N(en)->a].sc == SC_REGISTER))
			r = x64_rname(c->strs[sym_attrs(c, N(en)->a)->asmname - 1].s);	/* register long x asm("rdx") */
		if (r == RSP && N(en)->op == N_VAR && c->syms[N(en)->a].sc == SC_REGISTER) {
			op[i].fixed = op[i].sp = 1;
			op[i].reg = RSP;
			continue;
		}
		if (r >= 0) {
			op[i].fixed = 1;
			op[i].reg = (uint8_t)r;
			fixed |= 1u << r;
			x64_csused(e, r);
		}
	}
	if (fixed & ((1u << RSP) | (1u << RBP)))
		efatal(e, x->loc, "x86-64: asm clobbering rsp or rbp is not supported");
	/* temporaries live in them now: saved, and held so evaluation does not use them */
	for (int r = 0; r < 32; r++)
		if (fixed & (1u << r)) {
			if (X->live & (1u << r)) {
				lval s = { .k = L_FRAME, .off = sslot[r] = wk_slot(e, r >= XMM0 ? 16 : 8, 16), .align = 8 };

				x64_mem(e, X_ST, 8, r, &s, false);
				saved |= 1u << r;
			} else {
				reserved |= 1u << r;
			}
			X->live |= 1u << r;
		}
	X->asmcs = true;	/* (operands may take callee-saved registers from here) */
	/* outputs: lvalues; a register's value read first for + */
	for (uint32_t i = 0; i < a->nout; i++) {
		const char *cs = c->strs[a->ops[i].constraint - 1].s;
		uint32_t en = a->ops[i].expr;
		xopnd *o = &op[i];
		bool mem_ok = strpbrk(cs, "mogV") != NULL, reg_ok = strpbrk(cs, "rqQRgabcdSDxyl") != NULL || o->fixed;

		o->out = 1;
		o->tied = -1;
		o->inout = strchr(cs, '+') != NULL;
		o->t = N(en)->ty;
		o->name = a->ops[i].name;
		o->size = wk_size(c, o->t);
		if (o->sp) {
			o->kind = XO_REG;
			continue;
		}
		o->flt = strpbrk(cs, "xy") != NULL && is_flt(c, o->t);
		o->l = wk_lval(e, en);
		if (strpbrk(cs, "tu") && is_flt(c, o->t)) {
			o->kind = XO_X87;
			o->reg = strchr(cs, 'u') ? 1 : 0;
			continue;
		}
		if (mem_ok && !reg_ok) {
			o->kind = XO_MEM;
			if (o->l.bf)
				efatal(e, x->loc, "x86-64: a bit-field as an asm memory operand");
			continue;
		}
		o->kind = XO_REG;
		if (o->inout)
			o->v = wk_reload(e, &o->l, o->t);
		if (o->l.k == L_REG && !o->l.bf) {
			/* the address waits in a slot: four outputs through pointers and four fixed
			   registers (cpuid) would leave no temporaries */
			lval s = { .k = L_FRAME, .off = o->lpark = wk_slot(e, 8, 8), .align = 8 };

			x64_mem(e, X_ST, 8, o->l.r, &s, false);
			x64_tfree(e, o->l.r);
		}
	}
	/* inputs */
	for (uint32_t i = a->nout; i < nops; i++) {
		const char *cs = c->strs[a->ops[i].constraint - 1].s;
		uint32_t en = a->ops[i].expr;
		xopnd *o = &op[i];
		bool reg_ok = strpbrk(cs, "rqQRgabcdSDxyl") != NULL || o->fixed, mem_ok = strpbrk(cs, "mogV") != NULL;
		int tied = -1;
		val v;

		o->t = N(en)->ty;
		o->name = a->ops[i].name;
		o->size = wk_size(c, o->t);
		o->flt = strpbrk(cs, "xy") != NULL && is_flt(c, o->t);
		o->tied = -1;
		if (o->sp) {
			o->kind = XO_REG;
			continue;
		}
		for (const char *p = cs; *p; p++)
			if (*p >= '0' && *p <= '9')
				tied = atoi(p);
		if (tied >= 0 && (uint32_t)tied < a->nout && op[tied].kind == XO_X87) {
			o->kind = XO_X87;
			o->reg = op[tied].reg;
			o->tied = (int8_t)tied;
			o->v = wk_x87val(e, wk_gen(e, en), o->t);
			continue;
		}
		if (strpbrk(cs, "tu") && is_flt(c, o->t)) {
			o->kind = XO_X87;
			o->reg = strchr(cs, 'u') ? 1 : 0;
			o->v = wk_x87val(e, wk_gen(e, en), o->t);
			continue;
		}
		if (tied >= 0 && (uint32_t)tied < a->nout) {
			/* the output's register, holding this value before the template */
			xopnd *to = &op[tied];

			v = wk_gen(e, en);
			if (to->inout)
				wk_free(e, &to->v);
			to->v = v;
			to->inout = 1;
			o->kind = XO_REG;
			o->tied = (int8_t)tied;
			continue;
		}
		if (mem_ok && !reg_ok && !strpbrk(cs, "inIJKLMNeZ")) {
			if (N(en)->op == N_CAST && N(en)->aux == CK_DECAY)
				en = N(en)->a;	/* an array as a memory operand: the array itself */
			if (N(en)->flags & NF_LVALUE || N(en)->op == N_VAR || N(en)->op == N_DEREF || N(en)->op == N_MEMBER ||
			    N(en)->op == N_INDEX) {
				o->kind = XO_MEM;
				o->l = wk_lval(e, en);
				continue;
			}
		}
		{
			cval cv;

			/* an integer constant expression as the front end folds it (__builtin_clzll(63), ...) */
			if (is_integer(e->c, N(en)->ty) && cc_eval(e->c, en, &cv) && cv.kind == CV_INT)
				v = vconst(em_trunc_to(e->c, cv.i, N(en)->ty));
			else
				v = wk_gen(e, en);
		}
		if (v.k == V_CONST || (v.k == V_ADDR && !e->c->o->pic)) {
			bool imm = false;

			for (const char *p = cs; *p; p++)
				if (v.k == V_ADDR ? (*p == 'i' || *p == 'g' || *p == 's') : x64_immok(*p, v.i))
					imm = true;
			if (imm) {
				o->kind = XO_IMM;
				o->v = v;
				if (v.k == V_ADDR)
					note_ref(e, v.ref);
				continue;
			}
		}
		if (!reg_ok) {
			if (!mem_ok)
				efatal(e, x->loc, "impossible constraint in asm: '%s'", cs);
			/* a value for a memory operand: in a slot */
			o->kind = XO_MEM;
			o->l = (lval){ .k = L_FRAME, .off = wk_slot(e, o->size > 8 ? (uint64_t)o->size : 8, 16), .align = 16 };
			if (wk_mem(e, o->t)) {
				v = wk_putmem(e, &o->l, o->t, v);
				wk_free(e, &v);
			} else {
				int r = wk_reg(e, &v, o->t);

				x64_store(e, r, &o->l, o->size);
				x64_tfree(e, r);
			}
			continue;
		}
		o->kind = XO_REG;
		if (wk_mem(e, o->t))
			efatal(e, x->loc, "x86-64: an asm register operand of this type is not supported");
		if (o->fixed) {
			/* parked until the fixed registers are loaded, last */
			lval s = { .k = L_FRAME, .off = o->park = wk_slot(e, 8, 8), .align = 8 };
			int r = wk_reg(e, &v, o->t);

			x64_mem(e, X_ST, 8, r, &s, false);
			x64_tfree(e, r);
		} else {
			wk_reg(e, &v, o->t);
			o->reg = v.r;
			o->v = v;
		}
	}
	/* output registers */
	for (uint32_t i = 0; i < a->nout; i++) {
		xopnd *o = &op[i];

		if (o->kind != XO_REG || o->sp)
			continue;
		if (o->fixed) {
			if (o->inout) {
				lval s = { .k = L_FRAME, .off = o->park = wk_slot(e, 8, 8), .align = 8 };
				int r = wk_reg(e, &o->v, o->t);

				x64_mem(e, X_ST, 8, r, &s, false);
				x64_tfree(e, r);
				o->v = (val){ 0 };
			}
			continue;
		}
		if (o->inout) {
			wk_reg(e, &o->v, o->t);
			o->reg = o->v.r;
		} else {
			o->reg = (uint8_t)(o->flt ? x64_ftreg(e) : x64_treg(e));
			o->v = (val){ .k = V_REG, .r = o->reg };
		}
	}
	/* the fixed registers loaded last: nothing evaluated after can clobber them */
	for (uint32_t i = 0; i < nops; i++)
		if (op[i].park) {
			lval s = { .k = L_FRAME, .off = op[i].park, .align = 8 };

			x64_mem(e, X_LD, 8, op[i].reg, &s, false);
		}
	/* x87 inputs pushed: st(1) first, then st(0) */
	for (int st = 1; st >= 0; st--)
		for (uint32_t i = a->nout; i < nops; i++)
			if (op[i].kind == XO_X87 && op[i].reg == st) {
				lval m = { .k = L_REG, .r = op[i].v.r };

				x64_x87(e, XF_LDT, &m, 10);
				x64_tfree(e, op[i].v.r);
				break;
			}
	/* the template: {att|intel} alternatives keep the first */
	start = rb->n;
	if (basic) {
		tb_c(rb, '\t');
		for (const char *p = tp; *p; p++) {
			tb_c(rb, *p);
			if (*p == '\n' && p[1])
				tb_c(rb, '\t');
		}
	} else {
		int alt = 0;

		tb_c(rb, '\t');
		for (const char *p = tp; *p; p++) {
			char mod = 0;
			int idx = -1;

			if (*p == '{' && !alt) { alt = 1; continue; }
			if (*p == '|' && alt == 1) { alt = 2; continue; }
			if (*p == '}' && alt) { alt = 0; continue; }
			if (alt == 2)
				continue;
			if (*p != '%') {
				tb_c(rb, *p);
				if (*p == '\n' && p[1])
					tb_c(rb, '\t');
				continue;
			}
			p++;
			if (*p == '%') { tb_c(rb, '%'); continue; }
			if (*p == '=') {
				tb_u(rb, (uint64_t)e->top * 1024 + (uint64_t)(e->asmk++ & 1023));
				continue;
			}
			if (*p == '{' || *p == '|' || *p == '}') { tb_c(rb, *p); continue; }
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
							idx = (int)(nops + k);
					}
				if (idx < 0)
					efatal(e, x->loc, "unknown asm operand name '%.*s'", (int)len, p + 1);
				p = q;
			} else if (*p >= '0' && *p <= '9') {
				idx = 0;
				while (p[0] >= '0' && p[0] <= '9')
					idx = idx * 10 + (*p++ - '0');
				p--;
			} else {
				efatal(e, x->loc, "invalid %% escape in inline assembly string");
			}
			if ((uint32_t)idx >= nops) {
				uint32_t k = (uint32_t)idx - nops;

				if (k >= a->nlabel)
					efatal(e, x->loc, "invalid operand number %d in inline asm string", idx);
				x64_asmlabel(e, rb, wk_userlabel(e, a->labels[k]));
				continue;
			}
			{
				xopnd *o = &op[idx];

				if (o->tied >= 0)
					o = &op[o->tied];
				switch (o->kind) {
				case XO_REG: {
					int sz = mod == 'b' || mod == 'h' ? 1 : mod == 'w' ? 2 : mod == 'k' ? 4 : mod == 'q' ? 8 :
						 o->size > 8 || o->size < 1 ? 8 : o->size == 3 ? 4 : o->size > 4 ? 8 : o->size;

					if (mod == 'a') {
						tb_c(rb, '(');
						x64_asmreg(rb, o->reg, 8, false);
						tb_c(rb, ')');
					} else if (mod == 'z') {
						tb_c(rb, sz == 8 ? 'q' : sz == 4 ? 'l' : sz == 2 ? 'w' : 'b');
					} else {
						x64_asmreg(rb, o->reg, sz, mod == 'h');
					}
					break;
				}
				case XO_MEM:
					x64_asmmem(c, rb, &o->l);
					break;
				case XO_X87:
					tb_s(rb, o->reg ? "%st(1)" : "%st");
					break;
				case XO_IMM:
					if (mod != 'c' && mod != 'P' && mod != 'a' && mod != 'n')
						tb_c(rb, '$');
					if (o->v.k == V_ADDR)
						ref_off(c, rb, o->v.ref, (int64_t)o->v.i);
					else
						tb_i(rb, mod == 'n' ? -(int64_t)o->v.i : (int64_t)o->v.i);
					break;
				}
			}
		}
	}
	ri = x64_put(e, X_RAW);
	ri->off = (int32_t)start;
	ri->ref = (uint32_t)(rb->n - start);
	/* the x87 stack after the asm: its outputs from st(0) up, then the inputs it did not pop
	   (one in st(1) is popped by the asm when it clobbers st(1), as GCC's rules) */
	{
		int depth = 0, popped = 0;
		bool in0 = false, in1 = false, out0 = false, out1 = false;

		for (uint32_t i = 0; i < nops; i++)
			if (op[i].kind == XO_X87) {
				if (op[i].out && op[i].reg)
					out1 = true;
				else if (op[i].out)
					out0 = true;
				else if (op[i].reg)
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
				if (op[i].kind == XO_X87 && op[i].reg == st) {
					int sz = wk_size(c, op[i].t);
					lval l = op[i].l;

					if (KIND(c, op[i].t) == TY_LDOUBLE)
						x64_x87(e, XF_STPT, &l, 10);
					else
						x64_x87(e, XF_STP, &l, sz);
					depth--;
					goto next;
				}
			x64_x87(e, XF_POP, NULL, 0);
			depth--;
		next:;
		}
	}
	/* outputs stored */
	for (uint32_t i = 0; i < a->nout; i++) {
		xopnd *o = &op[i];

		if (o->kind != XO_REG || o->sp)
			continue;
		if (o->lpark) {
			lval s = { .k = L_FRAME, .off = o->lpark, .align = 8 };

			o->l.r = (uint8_t)x64_treg(e);
			x64_mem(e, X_LD, 8, o->l.r, &s, false);
		}
		if (o->l.bf) {
			val v = { .k = V_REG, .r = o->reg };

			(void)v;
			wk_put(e, &o->l, o->t, o->reg);
		} else {
			x64_store(e, o->reg, &o->l, o->size);
		}
	}
	/* temporaries given back, the saved ones restored */
	for (uint32_t i = 0; i < nops; i++) {
		xopnd *o = &op[i];

		if (o->kind == XO_REG && !o->fixed && o->tied < 0)
			x64_tfree(e, o->reg);
		if (o->kind == XO_X87 && o->out && o->l.k == L_REG)
			x64_tfree(e, o->l.r);
		if (o->out && o->l.k == L_REG && !o->l.bf)
			x64_tfree(e, o->l.r);
		if (!o->out && o->kind == XO_MEM && o->l.k == L_REG)
			x64_tfree(e, o->l.r);
		if (o->out && o->l.bf && o->l.k == L_REG)
			x64_tfree(e, o->l.r);
	}
	X->live &= ~reserved;
	X->asmcs = false;
	for (int r = 0; r < 32; r++)
		if (saved & (1u << r)) {
			lval s = { .k = L_FRAME, .off = sslot[r], .align = 8 };

			x64_mem(e, X_LD, 8, r, &s, false);
		}
}
