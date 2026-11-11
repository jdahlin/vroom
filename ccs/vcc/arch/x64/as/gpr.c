/* x86-64 assembler: general-purpose instructions: jumps and calls, setcc/cmovcc, shifts and bit tests, the integer families, movz/movs */
#include "x64as.h"

VPRIV int x64_i_jump(As *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- jumps, calls ---- */
	if (mn[0] == 'j' && nops == 1) {
		const char *c = mn + 1;

		if (!strcmp(mn, "jmp") || !strcmp(mn, "jmpq")) {
			if (o[0].star || o[0].kind == XK_REG || o[0].kind == XK_MEM) {
				o[0].kind = o[0].kind == XK_EXPR ? XK_MEM : o[0].kind;
				return x64_emit(a, &F(.op = OPB(0xff), .nop = 1, .reg = 4, .rm = &o[0], .gotx = 2));
			}
			return x64_branch(a, 16, &o[0], 0);
		}
		if ((cc = x64_cc(c)) >= 0)
			return x64_branch(a, cc, &o[0], 0);
	}
	/* loop/loope/loopne, jrcxz/jecxz: rel8 only (out of range is an error, as MC) */
	if (nops == 1 && o[0].kind == XK_EXPR && !o[0].star) {
		static const struct { const char *n; uint8_t a32, op; } r8[] = {
			{ "loop", 0, 0xe2 }, { "loope", 0, 0xe1 }, { "loopz", 0, 0xe1 }, { "loopne", 0, 0xe0 }, { "loopnz", 0, 0xe0 },
			{ "jrcxz", 0, 0xe3 }, { "jecxz", 1, 0xe3 },
		};
		for (size_t i = 0; i < sizeof r8 / sizeof *r8; i++)
			if (!strcmp(mn, r8[i].n)) {
				uint32_t fr = a->cur;
				uint64_t off;
				uint8_t *p;
				Val v = o[0].v;
				int n = 2 + r8[i].a32;

				elf_code(a->o, fr);
				off = elf_frag(a->o, fr)->size;
				p = elf_grow(a->o, fr, (uint64_t)n);
				if (r8[i].a32)
					p[0] = 0x67;
				p[n - 2] = r8[i].op, p[n - 1] = 0;
				v.c -= 1;
				elf_fixup(a->o, fr, off + (uint64_t)n - 1, p + n - 1, R_X86_64_PC8, v);
				return 0;
			}
	}
	/* ljmp/lcall through memory: ff /5, ff /3 (l: 32-bit offset, q: REX.W) */
	if (nops == 1 && (o[0].star || o[0].kind == XK_MEM) &&
	    (!strcmp(mn, "ljmp") || !strcmp(mn, "ljmpl") || !strcmp(mn, "ljmpq") || !strcmp(mn, "ljmpw") ||
	     !strcmp(mn, "lcall") || !strcmp(mn, "lcalll") || !strcmp(mn, "lcallq") || !strcmp(mn, "lcallw"))) {
		char s = mn[strlen(mn) - 1];

		if (o[0].kind == XK_EXPR)
			o[0].kind = XK_MEM;
		return x64_emit(a, &F(.w = s == 'q', .opsz16 = s == 'w', .op = OPB(0xff), .nop = 1, .reg = mn[1] == 'j' ? 5 : 3, .rm = &o[0]));
	}
	if ((!strcmp(mn, "call") || !strcmp(mn, "callq")) && nops == 1) {
		if (o[0].star || o[0].kind == XK_REG || o[0].kind == XK_MEM) {
			o[0].kind = o[0].kind == XK_EXPR ? XK_MEM : o[0].kind;
			return x64_emit(a, &F(.op = OPB(0xff), .nop = 1, .reg = 2, .rm = &o[0], .gotx = 2));
		}
		return x64_branch(a, -1, &o[0], 1);
	}
	return X64_NEXT;
}

