/* x86 GNU extended asm. Operand evaluation is separate from register placement:
   every expression is evaluated once, then frame slots break allocation cycles. */
#include "x64impl.h"

typedef struct xasmop {
	int reg, tie, size;
	uint32_t allowed;
	int32_t slot, addr;
	bool memory, immediate, read;
	int64_t imm;
} xasmop;

#define X_ASM_REGS (0xffffu & ~((1u << RSP) | (1u << RBP)))
#define X_ASM_ALLOC (X_ASM_REGS & ~(1u << R12))

static int x64_asm_name(const casm *a, const char *s, size_t n)
{
	for (uint32_t i = 0; i < a->nout + a->nin; i++) {
		uint32_t id = a->ops[i].name;
		if (id && vintern_ents[id].len == n && !memcmp(vintern_ents[id].s, s, n))
			return (int)i;
	}
	return -1;
}

static int x64_asm_index(ecx *e, const casm *a, const char **sp)
{
	const char *s = *sp;
	int n = 0;

	if (*s == '[') {
		const char *start = ++s;
		while (*s && *s != ']') s++;
		n = x64_asm_name(a, start, (size_t)(s - start));
		if (*s == ']') s++;
		else n = -1;
	} else if (*s >= '0' && *s <= '9') {
		do { n = n * 10 + *s++ - '0'; } while (*s >= '0' && *s <= '9' && n < 1000);
	} else n = -1;
	if (n >= 0 && (uint32_t)n >= a->nout + a->nin) {
		/* Read/write outputs also have implicit input operand numbers. */
		unsigned extra = (unsigned)n - a->nout - a->nin;
		n = -1;
		for (uint32_t i = 0; i < a->nout; i++) {
			if (e->c->strs[a->ops[i].constraint - 1].s[0] != '+') continue;
			if (!extra--) { n = (int)i; break; }
		}
	}
	if (n < 0 || (uint32_t)n >= a->nout + a->nin)
		efatal(e, e->curloc, "invalid inline asm operand reference");
	*sp = s;
	return n;
}

/* Try one common alternative across every operand. No expression is evaluated
   until a complete, allocatable alternative has been selected. */
static bool x64_asm_constraint(ecx *e, const casm *a, uint32_t i, xasmop *o, unsigned alt)
{
	const char *original = e->c->strs[a->ops[i].constraint - 1].s, *s = original;
	char constraint[128];
	bool reg = false, mem = false, imm = false;

	o->tie = -1, o->reg = -1;
	o->read = i >= a->nout || *s == '+';
	if (i < a->nout && *s != '=' && *s != '+') return false;
	for (unsigned k = 0; k < alt; k++) {
		s = strchr(s, ',');
		if (!s) return false;
		s++;
	}
	size_t len = strcspn(s, ",");
	if (len >= sizeof constraint) return false;
	memcpy(constraint, s, len); constraint[len] = 0; s = constraint;
	if (*s == '=' || *s == '+') s++;
	while (*s == '&' || *s == '%' || *s == '?' || *s == '!' || *s == '*') s++;
	if ((*s >= '0' && *s <= '9') || *s == '[') {
		o->tie = x64_asm_index(e, a, &s);
		return i >= a->nout && (uint32_t)o->tie < a->nout && !*s;
	}
	for (; *s && *s != '#'; s++) {
		int r = -1;
		switch (*s) {
		case '&': case '%': case '?': case '!': case '*': break;
		case 'r': case 'q': case 'R': reg = true; o->allowed |= *s == 'R' ? 0xcfu : X_ASM_REGS; break;
		case 'Q': reg = true; o->allowed |= 15; break;
		case 'a': r = RAX; break;
		case 'b': r = RBX; break;
		case 'c': r = RCX; break;
		case 'd': r = RDX; break;
		case 'S': r = RSI; break;
		case 'D': r = RDI; break;
		case 'm': case 'o': mem = true; break;
		case 'i': case 'n': imm = true; break;
		case 'g': case 'X': reg = mem = true; o->allowed |= X_ASM_REGS; break;
		default: return false;
		}
		if (r >= 0) reg = true, o->allowed |= 1u << r;
	}
	if (o->size > 8) reg = false;
	o->memory = mem && !reg;
	o->immediate = imm && !reg && !o->memory;
	if ((!reg && !o->memory && !o->immediate) || (i < a->nout && o->immediate)) return false;
	if (o->immediate) {
		cval cv;
		if (!cc_eval(e->c, a->ops[i].expr, &cv) || cv.kind != CV_INT) return false;
		o->imm = (int64_t)cv.i;
	}
	return true;
}

