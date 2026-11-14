/*
 * The walker's prototypes and the lowering interface (walk.h).  No include
 * guard: a second instantiation of the walker (SPEC-vir.md X1, codegen/vir/)
 * includes this again with WALK_PREFIX and WALK_LOWER_PREFIX defined, so that the
 * renamed functions are declared too.  Only prototypes and macros here.
 */
#include "prefix.h"

/* The lowering target: x64_ today.  A second instantiation of the walker
   (SPEC-vir.md X1) defines WALK_PREFIX and WALK_LOWER_PREFIX before including the
   walker files; WALK_LOWER_PREFIX is then the prefix of the target's operations. */
#undef WALK_LOWER
#ifdef WALK_LOWER_PREFIX
#define WALK_LOWER_PASTE_(p, n) p##n
#define WALK_LOWER_PASTE(p, n) WALK_LOWER_PASTE_(p, n)	/* (the extra level expands WALK_LOWER_PREFIX first) */
#define WALK_LOWER(name) WALK_LOWER_PASTE(WALK_LOWER_PREFIX, name)
#else
#define WALK_LOWER(name) x64_##name
#endif

VPRIV bool walk_switch_values_emit(codegen_context *e, uint32_t n);	/* (also in codegen-internal.h, for codegen/) */
VPRIV void walk_context_free(codegen_context *e);
VPRIV walk_state *walk_state_get(codegen_context *e);
VPRIV void walk_label_place(codegen_context *e, uint32_t label);
VPRIV bool walk_expression_is_constant(codegen_context *e, uint32_t n);
VPRIV int walk_condition_fold(codegen_context *e, uint32_t n);
VPRIV bool walk_branch_entry_check(cc *c, uint32_t n);
VPRIV bool walk_symbol_constant_get(codegen_context *e, uint32_t s, codegen_value *v);	/* s is bound to the constant *v */
VPRIV void walk_array_elements_clear(codegen_context *e, uint32_t s);	/* UINT32_MAX: every array */
VPRIV bool walk_parameter_constant_get(codegen_context *e, uint32_t s, codegen_value *v);	/* a provisional constant of parameter s (wkst.pk) */
VPRIV bool walk_symbol_address_may_be_zero(cc *c, codegen_value v);	/* the address of a weak undefined symbol (may be 0) */
VPRIV codegen_value walk_value_convert(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to);
VPRIV codegen_value walk_call_inline(codegen_context *e, uint32_t n, uint32_t s);	/* the call n to s, inlined (walk_call_can_inline) */
VPRIV bool walk_call_can_inline(codegen_context *e, uint32_t s, uint32_t call);
VPRIV void walk_function_emit(codegen_context *e, uint32_t s);	/* codegen/walk/func.c: what codegen/unit-emit.c calls for x86-64 */
VPRIV int64_t walk_slot_temporary(codegen_context *e, uint64_t size, uint32_t align);	/* a temporary: free again after the statement */
VPRIV int64_t walk_slot_permanent(codegen_context *e, uint64_t size, uint32_t align);	/* for the whole function */
VPRIV int64_t walk_slot_permanent_kind(codegen_context *e, uint64_t size, uint32_t align, int kind);	/* walk_slot_permanent, told to WALK_LOWER(slot) as kind (WK_SLOT_*) */
VPRIV int64_t walk_local_slot(codegen_context *e, uint32_t s);	/* a local's slot, made on first use (a goto may reach a use first) */
VPRIV uint32_t walk_label_new(codegen_context *e);
VPRIV codegen_value walk_value_emit(codegen_context *e, uint32_t n);
VPRIV codegen_lvalue walk_lvalue_emit(codegen_context *e, uint32_t n);
VPRIV codegen_value walk_division_signed_power_of_two_emit(codegen_context *e, ast_qualified_type t, codegen_value a, const em_sdivpow2_recipe *r);
VPRIV bool walk_division_unsigned_constant_check(codegen_context *e, codegen_value a, const em_udivconst_recipe *r);
VPRIV codegen_value walk_division_unsigned_constant_emit(codegen_context *e, ast_qualified_type ty, codegen_value a, const em_udivconst_recipe *r);
VPRIV int walk_value_to_register(codegen_context *e, codegen_value *v, ast_qualified_type t);
VPRIV void walk_value_free(codegen_context *e, const codegen_value *v);
VPRIV void walk_condition_emit(codegen_context *e, uint32_t n, bool jump_if, uint32_t lbl);
VPRIV void walk_statement_emit(codegen_context *e, uint32_t n);
VPRIV void walk_initializer_emit(codegen_context *e, const codegen_lvalue *dst, ast_qualified_type t, uint32_t init);
VPRIV void walk_bitfield_initialize(codegen_context *e, const codegen_lvalue *l, ast_qualified_type t, uint32_t init);	/* a bit-field initialized */	/* an object initialized by init (0: none) */
VPRIV int walk_type_size(cc *c, ast_qualified_type t);
VPRIV bool walk_type_is_in_memory(codegen_context *e, ast_qualified_type t);	/* values of t are held by address (records, arrays, x87 long double) */
VPRIV codegen_value walk_value_convert(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to);
VPRIV void walk_lvalue_store(codegen_context *e, const codegen_lvalue *l, ast_qualified_type t, codegen_value *v);
VPRIV codegen_value walk_lvalue_reload(codegen_context *e, const codegen_lvalue *l, ast_qualified_type t);	/* l's value again (a new register or address) */
VPRIV codegen_value walk_lvalue_store_memory(codegen_context *e, codegen_lvalue *l, ast_qualified_type t, codegen_value v);	/* v's bytes (held by address) into l: l's address */
VPRIV void codegen_constructor_entry_append(cc *c, tbuf *b, uint32_t s, bool fini);	/* arch/a64/codegen/func.c (shared) */
VPRIV bool walk_type_is_signed(cc *c, ast_qualified_type t);
VPRIV void walk_vla_sizes_evaluate(codegen_context *e, ast_qualified_type t, bool pure);	/* the VLA sizes in t evaluated now, into slots */
VPRIV codegen_value walk_vla_bytes(codegen_context *e, ast_qualified_type t);		/* sizeof t (a VLA: computed) */
VPRIV void walk_vla_declare(codegen_context *e, uint32_t s, bool zero);	/* a VLA's storage below sp */
VPRIV uint32_t walk_user_label_get(codegen_context *e, uint32_t l);
VPRIV void walk_lvalue_store_register(codegen_context *e, const codegen_lvalue *l, ast_qualified_type t, int r);
VPRIV void walk_goto_emit(codegen_context *e, uint32_t label);
VPRIV void walk_cleanups_run(codegen_context *e, int to);	/* the cleanups above depth `to` run, innermost first (not popped) */

