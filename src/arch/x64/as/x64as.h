/*
 * The x86-64 assembler's shared part: operands, the encoding form and the
 * encoder (asm.c), used by the instruction families, one file each (sys.c,
 * gpr.c, x87.c, sse.c, avx.c).  A family's function returns X64_NEXT for an
 * instruction that is not its own; x64_as_instruction_assemble tries them in order.
 */
#ifndef VCC_X64AS_H
#define VCC_X64AS_H
#include <stdbool.h>
#include "../../../as/asimpl.h"

#include "operand.h"

typedef struct {
	uint8_t b[24];
	int n;
	int fixat, fixtype, fixsize;	/* one fixup (the displacement or the immediate) */
	as_value fixv;
	int fix2at, fix2type;	/* a second one: an immediate symbol after a displacement */
	as_value fix2v;
	int x87;		/* (unused) */
} XEnc;

/*
 * prefixes, REX, the opcode bytes, ModRM (+ SIB, displacement) for reg field
 * `reg` (a register number or /digit) and the r/m operand rm, then an
 * immediate of isz bytes.  w: REX.W; opsz16: 0x66.  gotx: 2 for mov-like
 * instructions whose GOTPCREL relaxes (REX_)GOTPCRELX, 1 for others that do.
 */
typedef struct {
	int w, opsz16, rep;		/* rep: a mandatory F2/F3 prefix */
	const uint8_t *op; int nop;
	int reg;			/* ModRM.reg (0..15) */
	const XOp *rm;			/* register or memory */
	int rex8;			/* an 8-bit register operand that needs REX (spl ...) */
	int64_t imm; int isz; as_value immv; int immsym, immsgn, immmod;
	int gotx;
	int oreg;			/* a register in the last opcode byte, + 1 (0: none) */
	int vex, vvvv, vl;		/* VEX: the second source (0..15), 256-bit; the map from op (0f, 0f 38, 0f 3a) */
} XForm;
#define F(...) ((XForm){ __VA_ARGS__ })

#define OPB(...) ((const uint8_t[]){ __VA_ARGS__ })
enum { X64_NEXT = -2 };	/* a family's answer: not one of mine */

VPRIV void eb(XEnc *x, int v);
VPRIV void ew(XEnc *x, int64_t v, int n);
VPRIV int fits8(int64_t v);
VPRIV int fits32(int64_t v);
VPRIV int isconst(as_context *a, as_value *v, int64_t *c);
VPRIV int x64_rtype(int mod, int pcrel, int size, int sgn, int rexw, int gotx);
VPRIV int x64_emit(as_context *a, XForm *f);
VPRIV int x64_raw(as_context *a, const uint8_t *b, int n);
VPRIV int x64_cc(const char *s);
VPRIV int sfxsize(char c);
VPRIV int opsize(const XOp *o);
VPRIV int x64_imm(as_context *a, const XOp *o, XForm *f, int64_t *k);
VPRIV int x64_size(as_context *a, int sfx, const XOp *s, const XOp *d);
VPRIV int rex8(const XOp *o);
VPRIV int split_sfx(const char *mn, char *stem, int *sz);
VPRIV int x64_alu(as_context *a, int n, int sz, XOp *o, int nops);
VPRIV int x64_branch(as_context *a, int cc, XOp *t, int call);
VPRIV int x64_sse(as_context *a, int pfx, int op, int w, int reg, const XOp *rm);
VPRIV int x64_x87m(as_context *a, int op, int digit, const XOp *m);
VPRIV int x64_x87r(as_context *a, int op, int base, int i);

/* the families, in the order x64_as_instruction_assemble tries them */
VPRIV int x64_i_crdr(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_zero(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_jump(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_setcc(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_x87(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_sse(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_avx(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_sse4(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_ssepk(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_bits(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_sys(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_x87ar(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_int(as_context *a, char *mn, XOp *o, int nops);
VPRIV int x64_i_movx(as_context *a, char *mn, XOp *o, int nops);
#endif