static lval x64_asm_slot(int32_t off) { return (lval){ .k = L_FRAME, .off = off }; }

/* The scalar lowering keeps no live values on the x87 stack between records.
   Stage the operands in memory, populate st(1) before st(0), then empty the
   remaining stack after the asm. Clobbered input stack registers are popped
   by the template, per GCC's x87 operand rules. */
static bool x64_asm_x87(ecx *e, const casm *a)
{
	cc *c = e->c;
	uint32_t count = a->nout + a->nin;
	bool found = false, input[2] = { false }, output[2] = { false };
	int position[64], in_slot[2] = { 0 }, out_addr[2] = { 0 }, out_size[2] = { 0 };
	unsigned popped = 0;

	for (uint32_t i = 0; i < count; i++) {
		const char *s = c->strs[a->ops[i].constraint - 1].s;
		if (*s == '=' || *s == '+') s++;
		if (*s == '&') s++;
		found |= ((*s == 't' || *s == 'u') && !s[1]);
	}
	if (!found) return false;
	if (count > 64 || a->nlabel)
		efatal(e, e->curloc, "unsupported x87 inline asm operand count or goto");
	for (uint32_t i = 0; i < a->nclob; i++) {
		const char *s = c->strs[a->clob[i] - 1].s;
		if (!strcmp(s, "st") || !strcmp(s, "st(0)")) popped |= 1;
		else if (!strcmp(s, "st(1)")) popped |= 2;
		else if (strcmp(s, "cc") && strcmp(s, "memory"))
			efatal(e, e->curloc, "unsupported x87 inline asm clobber '%s'", s);
	}
	for (uint32_t i = 0; i < count; i++) {
		const char *s = c->strs[a->ops[i].constraint - 1].s;
		bool read = i >= a->nout || *s == '+';
		if (*s == '=' || *s == '+') s++;
		if (*s == '&') s++;
		int p;
		if ((*s == 't' || *s == 'u') && !s[1]) p = *s == 'u';
		else if (i >= a->nout && ((*s >= '0' && *s <= '9') || *s == '[')) {
			int tie = x64_asm_index(e, a, &s);
			if (*s || (uint32_t)tie >= a->nout)
				efatal(e, e->curloc, "invalid x87 inline asm matching operand");
			p = position[tie];
		} else efatal(e, e->curloc, "unsupported mixed x87 inline asm constraints");
		position[i] = p;
		qty ty = N(a->ops[i].expr)->ty;
		if (KIND(c, ty) != TY_FLOAT && KIND(c, ty) != TY_DOUBLE && KIND(c, ty) != TY_LDOUBLE)
			efatal(e, e->curloc, "x87 inline asm operand must have scalar floating type");
		if (i < a->nout) {
			if (output[p] || (popped & (1u << p)))
				efatal(e, e->curloc, "conflicting x87 inline asm output");
			output[p] = true;
			lval l = wk_lval(e, a->ops[i].expr);
			int r = x64_laddr(e, &l);
			lval slot = x64_asm_slot(out_addr[p] = wk_slot(e, 8, 8));
			x64_store(e, r, &slot, 8);
			out_size[p] = KIND(c, ty) == TY_LDOUBLE ? 10 : (int)tsize(c, ty);
			if (read) {
				lval src = { .k = L_REG, .r = (uint8_t)r };
				lval tmp = x64_asm_slot(in_slot[p] = wk_slot(e, 16, 16));
				x64_x87(e, out_size[p] == 10 ? XF_LDT : XF_LD, &src, out_size[p]);
				x64_x87(e, XF_STPT, &tmp, 10);
			}
			x64_tfree(e, r);
		} else {
			val v = wk_gen(e, a->ops[i].expr);
			if (KIND(c, ty) != TY_LDOUBLE) v = x64_cvt(e, v, ty, prim(TY_LDOUBLE));
			int r = wk_reg(e, &v, prim(TY_LDOUBLE));
			lval src = { .k = L_REG, .r = (uint8_t)r };
			lval tmp = x64_asm_slot(in_slot[p] = wk_slot(e, 16, 16));
			x64_x87(e, XF_LDT, &src, 10);
			x64_x87(e, XF_STPT, &tmp, 10);
			wk_free(e, &v);
		}
		if (read) {
			if (input[p]) efatal(e, e->curloc, "duplicate x87 inline asm input register");
			input[p] = true;
		}
	}
	if ((input[1] && !input[0]) || (output[1] && !output[0]))
		efatal(e, e->curloc, "x87 inline asm operands must occupy consecutive stack registers");
	for (int p = 1; p >= 0; p--) if (input[p]) {
		lval tmp = x64_asm_slot(in_slot[p]);
		x64_x87(e, XF_LDT, &tmp, 10);
	}
	uint32_t begin = (uint32_t)e->raw.n;
	const char *s = c->strs[a->tmpl - 1].s;
	while (*s) {
		if (*s != '%') { tb_c(&e->raw, *s++); continue; }
		s++;
		if (*s == '%') { tb_c(&e->raw, *s++); continue; }
		if (*s == '=') { tb_u(&e->raw, ((uint64_t)e->top << 32) | e->asmk); s++; continue; }
		int i = x64_asm_index(e, a, &s);
		tb_s(&e->raw, position[i] ? "%st(1)" : "%st");
	}
	e->asmk++;
	rec *raw = x64_put(e, X_RAW);
	raw->off = (int32_t)begin, raw->ref = (uint32_t)e->raw.n - begin;
	for (int p = 0; p < 2; p++) {
		if (output[p]) {
			int r = x64_treg(e);
			lval slot = x64_asm_slot(out_addr[p]);
			x64_load(e, r, &slot, 8, false);
			lval out = { .k = L_REG, .r = (uint8_t)r };
			x64_x87(e, out_size[p] == 10 ? XF_STPT : XF_STP, &out, out_size[p]);
			x64_tfree(e, r);
		} else if (input[p] && !(popped & (1u << p)))
			x64_x87(e, XF_POP, NULL, 0);
	}
	return true;
}