/* ---------------- the lowering interface (layer 2) ---------------- */

/* registers */
VPRIV int WALK_LOWER(treg)(codegen_context *e);				/* a free temporary */
VPRIV void WALK_LOWER(tfree)(codegen_context *e, int r);
VPRIV void WALK_LOWER(mov)(codegen_context *e, int rd, int rs);			/* rd = rs (64 bits) */
VPRIV void WALK_LOWER(tkeep)(codegen_context *e, int r);			/* r in use again (an operation consumed it, the walker still holds it) */
VPRIV int WALK_LOWER(nfree)(codegen_context *e);				/* free temporaries left */
VPRIV int WALK_LOWER(nffree)(codegen_context *e);				/* free float temporaries left */
VPRIV int WALK_LOWER(toreg)(codegen_context *e, codegen_value *v, int size);	/* a constant or address into a temporary: v becomes CODEGEN_VALUE_REGISTER */
/* memory: size 1, 2, 4, 8; loads extend to 64 bits by sgn */
VPRIV void WALK_LOWER(load)(codegen_context *e, int rd, const codegen_lvalue *l, int size, bool sgn);
VPRIV void WALK_LOWER(store)(codegen_context *e, int rs, const codegen_lvalue *l, int size);
VPRIV int WALK_LOWER(laddr)(codegen_context *e, const codegen_lvalue *l);		/* the address in a temporary (l's register is reused) */
VPRIV void WALK_LOWER(copy)(codegen_context *e, int rdst, int rsrc, uint64_t n);	/* n bytes; the address registers stay live */
VPRIV void WALK_LOWER(zero)(codegen_context *e, int rdst, uint64_t n);
/* integers: size 4 or 8 (the operation's width); results in a's register */
VPRIV codegen_value WALK_LOWER(umulhi)(codegen_context *e, codegen_value a, codegen_value b);	/* high64(a*b), consumes both, returns a register */
VPRIV codegen_value WALK_LOWER(smulhi)(codegen_context *e, codegen_value a, codegen_value b);	/* signed high64(a*b), same ownership as umulhi */
VPRIV bool walk_division_signed_constant_check(codegen_context *e, codegen_value a, const em_sdivconst_recipe *r);
VPRIV codegen_value walk_division_signed_constant_emit(codegen_context *e, ast_qualified_type ty, codegen_value a, const em_sdivconst_recipe *r);
VPRIV codegen_value WALK_LOWER(bin)(codegen_context *e, int op, int size, bool sgn, codegen_value a, codegen_value b);	/* LEX_TOKEN_PLUS LEX_TOKEN_MINUS LEX_TOKEN_STAR LEX_TOKEN_SLASH LEX_TOKEN_PERCENT LEX_TOKEN_AMPERSAND LEX_TOKEN_PIPE LEX_TOKEN_CARET LEX_TOKEN_LESS_LESS LEX_TOKEN_GREATER_GREATER */
VPRIV void WALK_LOWER(switch_bitset)(codegen_context *e, int r, int size, const em_switch_bitset_recipe *recipe,
			   uint32_t target, uint32_t dflt);