VPRIV int x64_i_setcc(As *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- setcc, cmovcc ---- */
	if (!strncmp(mn, "set", 3) && nops == 1 && (cc = x64_cc(mn + 3)) >= 0) {
		uint8_t op[2] = { 0x0f, (uint8_t)(0x90 + cc) };

		return x64_emit(a, &F(.op = op, .nop = 2, .reg = 0, .rm = &o[0], .rex8 = rex8(&o[0])));
	}
	if (!strncmp(mn, "cmov", 4) && nops == 2) {
		char t[16];
		size_t n = strlen(mn + 4);
		int s2 = 0;

		memcpy(t, mn + 4, n + 1);
		if ((cc = x64_cc(t)) < 0 && n > 1 && sfxsize(t[n - 1])) {
			s2 = sfxsize(t[n - 1]);
			t[n - 1] = 0;
			cc = x64_cc(t);
		}
		if (cc >= 0) {
			uint8_t op[2] = { 0x0f, (uint8_t)(0x40 + cc) };
			int s = s2 ? s2 : opsize(&o[1]);

			return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = op, .nop = 2, .reg = o[1].reg, .rm = &o[0]));
		}
	}
	return X64_NEXT;
}

VPRIV int x64_i_bits(As *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- shld/shrd, bt*, prefetch ---- */
	{
		char st2[32];
		int s2 = 0;

		if (!split_sfx(mn, st2, &s2))
			strcpy(st2, mn);
		if ((!strcmp(st2, "shld") || !strcmp(st2, "shrd")) && nops == 3) {
			int z = s2 ? s2 : opsize(&o[2]), r = st2[2] == 'r';
			XOp *d = &o[2];

			if (d->kind == XK_EXPR)
				d->kind = XK_MEM;
			if (o[0].kind == XK_REG && o[0].reg == 1 && o[0].size == 1) {
				uint8_t ops[2] = { 0x0f, (uint8_t)(r ? 0xad : 0xa5) };

				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = ops, .nop = 2, .reg = o[1].reg, .rm = d));
			}
			{
				int64_t c = 0;
				uint8_t ops[2] = { 0x0f, (uint8_t)(r ? 0xac : 0xa4) };
				XForm f = F(.w = z == 8, .opsz16 = z == 2, .op = ops, .nop = 2, .reg = o[1].reg, .rm = d, .isz = 1);

				x64_imm(a, &o[0], &f, &c);
				f.imm = c;
				return x64_emit(a, &f);
			}
		}
		{
			static const char *const btn[4] = { "bt", "bts", "btr", "btc" };

			for (int k = 0; k < 4; k++)
				if (!strcmp(st2, btn[k]) && nops == 2) {
					int z = s2 ? s2 : opsize(&o[1]) ? opsize(&o[1]) : opsize(&o[0]);

					if (o[1].kind == XK_EXPR)
						o[1].kind = XK_MEM;
					if (o[0].kind == XK_IMM) {
						int64_t c = 0;
						XForm f = F(.w = z == 8, .opsz16 = z == 2, .op = OPB(0x0f, 0xba), .nop = 2, .reg = 4 + k, .rm = &o[1], .isz = 1);

						x64_imm(a, &o[0], &f, &c);
						f.imm = c;
						return x64_emit(a, &f);
					}
					{
						uint8_t ops[2] = { 0x0f, (uint8_t)(0xa3 + 8 * k) };

						return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = ops, .nop = 2, .reg = o[0].reg, .rm = &o[1]));
					}
				}
		}
		if (!strncmp(mn, "prefetch", 8) && nops == 1) {
			static const struct { const char *n; uint8_t op, d; } pf[] = {
				{ "prefetchnta", 0x18, 0 }, { "prefetcht0", 0x18, 1 }, { "prefetcht1", 0x18, 2 }, { "prefetcht2", 0x18, 3 },
				{ "prefetchw", 0x0d, 1 }, { "prefetch", 0x0d, 0 },
			};

			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			for (size_t i = 0; i < sizeof pf / sizeof *pf; i++)
				if (!strcmp(mn, pf[i].n)) {
					uint8_t ops[2] = { 0x0f, pf[i].op };

					return x64_emit(a, &F(.op = ops, .nop = 2, .reg = pf[i].d, .rm = &o[0]));
				}
		}
	}
	return X64_NEXT;
}