VPRIV void x64_asm(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const casm *a = &c->asms[N(n)->a];
	uint32_t count = a->nout + a->nin, used = 0, save = 0;
	xasmop ops[64] = { 0 };
	int32_t saved[16] = { 0 };

	if (x64_asm_x87(e, a)) return;

	if (count > 64 || a->nlabel > 64)
		efatal(e, e->curloc, "x86 inline asm has more than 64 operands or labels");
	uint32_t edges[64];
	for (uint32_t i = 0; i < a->nlabel; i++) edges[i] = wk_newlabel(e);
	for (uint32_t i = 0; i < a->nclob; i++) {
		const char *name = c->strs[a->clob[i] - 1].s;
		if (!strcmp(name, "memory") || !strcmp(name, "cc")) continue;
		int r = x64_asm_namedreg(name);
		if (r < 0 || r == RSP || r == RBP)
			efatal(e, e->curloc, "unsupported x86 inline asm clobber '%s'", name);
		used |= 1u << r;
	}
	save = used;
	unsigned alternatives = 1;
	for (uint32_t i = 0; i < count; i++) {
		unsigned nalt = 1;
		for (const char *p = c->strs[a->ops[i].constraint - 1].s; *p; p++) nalt += *p == ',';
		if (nalt > alternatives) alternatives = nalt;
	}
	bool selected = false;
	for (unsigned alt = 0; alt < alternatives; alt++) {
		bool valid = true;
		used = save;
		memset(ops, 0, sizeof ops);
		for (uint32_t i = 0; i < count; i++) {
			xasmop *o = &ops[i];
			qty ty = N(a->ops[i].expr)->ty;
			o->size = (int)tsize(c, ty);
			if (!x64_asm_constraint(e, a, i, o, alt)) { valid = false; break; }
			if (!o->memory && o->size != 1 && o->size != 2 && o->size != 4 && o->size != 8)
				{ valid = false; break; }
			uint32_t expr = a->ops[i].expr;
			while (N(expr)->op == N_CAST && N(expr)->aux == CK_NOP) expr = N(expr)->a;
			if (!o->memory && !o->immediate && N(expr)->op == N_VAR) {
				uint32_t sym = N(expr)->a;
				const attrs *at = sym_attrs(c, sym);
				if (c->syms[sym].sc == SC_REGISTER && at->asmname) {
					int fixed = x64_asm_namedreg(c->strs[at->asmname - 1].s);
					uint32_t *allowed = o->tie >= 0 ? &ops[o->tie].allowed : &o->allowed;
					if (fixed < 0 || !(*allowed & (1u << fixed)))
						{ valid = false; break; }
					*allowed = 1u << fixed;
				}
			}
		}
		for (uint32_t i = 0; valid && i < count; i++) if (ops[i].tie >= 0) {
			xasmop *out = &ops[ops[i].tie];
			if (out->memory || out->read) valid = false;
		}
		if (!valid) continue;
		for (uint32_t i = 0; i < count; i++)
			if (ops[i].allowed != (1u << R12)) ops[i].allowed &= ~(1u << R12);
		/* Reserve the most restricted choices first; every untied operand has a
		   distinct register, which also satisfies all early-clobber constraints. */
		for (uint32_t k = 0; k < count; k++) {
			int best = -1, choices = 100;
			for (uint32_t i = 0; i < count; i++) {
				xasmop *o = &ops[i];
				if (o->tie >= 0 || o->immediate || o->reg >= 0) continue;
				uint32_t avail = (o->memory ? X_ASM_ALLOC : o->allowed) & ~used;
				int nc = __builtin_popcount(avail);
				if (nc < choices) best = (int)i, choices = nc;
			}
			if (best < 0) break;
			if (!choices) { valid = false; break; }
			xasmop *o = &ops[best];
			o->reg = __builtin_ctz((o->memory ? X_ASM_ALLOC : o->allowed) & ~used);
			used |= 1u << o->reg;
		}
		if (valid) { selected = true; break; }
	}
	if (!selected)
		efatal(e, e->curloc, "no supported, allocatable x86 inline asm constraint alternative");
	for (uint32_t i = 0; i < count; i++) {
		xasmop *o = &ops[i];
		if (o->tie >= 0) {
			xasmop *out = &ops[o->tie];
			if (out->memory || out->read)
				efatal(e, e->curloc, "unsupported matching inline asm operand");
			o->reg = out->reg;
		}
		uint32_t expr = a->ops[i].expr;
		qty ty = N(expr)->ty;
		if (i < a->nout || o->memory) {
			int r;
			if (i >= a->nout && !(N(expr)->flags & NF_LVALUE)) {
				val v = wk_gen(e, expr);
				int vr = wk_reg(e, &v, ty);
				lval temp = x64_asm_slot(wk_slot(e, (uint64_t)o->size, 8));
				if (is_record(c, ty) || is_vec(c, ty)) {
					r = x64_laddr(e, &temp);
					x64_copy(e, r, vr, (uint64_t)o->size);
				} else {
					x64_store(e, vr, &temp, o->size);
					r = x64_laddr(e, &temp);
				}
				wk_free(e, &v);
			} else {
				lval l = wk_lval(e, expr);
				if (l.bf) efatal(e, e->curloc, "inline asm operand cannot be a bit-field");
				r = x64_laddr(e, &l);
			}
			o->addr = wk_slot(e, 8, 8);
			lval slot = x64_asm_slot(o->addr);
			x64_store(e, r, &slot, 8);
			if (i < a->nout && o->read && !o->memory) {
				lval at = { .k = L_REG, .r = (uint8_t)r };
				x64_load(e, r, &at, o->size, wk_signed(c, ty));
				o->slot = wk_slot(e, 8, 8);
				slot = x64_asm_slot(o->slot);
				x64_store(e, r, &slot, 8);
			}
			x64_tfree(e, r);
		} else {
			val v = wk_gen(e, expr);
			if (o->immediate) {
				/* The selected constraint already proved the constant; still
				   evaluate the expression once to retain any comma side effects. */
				wk_free(e, &v);
			} else {
				int r = wk_reg(e, &v, ty);
				if (is_record(c, ty) || is_vec(c, ty)) {
					lval data = { .k = L_REG, .r = (uint8_t)r };
					x64_load(e, r, &data, o->size, false);
				}
				o->slot = wk_slot(e, 8, 8);
				lval slot = x64_asm_slot(o->slot);
				x64_store(e, r, &slot, IS_XMM(r) ? o->size : 8);
				wk_free(e, &v);
			}
		}
	}
	/* Preserve both enclosing expression temporaries and ABI callee-saves.
	   R11 is printer scratch, so it is not used between placement and asm. */
	save |= used | (e->x64->live & X_ASM_REGS);
	for (int r = 0; r < 16; r++) if (save & (1u << r)) {
		saved[r] = wk_slot(e, 8, 8);
		lval slot = x64_asm_slot(saved[r]);
		x64_store(e, r, &slot, 8);
	}
	for (uint32_t i = 0; i < count; i++) {
		xasmop *o = &ops[i];
		if (o->immediate || (!o->read && !o->memory)) continue;
		lval slot = x64_asm_slot(o->memory ? o->addr : o->slot);
		x64_load(e, o->reg, &slot, 8, false);
	}
	uint32_t begin = (uint32_t)e->raw.n;
	const char *s = c->strs[a->tmpl - 1].s;
	uint32_t serial = e->asmk++;
	while (*s) {
		if (*s != '%' || !a->extended) { tb_c(&e->raw, *s++); continue; }
		s++;
		if (*s == '%') { tb_c(&e->raw, *s++); continue; }
		if (*s == '=') { tb_u(&e->raw, ((uint64_t)e->top << 32) | serial); s++; continue; }
		char mod = 0;
		if (*s >= 'a' && *s <= 'z') mod = *s++;
		if (mod == 'l') {
			int target = -1;
			if (*s == '[') {
				const char *start = ++s;
				while (*s && *s != ']') s++;
				for (uint32_t i = 0; i < a->nlabel; i++) {
					const vident *id = &vintern_ents[c->labels[a->labels[i]].name];
					if (id->len == (size_t)(s - start) && !memcmp(id->s, start, id->len)) target = (int)i;
				}
				if (*s == ']') s++; else target = -1;
			} else if (*s >= '0' && *s <= '9') {
				target = 0;
				do { target = target * 10 + *s++ - '0'; } while (*s >= '0' && *s <= '9' && target < 1000);
				target -= (int)count;
				for (uint32_t i = 0; i < a->nout; i++) target -= ops[i].read;
			}
			if (target < 0 || (uint32_t)target >= a->nlabel)
				efatal(e, e->curloc, "invalid inline asm goto label reference");
			x64_lbl(&e->raw, e->wk->fsym, edges[target]);
			continue;
		}
		int index = x64_asm_index(e, a, &s);
		xasmop *o = &ops[index];
		if (o->immediate) {
			if (mod && mod != 'c' && mod != 'n') efatal(e, e->curloc, "unsupported inline asm immediate modifier");
			if (!mod) tb_c(&e->raw, '$');
			tb_i(&e->raw, mod == 'n' ? (int64_t)(0 - (uint64_t)o->imm) : o->imm);
		} else if (o->memory) {
			if (mod) efatal(e, e->curloc, "unsupported inline asm memory modifier");
			tb_c(&e->raw, '('); x64_reg(&e->raw, o->reg, 8); tb_c(&e->raw, ')');
		} else {
			int size = mod == 'q' ? 8 : mod == 'k' ? 4 : mod == 'w' ? 2 : mod == 'b' ? 1 : o->size;
			if (mod == 'h' && o->reg < 4) {
				static const char *const high[] = { "%ah", "%ch", "%dh", "%bh" };
				tb_s(&e->raw, high[o->reg]);
			} else {
				if (mod && mod != 'q' && mod != 'k' && mod != 'w' && mod != 'b') efatal(e, e->curloc, "unsupported inline asm register modifier");
				x64_reg(&e->raw, o->reg, size);
			}
		}
	}
	rec *raw = x64_put(e, X_RAW);
	raw->off = (int32_t)begin, raw->ref = (uint32_t)e->raw.n - begin;
	raw->cond = (save & (1u << R12)) != 0;
	for (uint32_t i = 0; i < a->nout; i++)
		if (!ops[i].memory) ops[i].slot = wk_slot(e, 8, 8);
	uint32_t done = a->nlabel ? wk_newlabel(e) : 0;
	/* Every asm-goto edge first writes outputs and restores the register set.
	   A normal C goto then performs cleanups for scopes crossed by that edge. */
	for (uint32_t edge = 0; edge <= a->nlabel; edge++) {
		if (edge) x64_label(e, edges[edge - 1]);
		for (uint32_t i = 0; i < a->nout; i++) if (!ops[i].memory) {
			lval slot = x64_asm_slot(ops[i].slot);
			x64_store(e, ops[i].reg, &slot, 8);
		}
		for (int r = 0; r < 16; r++) if (save & (1u << r)) {
			lval slot = x64_asm_slot(saved[r]);
			x64_load(e, r, &slot, 8, false);
		}
		for (uint32_t i = 0; i < a->nout; i++) if (!ops[i].memory) {
			int ptr = x64_treg(e), value = x64_treg(e);
			lval slot = x64_asm_slot(ops[i].addr);
			x64_load(e, ptr, &slot, 8, false);
			slot = x64_asm_slot(ops[i].slot);
			x64_load(e, value, &slot, 8, false);
			lval out = { .k = L_REG, .r = (uint8_t)ptr };
			x64_store(e, value, &out, ops[i].size);
			x64_tfree(e, value); x64_tfree(e, ptr);
		}
		if (edge) wk_goto(e, a->labels[edge - 1]);
		else if (done) x64_jump(e, done);
	}
	if (done) x64_label(e, done);
}