VPRIV void WALK_LOWER(switch_table)(codegen_context *e, int r, int size, const em_switch_case *cases, uint32_t n, uint32_t dflt);
VPRIV codegen_value WALK_LOWER(un)(codegen_context *e, int op, int size, codegen_value a);			/* LEX_TOKEN_MINUS LEX_TOKEN_TILDE */
VPRIV void WALK_LOWER(ext)(codegen_context *e, int r, int from, bool sgn);		/* low `from` bytes of r extended to 64 bits */
VPRIV codegen_value WALK_LOWER(cmp)(codegen_context *e, int op, int size, bool sgn, codegen_value a, codegen_value b);	/* 0 or 1 */
VPRIV void WALK_LOWER(cmpbr)(codegen_context *e, int op, int size, bool sgn, codegen_value a, codegen_value b, uint32_t lbl);	/* to lbl if a op b */
VPRIV void WALK_LOWER(brz)(codegen_context *e, codegen_value a, int size, bool nz, uint32_t lbl);	/* to lbl if (a != 0) == nz */
/* floating point: float and double values in the CPU's float registers; long double as
   WALK_LOWER(ldmem) says (true: by address, a 16-byte object, as records) */
VPRIV bool WALK_LOWER(ldmem)(void);
/* __int128: by address too when WALK_LOWER(i128mem) */
VPRIV bool WALK_LOWER(i128mem)(void);
VPRIV codegen_value WALK_LOWER(wconst)(codegen_context *e, i128 v);
VPRIV codegen_value WALK_LOWER(wbitint)(codegen_context *e, codegen_value v, unsigned bits, bool sgn);
/* Integer objects wider than 128 bits, canonical little-endian limbs. */
VPRIV codegen_value WALK_LOWER(bigconst)(codegen_context *e, ast_qualified_type t, const uint32_t *words, i128 small);
VPRIV codegen_value WALK_LOWER(bigbin)(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b);
VPRIV codegen_value WALK_LOWER(bigcmp)(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b);
VPRIV codegen_value WALK_LOWER(bigcvt)(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to);
VPRIV codegen_value WALK_LOWER(wbin)(codegen_context *e, int op, bool sgn, codegen_value a, codegen_value b);	/* b an int for << >> */
VPRIV codegen_value WALK_LOWER(wun)(codegen_context *e, int op, codegen_value a);			/* LEX_TOKEN_MINUS LEX_TOKEN_TILDE */
VPRIV codegen_value WALK_LOWER(wcmp)(codegen_context *e, int op, bool sgn, codegen_value a, codegen_value b);
VPRIV codegen_value WALK_LOWER(wcvt)(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to);	/* __int128 on either side */
VPRIV int WALK_LOWER(ftreg)(codegen_context *e);				/* a free float temporary */
VPRIV codegen_value WALK_LOWER(fconst)(codegen_context *e, uint64_t bits, int size);	/* a float (4) or double (8) constant */
VPRIV codegen_value WALK_LOWER(fbin)(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b);	/* + - * / */
VPRIV codegen_value WALK_LOWER(fneg)(codegen_context *e, ast_qualified_type t, codegen_value a);
VPRIV codegen_value WALK_LOWER(fsqrt)(codegen_context *e, ast_qualified_type t, codegen_value a);
VPRIV codegen_value WALK_LOWER(root4)(codegen_context *e, codegen_value a);
VPRIV codegen_value WALK_LOWER(fcmp)(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b);	/* 0 or 1, false when unordered (but !=) */
VPRIV codegen_value WALK_LOWER(cvt)(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to);	/* a float on either side (to may be _Bool) */
/* builtins the CPU does */
VPRIV void WALK_LOWER(trap)(codegen_context *e);				/* __builtin_trap, __builtin_unreachable */
VPRIV void WALK_LOWER(asm)(codegen_context *e, uint32_t n);
VPRIV void WALK_LOWER(barrier)(codegen_context *e);			/* compiler barrier: no machine instruction */
VPRIV codegen_value WALK_LOWER(setjmp)(codegen_context *e, int rbuf);
VPRIV void WALK_LOWER(longjmp)(codegen_context *e, int rbuf);
VPRIV codegen_value WALK_LOWER(frame)(codegen_context *e, bool ret, uint32_t level);			/* the return address (ret) or the frame address */
VPRIV codegen_value WALK_LOWER(bitop)(codegen_context *e, int bi, int size, codegen_value a);	/* CC_BUILTIN_CLZ.. CC_BUILTIN_BSWAP64: a nonzero where the builtin requires it */
VPRIV codegen_value WALK_LOWER(alloca)(codegen_context *e, codegen_value n);			/* n bytes on the stack, 16-aligned */
VPRIV int WALK_LOWER(tlsaddr)(codegen_context *e, uint32_t ref, int model);	/* a thread-local's address: 0 local-exec, 1 initial-exec, 2 general dynamic */
/* atomics at [rp], size 1, 2, 4, 8, sequentially consistent; the operand registers stay
   the caller's; a result is a new temporary, zero-extended:
   LOAD -> value; STORE rv; XCHG / XADD rv -> the old value; CAS: [rp] = rv if it holds
   rexp -> the old value; FENCE */