VPRIV int x64_i_int(As *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- integer instructions: the full name, then stem + suffix ---- */
	{
		static const char *const alu[] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
		static const char *const sh[] = { "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar" };
		const char *m = mn;
		int k;

		for (int pass = 0; pass < 2; pass++) {
			if (pass) {
				if (!split_sfx(mn, stem, &sz))
					break;
				m = stem;
			}
			for (k = 0; k < 8; k++)
				if (!strcmp(m, alu[k])) {
					for (int i = 0; i < nops; i++)
						if (o[i].kind == XK_EXPR)
							o[i].kind = XK_MEM;
					return x64_alu(a, k, x64_size(a, sz, &o[0], nops > 1 ? &o[1] : &o[0]), o, nops);
				}
			for (k = 0; k < 8; k++)
				if (!strcmp(m, sh[k])) {
					XOp *d = &o[nops - 1];
					int s = x64_size(a, sz, d, d), digit = k == 6 ? 4 : k;

					if (d->kind == XK_EXPR)
						d->kind = XK_MEM;
					if (nops == 1)
						return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = s == 1 ? OPB(0xd0) : OPB(0xd1), .nop = 1,
								  .reg = digit, .rm = d, .rex8 = rex8(d)));
					if (o[0].kind == XK_REG && o[0].reg == 1 && o[0].size == 1)
						return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = s == 1 ? OPB(0xd2) : OPB(0xd3), .nop = 1,
								  .reg = digit, .rm = d, .rex8 = rex8(d)));
					{
						int64_t c;
						Val v = o[0].v;

						if (o[0].kind != XK_IMM || !isconst(a, &v, &c))
							return err(a, "invalid shift count");
						if (c == 1)
							return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = s == 1 ? OPB(0xd0) : OPB(0xd1), .nop = 1,
									  .reg = digit, .rm = d, .rex8 = rex8(d)));
						return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = s == 1 ? OPB(0xc0) : OPB(0xc1), .nop = 1,
								  .reg = digit, .rm = d, .rex8 = rex8(d), .isz = 1, .imm = c));
					}
				}
			if (!strcmp(m, "mov") && nops == 2) {
				XOp *s = &o[0], *d = &o[1];
				int z = x64_size(a, sz, s, d);

				for (int i = 0; i < 2; i++)
					if (o[i].kind == XK_EXPR)
						o[i].kind = XK_MEM;
				if (s->kind == XK_IMM) {
					int64_t c = 0;
					XForm f = F(.w = z == 8, .opsz16 = z == 2, .rex8 = rex8(d), .immsgn = 1);
					int sym = x64_imm(a, s, &f, &c);

					if (d->kind == XK_REG) {
						if (z == 8 && (sym || fits32(c))) {
							f.op = OPB(0xc7), f.nop = 1, f.reg = 0, f.rm = d, f.isz = 4, f.imm = c;
							return x64_emit(a, &f);
						}
						/* b0+r / b8+r with the whole immediate (movabs for 64 bits) */
						f.op = z == 1 ? OPB(0xb0) : OPB(0xb8), f.nop = 1, f.oreg = d->reg + 1;
						f.isz = z, f.imm = c, f.immsgn = 0;
						return x64_emit(a, &f);
					}
					f.op = z == 1 ? OPB(0xc6) : OPB(0xc7), f.nop = 1, f.reg = 0, f.rm = d, f.isz = z == 8 ? 4 : z, f.imm = c;
					return x64_emit(a, &f);
				}
				if (s->kind == XK_REG && s->cls == XR_GPR)
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0x88) : OPB(0x89), .nop = 1, .reg = s->reg,
							      .rm = d, .rex8 = rex8(s) || rex8(d)));
				if (d->kind == XK_REG)
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0x8a) : OPB(0x8b), .nop = 1, .reg = d->reg,
							      .rm = s, .rex8 = rex8(d), .gotx = 2));
			}
			if (!strcmp(m, "movabs") && nops == 2) {
				int64_t c = 0;
				XOp *d = &o[1];
				XForm f = F(.w = 1, .op = OPB(0xb8), .nop = 1, .oreg = d->reg + 1, .isz = 8);

				x64_imm(a, &o[0], &f, &c);
				f.imm = c;
				return x64_emit(a, &f);
			}
			if (!strcmp(m, "lea") && nops == 2) {
				int z = sz ? sz : opsize(&o[1]);

				if (o[0].kind == XK_EXPR)
					o[0].kind = XK_MEM;
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = OPB(0x8d), .nop = 1, .reg = o[1].reg, .rm = &o[0]));
			}
			if (!strcmp(m, "test") && nops == 2) {
				XOp *s = &o[0], *d = &o[1];
				int z = x64_size(a, sz, s, d);

				for (int i = 0; i < 2; i++)
					if (o[i].kind == XK_EXPR)
						o[i].kind = XK_MEM;
				if (s->kind == XK_IMM) {
					int64_t c = 0;
					XForm f = F(.w = z == 8, .opsz16 = z == 2, .rex8 = rex8(d), .immsgn = 1);

					x64_imm(a, s, &f, &c);
					f.imm = c, f.isz = z == 8 ? 4 : z;
					if (d->kind == XK_REG && d->reg == 0 && !d->hi8) {
						f.op = z == 1 ? OPB(0xa8) : OPB(0xa9), f.nop = 1;
						return x64_emit(a, &f);
					}
					f.op = z == 1 ? OPB(0xf6) : OPB(0xf7), f.nop = 1, f.reg = 0, f.rm = d;
					return x64_emit(a, &f);
				}
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0x84) : OPB(0x85), .nop = 1, .reg = s->reg,
						      .rm = d, .rex8 = rex8(s) || rex8(d), .gotx = 2));
			}
			{
				static const struct { const char *n; uint8_t d; } un[] = {
					{ "not", 2 }, { "neg", 3 }, { "mul", 4 }, { "div", 6 }, { "idiv", 7 },
				};

				for (size_t i = 0; i < sizeof un / sizeof *un; i++)
					if (!strcmp(m, un[i].n) && nops == 1) {
						int z = x64_size(a, sz, &o[0], &o[0]);

						if (o[0].kind == XK_EXPR)
							o[0].kind = XK_MEM;
						return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0xf6) : OPB(0xf7), .nop = 1,
								      .reg = un[i].d, .rm = &o[0], .rex8 = rex8(&o[0])));
					}
				if ((!strcmp(m, "inc") || !strcmp(m, "dec")) && nops == 1) {
					int z = x64_size(a, sz, &o[0], &o[0]);

					if (o[0].kind == XK_EXPR)
						o[0].kind = XK_MEM;
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0xfe) : OPB(0xff), .nop = 1,
							      .reg = m[0] == 'd', .rm = &o[0], .rex8 = rex8(&o[0])));
				}
			}
			if (!strcmp(m, "imul")) {
				int z = x64_size(a, sz, &o[0], &o[nops - 1]);

				a->x64_isz = z;
				for (int i = 0; i < nops; i++)
					if (o[i].kind == XK_EXPR)
						o[i].kind = XK_MEM;
				if (nops == 1)
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0xf6) : OPB(0xf7), .nop = 1, .reg = 5, .rm = &o[0]));
				if (nops == 2 && o[0].kind != XK_IMM)
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = OPB(0x0f, 0xaf), .nop = 2, .reg = o[1].reg, .rm = &o[0]));
				{
					/* imul $k, src, dst (or imul $k, dst: src = dst) */
					const XOp *src = nops == 3 ? &o[1] : &o[1], *dst = nops == 3 ? &o[2] : &o[1];
					int64_t c = 0;
					XForm f = F(.w = z == 8, .opsz16 = z == 2, .reg = dst->reg, .rm = src, .immsgn = 1);
					int sym = x64_imm(a, &o[0], &f, &c);

					f.imm = c;
					if (!sym && fits8(c))
						f.op = OPB(0x6b), f.nop = 1, f.isz = 1;
					else
						f.op = OPB(0x69), f.nop = 1, f.isz = z == 2 ? 2 : 4;
					return x64_emit(a, &f);
				}
			}
			if ((!strcmp(m, "push") || !strcmp(m, "pop")) && nops == 1) {
				int pop = m[1] == 'o';

				if (o[0].kind == XK_REG)
					return x64_emit(a, &F(.op = pop ? OPB(0x58) : OPB(0x50), .nop = 1, .oreg = o[0].reg + 1));
				if (o[0].kind == XK_IMM && !pop) {
					int64_t c = 0;
					XForm f = F(.immsgn = 1);
					int sym = x64_imm(a, &o[0], &f, &c);

					f.imm = c;
					if (!sym && fits8(c))
						f.op = OPB(0x6a), f.nop = 1, f.isz = 1;
					else
						f.op = OPB(0x68), f.nop = 1, f.isz = 4;
					return x64_emit(a, &f);
				}
				if (o[0].kind == XK_EXPR)
					o[0].kind = XK_MEM;
				return x64_emit(a, &F(.op = pop ? OPB(0x8f) : OPB(0xff), .nop = 1, .reg = pop ? 0 : 6, .rm = &o[0]));
			}
			if ((!strcmp(m, "bsf") || !strcmp(m, "bsr") || !strcmp(m, "popcnt") || !strcmp(m, "lzcnt") || !strcmp(m, "tzcnt")) && nops == 2) {
				int z = x64_size(a, sz, &o[0], &o[1]);
				uint8_t op = m[0] == 'p' ? 0xb8 : m[0] == 'l' ? 0xbd : m[0] == 't' ? 0xbc : m[2] == 'f' ? 0xbc : 0xbd;
				uint8_t ops[2] = { 0x0f, op };

				if (o[0].kind == XK_EXPR)
					o[0].kind = XK_MEM;
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .rep = m[0] == 'b' ? 0 : 0xf3, .op = ops, .nop = 2, .reg = o[1].reg, .rm = &o[0]));
			}
			if (!strcmp(m, "ud1") && nops == 2) {	/* 0f b9 /r: the kernel's BUG/UBSAN trap with operands */
				int z = x64_size(a, sz, &o[0], &o[1]);

				if (o[0].kind == XK_EXPR)
					o[0].kind = XK_MEM;
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = OPB(0x0f, 0xb9), .nop = 2, .reg = o[1].reg, .rm = &o[0]));
			}
			if (!strcmp(m, "bswap") && nops == 1) {
				int z = x64_size(a, sz, &o[0], &o[0]);

				return x64_emit(a, &F(.w = z == 8, .op = OPB(0x0f, 0xc8), .nop = 2, .oreg = o[0].reg + 1));
			}
			if (!strcmp(m, "xchg") && nops == 2) {
				int z = x64_size(a, sz, &o[0], &o[1]);
				XOp *r = o[0].kind == XK_REG ? &o[0] : &o[1], *x = o[0].kind == XK_REG ? &o[1] : &o[0];

				if (x->kind == XK_EXPR)
					x->kind = XK_MEM;
				if (z != 1 && x->kind == XK_REG && (r->reg == 0 || x->reg == 0)) {
					/* the accumulator form 90+r */
					int other = r->reg == 0 ? x->reg : r->reg;

					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = OPB(0x90), .nop = 1, .oreg = other + 1));
				}
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0x86) : OPB(0x87), .nop = 1, .reg = r->reg, .rm = x,
						      .rex8 = rex8(r) || rex8(x)));
			}
			if ((!strcmp(m, "cmpxchg") || !strcmp(m, "xadd")) && nops == 2) {
				int z = x64_size(a, sz, &o[0], &o[1]);
				uint8_t ops[2] = { 0x0f, (uint8_t)((m[1] == 'm' ? 0xb0 : 0xc0) + (z != 1)) };

				if (o[1].kind == XK_EXPR)
					o[1].kind = XK_MEM;
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = ops, .nop = 2, .reg = o[0].reg, .rm = &o[1], .rex8 = rex8(&o[0])));
			}
		}
	}
	return X64_NEXT;
}

VPRIV int x64_i_movx(As *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- movz/movs: movzbl, movsbq, movslq ... ---- */
	if ((!strncmp(mn, "movz", 4) || !strncmp(mn, "movs", 4)) && nops == 2 && strlen(mn) == 6) {
		int from = sfxsize(mn[4]), to = sfxsize(mn[5]), sx = mn[3] == 's';

		if (from && to) {
			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			if (from == 4 && sx)
				return x64_emit(a, &F(.w = 1, .op = OPB(0x63), .nop = 1, .reg = o[1].reg, .rm = &o[0]));
			{
				uint8_t ops[2] = { 0x0f, (uint8_t)((sx ? 0xbe : 0xb6) + (from == 2)) };

				return x64_emit(a, &F(.w = to == 8, .opsz16 = to == 2, .op = ops, .nop = 2, .reg = o[1].reg, .rm = &o[0], .rex8 = rex8(&o[0])));
			}
		}
	}
	return err(a, "invalid instruction mnemonic '%s'", mn);
	return X64_NEXT;
}