VPRIV int WALK_LOWER(atomic)(codegen_context *e, int op, int size, int rp, int rv, int rexp);
VPRIV int WALK_LOWER(spget)(codegen_context *e);				/* the stack pointer in a temporary */
VPRIV void WALK_LOWER(spset)(codegen_context *e, int r);			/* the stack pointer = r (r freed) */
VPRIV codegen_value WALK_LOWER(fbits)(codegen_context *e, codegen_value v, int size);		/* a float's bits in an integer register */
/* control */
VPRIV void WALK_LOWER(dbgrow)(codegen_context *e, uint32_t loc);	/* -g: line row at the next instruction */
VPRIV void WALK_LOWER(label)(codegen_context *e, uint32_t l);
VPRIV void WALK_LOWER(jump)(codegen_context *e, uint32_t l);
VPRIV void WALK_LOWER(jumpr)(codegen_context *e, int r);			/* to the address in r (computed goto) */
VPRIV void WALK_LOWER(named)(codegen_context *e, uint32_t ref);		/* a label with a symbol name here (ref: CODEGEN_REFERENCE_LABEL, an address taken) */
/* calls: the arguments are in frame slots (8 bytes each, extended); ref: a direct callee, else fn */
VPRIV void WALK_LOWER(classify)(codegen_context *e, ast_qualified_type t, walk_argument *a, bool unnamed);
/* r: a record result (its slot receives it), NULL otherwise; retsize/retsgn: a scalar result */
VPRIV codegen_value WALK_LOWER(call)(codegen_context *e, uint32_t ref, codegen_value *fn, const walk_argument *a, int n, bool variadic, const walk_argument *r, ast_qualified_type rt);
/* Incoming arguments to their slots or selected register homes; a parameter
   left on the stack keeps the caller's location as its slot. */
VPRIV void WALK_LOWER(params)(codegen_context *e, walk_argument *a, int n, const walk_argument *r, bool variadic);
VPRIV bool WALK_LOWER(tail_return)(codegen_context *e, uint32_t expr);
VPRIV void WALK_LOWER(ret)(codegen_context *e, codegen_value *v, ast_qualified_type t, uint32_t lbl);	/* a scalar result of type t (v may be NULL), then to lbl */
VPRIV void WALK_LOWER(retrec)(codegen_context *e, int raddr, const walk_argument *r, uint32_t lbl);	/* a record result from the object at raddr */
VPRIV codegen_value WALK_LOWER(pcmpstr)(codegen_context *e, uint32_t n);
VPRIV void WALK_LOWER(va_start)(codegen_context *e, int rap);			/* the va_list at rap */
VPRIV int WALK_LOWER(va_arg)(codegen_context *e, int rap, ast_qualified_type t);		/* the next argument's address */
/* the function: before its body, and once it is generated (frame, prologue, output) */
VPRIV void WALK_LOWER(begin)(codegen_context *e, uint32_t s);
VPRIV void WALK_LOWER(locals_begin)(codegen_context *e, uint32_t s);
VPRIV bool WALK_LOWER(local)(codegen_context *e, uint32_t s, codegen_lvalue *l);
/* a parameter of the function (not of an inlined copy) whose frame slot holds its address: the
   Microsoft convention's by-reference arguments, used in place (PLAN-win64.md WIN2) */
VPRIV bool WALK_LOWER(byref)(codegen_context *e, uint32_t s);
#ifdef WALK_PREFIX
/* VIR: a local declared without an initializer: its value is indeterminate from here (C11 6.2.4) */
VPRIV void WALK_LOWER(undecl)(codegen_context *e, const codegen_lvalue *l, uint64_t size);
#endif
/* The walker hands out the frame offset off (negative, from the frame base)
   for size bytes at align: fresh, or an earlier slot of the same size and
   alignment made free again.  The same offset names different objects at
   different times; this is the only place a target learns when.  Called once
   per hand-out. */
VPRIV void WALK_LOWER(slot)(codegen_context *e, int64_t off, uint64_t size, uint32_t align, int kind);
VPRIV void WALK_LOWER(finish)(codegen_context *e, uint32_t s);

