# VIR 1.0 specification

Status: **draft 0.5, 2026-11-11**. Implemented: the format and its tools in
sections 3-12, 16-18 and 22.5 (V1's first half; 0.4's corrections are listed
in its pull request), and section 15's allocator and checker on a synthetic
target (`PLAN-vir.md` V4, first part); nothing else. Plan and steps
are in `PLAN-vir.md`; the reasons behind the rules are in `RESEARCH-vir.md`
(rule numbers R1-R20 refer to its section 5). Changed in 0.5, from the
allocator's implementation: 15.1 the full `vir_regs`/`vir_fix`; 15.2 spill
slots in the allocator's own table, preconditions, the loop forest as input;
15.5 `lend` at every back-edge target, the rule for edges into the middle of a
loop and slots for blocks entered in slots (all three irreducible-flow fixes
found by random testing), linear mode as a restart; 15.6 the step order with
clobbers, the tied fallback, the header rule's count; 15.8 the choice of
predecessor, cycle breaking; 15.11 what the checker checks. Changed in 0.4: section 15's
allocator is no longer linear scan over intervals but greedy SSA assignment
with farthest-next-use spilling, from `RESEARCH-regalloc.md` section 7
(15.2, 15.3, new 15.5-15.11; 22.6 rows 5 and 6; the sweep count is unchanged).

This document is the complete definition of the format and of the tools
around it. Code is written to it, and a disagreement between code and this
text is a bug in one of them that is settled here first. Words used strictly:
**must** is checked by the verifier or is a contract a pass relies on;
**immediate UB** means the program has no defined behaviour from that
instruction on; **unspecified** means one of several allowed results, fixed
for one execution.

Contents: 1 scope, 2 model, 3 in-memory format, 4 types, 5 values and
constants, 6 memory model, 7 instructions, 8 control flow, 9 ABI signatures,
10 effects and what passes may do, 11 text form, 12 verifier, 13 interpreter,
14 passes, 15 register allocation and lowering, 16 tools, 17 tests, 18 limits,
19 integration with the walker, 20 open issues, 21 traceability,
22 performance design.

## 1. Scope

1.1 VIR is a per-function SSA representation between the shared AST walker
(`emit/walk/`) and a CPU's lowering (`LO()`), used at `-O2` only.

1.2 A VIR function is built **for one target**: the ABI classification, the
data model and the choice of what is held in memory are the target's and are
fixed when the function is built. 1.0 targets: x86-64 Linux, arm64 Linux and
macOS. All are little-endian with 64-bit pointers; VIR 1.0 assumes both.

1.3 Not in 1.0 (a function that needs one of these is not built as VIR and
takes the ordinary path): calls to `returns_twice` functions, `setjmp`,
`alloca` and VLAs, `__int128`/`_BitInt` values wider than 64 bits, x87
`long double` and binary128 as values, C vector types (VIR's own vectors,
section 7.5, are made by the loop vectorizer only), `_Float16`, tail calls,
`naked` functions, interrupt calling conventions, and (until the step that
splits the target's asm handling, X6) inline assembly other than an empty
barrier.

1.4 Non-goals of 1.0: an on-disk binary form, a stable C API for outside
users, cross-function optimization, loop transformations.

## 2. Model

2.1 A **function** is: a header, a signature, a table of **slots** (frame
objects), an array of **instructions**, an array of **blocks**, an `extra`
array of 32-bit words, and side arrays. Nothing in a function points into
another function.

2.2 A **value** is the index of the instruction that defines it. Index 0 is
reserved and means "no value".

2.3 Instructions are in three regions, in this order:
1. index 0, a `nop`;
2. the **constant region**: `iconst`, `fconst`, `symaddr`, `slotaddr`,
   `labeladdr` only, each unique within the function;
3. the **block region**: every block's instructions, contiguous per block,
   every block after its immediate dominator (the builder uses reverse
   postorder of the control-flow graph from block 0).

2.4 **Canonical form** is what every pass receives and returns: the three
regions as above; no unreachable block; no `nop` inside the block region; the
constants ordered by first use in the block region and none unused. `vir_canon`
produces it in one sweep. In canonical form every value operand of an
instruction has a smaller index than the instruction (this follows from 8.5
and the block order), so one forward sweep sees definitions before uses.

2.5 A **unit** exists only in the text form and in the test tools: a target
line, declarations of symbols, signatures and asm templates, and functions.
Inside the compiler the same information comes from `cc` and the emitter.

2.6 The **builder form** is what `vir_const` and `vir_ins_add` produce before
`vir_canon`: constants and block instructions are created in the order the
walker needs them, so a constant may lie inside the range of the block that
was open when it was created (the block's `n` counts it); blocks may be in
any order, there may be `nop`s and unreachable blocks, and `nconst` is 0.
`vir_canon` takes either form to the canonical one. The verifier requires the
regions of 2.3 (rule S1), so it is run on canonical or parsed functions.

## 3. In-memory format

3.1 Instruction, 16 bytes, little-endian fields:

```c
typedef struct vir_ins {
	uint8_t  op;	/* VO_* (section 7) */
	uint8_t  ty;	/* VT_* of the result (section 4) */
	uint16_t fl;	/* per-op flags (7.2) */
	uint32_t a, b, c;	/* per op: a value, an immediate, a table index or an extra offset */
} vir_ins;
```

3.2 Block:

```c
typedef struct vir_blk {
	uint32_t first, n;	/* instructions [first, first+n) */
	uint16_t nparam;	/* the first nparam instructions are VO_BPARAM */
	uint16_t fl;		/* VB_LOOP header, VB_ADDR address taken, VB_COLD */
	uint16_t depth;		/* loop nesting, 0 outside loops */
	uint16_t pad;
	uint32_t pred, npred;	/* predecessors: extra[pred .. pred+npred), block ids; valid after vir_cfg */
	uint32_t idom;		/* immediate dominator (block 0: itself); ~0 if unreachable; valid after vir_dom */
	uint32_t pre, post;	/* dominator-tree preorder number, and the largest one in the subtree (22.5) */
} vir_blk;				/* 36 bytes */
```

The last instruction of a block is its only terminator (section 8).

3.3 Slot:

```c
typedef struct vir_slot {
	uint64_t size;
	uint32_t align;		/* power of two */
	uint32_t fl;		/* VS_PARAM incoming memory argument, VS_EXPOSED (computed, 6.4) */
	int64_t  hint;		/* frame offset the builder proposes; INT64_MIN: none */
} vir_slot;
```

3.4 Function:

```c
typedef struct vir_fn {
	uint32_t version;	/* 1 */
	uint32_t target;	/* VTG_X86_64_LINUX, VTG_A64_LINUX, VTG_A64_DARWIN */
	uint32_t sym;		/* the function's symbol (emitter ref) */
	uint32_t sig;		/* extra offset of its signature (section 9) */
	uint32_t fl;		/* VF_NOFP, VF_FENV, VF_NULLINVALID, VF_VARIADIC, VF_NORETURN */
	vir_ins  *ins;   uint32_t nins, nconst;	/* nconst: end of the constant region */
	vir_blk  *blk;   uint32_t nblk;
	vir_slot *slot;  uint32_t nslot;
	uint32_t *extra; uint32_t nextra;
	vir_loc  *loc;   uint32_t nloc;	/* runs of (first instruction, source location), 22.7 */
	uint32_t *tmp;		/* per value: scratch for the running pass, undefined between passes */
} vir_fn;
```

All arrays come from the thread's `ecx` and are reset per function. A pass
that needs more room writes a new set of arrays and swaps them in.

`vir_loc` is `{ uint32_t first, loc; }`: the index of the first instruction of
a run, and the compiler's source-location id. The flag values are: `VB_LOOP`
1, `VB_ADDR` 2, `VB_COLD` 4; `VS_PARAM` 1, `VS_EXPOSED` 2; `VF_NOFP` 1,
`VF_FENV` 2, `VF_NULLINVALID` 4, `VF_VARIADIC` 8, `VF_NORETURN` 16,
`VF_LOCAL` 32 (the symbol is local to the unit), `VF_EXPOSURE` 64 (every
`VS_EXPOSED` is valid: `mem` computes it, rule M1 checks it). The targets are
`VTG_X86_64_LINUX` 1, `VTG_A64_LINUX` 2, `VTG_A64_DARWIN` 3. Offset 0 of
`extra` is a reserved word, so an extra offset of 0 means "none".

3.5 `extra` holds every variable-length operand list. An instruction refers to
its list by offset; the layouts are given per instruction. Words are values,
block ids, immediates or packed descriptors as stated.

3.6 There are no use lists. `vir_uses(f, cnt)` fills a per-value use count in
one sweep when a pass asks for it.

## 4. Types

| `ty` | Name | Meaning |
|---|---|---|
| 0 | `void` | No result |
| 1 | `i32` | 32-bit integer, no signedness |
| 2 | `i64` | 64-bit integer, no signedness |
| 3 | `f32` | IEEE binary32 |
| 4 | `f64` | IEEE binary64 |
| 5 | `ptr` | Address with provenance (section 6), 64 bits |
| 6 | `multi` | Several results, read with `proj` |
| 7 | `f32x4` | Four `f32` lanes, 128 bits (section 7.5) |
| 8 | `i32x4` | Four `i32` lanes, 128 bits (section 7.5) |
| 9 | `f64x2` | Two `f64` lanes, 128 bits (section 7.5) |
| 10 | `i64x2` | Two `i64` lanes, 128 bits (section 7.5) |

4.1 There are no 8- or 16-bit values, no booleans, no aggregates. Narrow
integers exist as load and store widths and as the `ext` operation.
4.2 A comparison yields `i32` 0 or 1. A condition operand is any `i32` or
`i64`; nonzero is true.
4.3 `ptr` is not an integer type: arithmetic on it is `ptradd` and `psub`
only, and the conversions are `p2i` and `i2p` (R3).

## 5. Values and constants

5.1 Every value is a concrete bit pattern of its type. There is no undefined
or poison value (R1). Where C leaves a value indeterminate the builder
supplies 0.

5.2 Constants:

| Op | `ty` | Fields | Value |
|---|---|---|---|
| `iconst` | `i32` `i64` `ptr` | `b` low, `c` high 32 bits | The integer; for `i32`, `c` must be 0. A `ptr` constant is that address, based on the exposed set (6.3) |
| `fconst` | `f32` `f64` | `b`, `c`: the IEEE bits | That bit pattern |
| `symaddr` | `ptr` | `a` emitter ref (symbol, string, table), `b`,`c` signed 64-bit addend | Address of the symbol plus addend |
| `slotaddr` | `ptr` | `a` slot id, `b`,`c` addend | Address of the slot plus addend |
| `labeladdr` | `ptr` | `a` block id (the block must have `VB_ADDR`) | Address of that block's code |

5.3 Two constants with equal `op`, `ty`, `a`, `b`, `c` are the same
instruction. Constants are never in a block and have no position in time.

## 6. Memory model

6.1 Memory is a set of disjoint **objects**, each an array of concrete bytes:
the function's slots (one instance per activation), the objects behind
symbols, and everything else ("outside memory": heap, other frames, device
memory). Uninitialized bytes are unspecified but fixed (R1).

6.2 An access is a `load`, `store`, `memcpy`, `memset`, atomic operation,
`va_start`, `va_arg`, or the accesses a call or asm performs. An access outside
every live object, or outside the object its pointer is based on, is immediate
UB.

6.3 **Based on.** A `ptr` value is based on:
- slot *s*, if it is `slotaddr s`;
- symbol *x*, if it is `symaddr x`;
- whatever `p` is based on, if it is `ptradd p, i`;
- the union of what the alternatives are based on, if it is a `select` or a
  block parameter;
- **the exposed set**, in every other case (`i2p`, `load`, `param`, `proj`,
  a call result, a non-symbol `iconst`).

6.4 **Exposed.** A slot is exposed if a pointer based on it is used as
anything other than: the address operand of `load`, `store`, `memcpy`,
`memset`, an atomic operation or `va_start`/`va_arg`; the pointer operand of
`ptradd` or `psub`; an operand of `icmp`; the argument of a `br` or `select`
whose result is itself only used so. In particular storing the pointer,
passing it to a call or asm, returning it and `p2i` expose the slot. Symbols
and outside memory are always exposed.

6.5 A pointer based on the exposed set may access any exposed object and
outside memory, and never a slot that is not exposed.

6.6 Null is an ordinary address unless the function has `VF_NULLINVALID`; then
an access at address 0 is immediate UB.

6.7 `p2i` gives the address. `i2p` gives a pointer to that address based on
the exposed set. No rule equates `i2p(p2i(p))` with `p`, and no pass may
replace a pointer by another value because they compare equal (R3).

6.8 **Overlaid slots.** Two slots whose `hint` ranges overlap may occupy the
same bytes. The builder guarantees that no byte stored to one is read after a
store to the other. They are still different objects for 6.3-6.5. A pass must
not move an access to a slot across an access to a slot overlaid with it; no
1.0 pass moves memory accesses at all.

6.9 The address of a slot compares unequal to the address of a different slot
only if neither is overlaid with the other; a pass must not fold a comparison
of two different slot addresses otherwise (R4).

6.10 `volatile` accesses are observable events: each is performed exactly
once, at its width, in order relative to other volatile accesses (R19).

## 7. Instructions

### 7.1 Notation

`T` is the instruction's `ty`. Operand kinds: `v` a value; `#` an immediate
in the field; `@x` an extra offset; `-` must be 0. Effects (section 10): **P**
pure, **R** reads memory, **W** writes memory, **U** may be immediate UB, **C**
call-like (may not return, may read and write all exposed memory), **T**
terminator.

### 7.2 Flags (`fl`)

| Used by | Bits | Meaning |
|---|---|---|
| `add sub mul shl neg` | 0 `NSW`, 1 `NUW` | Signed / unsigned overflow is immediate UB |
| `sdiv udiv lshr ashr` | 2 `EXACT` | A nonzero remainder or shifted-out bit is immediate UB |
| `clz ctz` | 3 `NZ` | A zero operand is immediate UB |
| `ptradd` | 4 `INB` | Reserved, must be 0 in 1.0 |
| `load store` and atomics | 0-1 `W`: log2 of the width in bytes; 2 `S`: sign-extend (load); 4-7 log2 of the alignment; 8 `VOL` volatile |
| `ext` | 0-1 log2 of the source width in bytes (1, 2, 4, 8); 2 `S` |
| `icmp fcmp` | 0-3 condition code |
| `memcpy memset` | 4-7 log2 alignment of `a`; 8-11 of `b` (memcpy) |
| `call` | 0 `NORETURN`, 1 `PURE`, 2 `CONST`, 3 `TAIL` (reserved, 0) |
| `brif` | 0 `LIKELY`, 1 `UNLIKELY` |

A flag bit not listed for an instruction must be 0. A pass that moves an
instruction to where it was not executed before, or changes its operands
other than to equal values, must clear its UB flags (R2).

### 7.3 Table

| # | Op | T | a | b | c | Eff | Meaning |
|---|---|---|---|---|---|---|---|
| 0 | `nop` | void | - | - | - | P | Nothing; only at index 0 and transiently inside passes |
| 1-5 | constants | | | | | P | Section 5 |
| 6 | `bparam` | any but void, multi | - | - | - | P | Block parameter; value supplied by each `br` to the block |
| 7 | `param` | i64 f32 f64 ptr | # group | # piece | - | P | Incoming argument piece, or for a memory group the address of its bytes (section 9); only in block 0 |
| 8 | `proj` | per producer | v producer (`multi`) | # index | - | P | Result number `b` of a call or asm |
| 9 | `fma` | f32 f64 | v:T | v:T | v:T | P/F | `a * b + c` rounded once (IEEE fusedMultiplyAdd). Flags: `negp` the product is negated, `negc` the addend is negated, both exact |
| 10 | `add` | i32 i64 | v:T | v:T | - | P,U* | Sum modulo 2^n |
| 11 | `sub` | i32 i64 | v:T | v:T | - | P,U* | Difference modulo 2^n |
| 12 | `mul` | i32 i64 | v:T | v:T | - | P,U* | Product modulo 2^n |
| 13 | `sdiv` | i32 i64 | v:T | v:T | - | U | Signed quotient toward zero; divisor 0 is immediate UB; minimum / -1 is the minimum |
| 14 | `udiv` | i32 i64 | v:T | v:T | - | U | Unsigned quotient; divisor 0 is immediate UB |
| 15 | `srem` | i32 i64 | v:T | v:T | - | U | Signed remainder, sign of the dividend; divisor 0 immediate UB; minimum % -1 is 0 |
| 16 | `urem` | i32 i64 | v:T | v:T | - | U | Unsigned remainder; divisor 0 immediate UB |
| 17 | `and` | i32 i64 | v:T | v:T | - | P | |
| 18 | `or` | i32 i64 | v:T | v:T | - | P | |
| 19 | `xor` | i32 i64 | v:T | v:T | - | P | |
| 20 | `shl` | i32 i64 | v:T | v:T | - | P,U* | Shift left by `b` modulo the width |
| 21 | `lshr` | i32 i64 | v:T | v:T | - | P,U* | Logical shift right by `b` modulo the width |
| 22 | `ashr` | i32 i64 | v:T | v:T | - | P,U* | Arithmetic shift right by `b` modulo the width |
| 23 | `umulhi` | i64 | v:i64 | v:i64 | - | P | High 64 bits of the unsigned 128-bit product |
| 24 | `smulhi` | i64 | v:i64 | v:i64 | - | P | High 64 bits of the signed 128-bit product |
| 25 | `neg` | i32 i64 | v:T | - | - | P,U* | 0 - a |
| 26 | `not` | i32 i64 | v:T | - | - | P | Bitwise complement |
| 27 | `clz` | i32 i64 | v:T | - | - | P,U* | Leading zeros; the width for 0 |
| 28 | `ctz` | i32 i64 | v:T | - | - | P,U* | Trailing zeros; the width for 0 |
| 29 | `popcnt` | i32 i64 | v:T | - | - | P | Number of one bits |
| 30 | `bswap` | i32 i64 | v:T | - | - | P | Bytes reversed |
| 31 | `ext` | i32 i64 | v:i32 or i64 | - | - | P | The low `W` bytes of `a`, sign- (`S`) or zero-extended to T; `W` must not exceed the operand's size |
| 32 | `trunc` | i32 | v:i64 | - | - | P | Low 32 bits |
| 33 | `icmp` | i32 | v:X | v:X | - | P | X is i32, i64 or ptr. Codes: 0 eq, 1 ne, 2 slt, 3 sle, 4 sgt, 5 sge, 6 ult, 7 ule, 8 ugt, 9 uge. On ptr the addresses are compared, signed codes not allowed |
| 34 | `fcmp` | i32 | v:F | v:F | - | P | F is f32 or f64. Codes: 0 oeq, 1 une, 2 olt, 3 ole, 4 ogt, 5 oge, 6 uno, 7 one. Ordered codes are false with a NaN operand; `une` and `uno` true |
| 35 | `select` | any but void, multi | v cond | v:T | v:T | P | `b` if `a` is nonzero, else `c` |
| 36-39 | `fadd fsub fmul fdiv` | f32 f64 | v:T | v:T | - | P/F | IEEE result, round to nearest even |
| 40 | `fneg` | f32 f64 | v:T | - | - | P | Sign bit flipped |
| 41 | `fabs` | f32 f64 | v:T | - | - | P | Sign bit cleared |
| 42 | `fsqrt` | f32 f64 | v:T | - | - | P/F | IEEE square root |
| 43 | `fext` | f64 | v:f32 | - | - | P/F | Exact widening |
| 44 | `ftrunc` | f32 | v:f64 | - | - | P/F | Rounded narrowing |
| 45 | `s2f` | f32 f64 | v:i32 or i64 | - | - | P/F | Signed integer to float, rounded |
| 46 | `u2f` | f32 f64 | v:i32 or i64 | - | - | P/F | Unsigned integer to float, rounded |
| 47 | `f2s` | i32 i64 | v:f32 or f64 | - | - | U | Truncation toward zero; NaN or a result out of range is immediate UB |
| 48 | `f2u` | i32 i64 | v:f32 or f64 | - | - | U | As `f2s`, unsigned range |
| 49 | `bitcast` | i32 i64 f32 f64 | v | - | - | P | Same bits; i32 with f32, i64 with f64 only |
| 50 | `ptradd` | ptr | v:ptr | v:i64 | - | P | Address plus `b` modulo 2^64, same basis as `a` |
| 51 | `psub` | i64 | v:ptr | v:ptr | - | P | Address difference modulo 2^64 |
| 52 | `p2i` | i64 | v:ptr | - | - | P | The address; exposes (6.4) |
| 53 | `i2p` | ptr | v:i64 | - | - | P | Pointer to that address, based on the exposed set |
| 54 | `load` | i32 i64 f32 f64 ptr | v:ptr | # alias tag (0) | - | R,U | The `W` bytes at `a`. Integer T: extended per `S`. f32 needs W=4; f64 and ptr W=8 |
| 55 | `store` | void | v:ptr | v value | # alias tag (0) | W,U | The low `W` bytes of `b` to `a`. f32 needs W=4; f64 and ptr W=8 |
| 56 | `memcpy` | void | v:ptr dst | v:ptr src | v:i64 size, an `iconst` | R,W,U | Copy; the ranges must be identical or disjoint, else immediate UB |
| 57 | `memset` | void | v:ptr dst | v:i32 byte | v:i64 size, an `iconst` | W,U | Fill with the low 8 bits of `b` |
| 58 | `aload` | i32 i64 | v:ptr | - | - | R,U,A | Atomic load of `W` bytes, zero-extended |
| 59 | `astore` | void | v:ptr | v:i32 or i64 | - | W,U,A | Atomic store |
| 60 | `axchg` | i32 i64 | v:ptr | v:T | - | R,W,U,A | Store `b`, return the old value |
| 61 | `axadd` | i32 i64 | v:ptr | v:T | - | R,W,U,A | Add `b`, return the old value |
| 62 | `acas` | i32 i64 | v:ptr | v:T expected | v:T new | R,W,U,A | Store `c` if the content equals `b`; return the old value |
| 63 | `afence` | void | - | - | - | A | Full memory barrier |
| 64 | `call` | void, a piece type, or multi | v:ptr callee | @x signature | @x arguments | C | Section 9. Direct when `a` is a `symaddr` with addend 0 |
| 65 | `va_start` | void | v:ptr va_list | - | - | W | Initialize the `va_list` at `a` |
| 66 | `va_arg` | ptr | v:ptr va_list | @x one argument group | - | R,W,U | Address of the next variadic argument as that group passes it; advances the list |
| 67 | `tlsaddr` | ptr | # emitter ref | # model (0 local-exec, 1 initial-exec, 2 general dynamic) | - | R | Address of a thread-local; based on its symbol |
| 68 | `asm` | void or multi | # asm template id | @x operands | - | C or less (8.6) | Inline assembly without label operands |
| 69 | `barrier` | void | - | - | - | R,W | Reads and writes all exposed memory; no code |
| 70 | `frameaddr` | ptr | # level | - | - | R | `__builtin_frame_address(level)` |
| 71 | `retaddr` | ptr | # level | - | - | R | `__builtin_return_address(level)` |
| 80-86 | terminators | | | | | T | Section 8 |

`U*`: only when a UB flag is set. `P/F`: pure, unless the function has
`VF_FENV`; then it reads and writes the floating-point environment and is
treated as R,W on a location nothing else touches (it may be neither removed,
merged nor reordered with other F operations or calls). `A`: atomic operations
are sequentially consistent, are never removed or merged, and other threads
may observe and change the memory between any two instructions.

Opcodes 128-255 are reserved for target instructions introduced by lowering;
they never appear in canonical 1.0 VIR.

### 7.4 Floating point

7.4.1 Without `VF_FENV` the rounding mode is nearest-even and exception flags
are not observable.
7.4.2 When an operation yields a NaN, its sign and payload are unspecified.
The constant folder therefore does not fold an operation whose result is a NaN.
7.4.3 A function with `VF_NOFP` must contain no value of type `f32` or `f64`
and no `fconst`.

### 7.5 Vectors

Draft (2026-11-12; `RESEARCH-vectors.md` section 5 is the design and says what
is left out). Lane 0 is at the lowest address.

7.5.1 A vector is a value like any other: a block parameter, a `select` arm
(the condition is a scalar and picks a whole vector), a `load` result, a
`store` operand. A `load` or `store` of a vector moves 16 bytes; its width
field is the lane's, and its alignment is at least the lane's, never 16 by
default.
7.5.2 These operations take a vector type as their result type, lane by lane:
`add sub and or xor neg not` on `i32x4` and `i64x2`, `mul` on `i32x4` only
(no target has a 64-bit lane multiply); `fadd fsub fmul fdiv fneg fabs fsqrt`
on `f32x4` and `f64x2`, each lane as 7.4. No flag (`nsw`, `nuw`) is allowed to
mean anything on a vector: lanes wrap.
7.5.3 There is no mask type. A mask is the integer vector of the same lane
width (`i32x4` for `f32x4` and `i32x4`, `i64x2` for the other two) whose
lanes are 0 or -1; `vbitsel` is defined for every bit pattern. Below, V is
any vector type, I(V) the integer and F(V) the float vector of V's lane width.

| # | Name | `ty` | `a` | `b` | `c` | Eff | Meaning |
|---|---|---|---|---|---|---|---|
| 72 | `vsplat` | V | lane type | - | - | P | `a` in every lane |
| 73 | `vseries` | integer V | - | - | - | P | Lanes 0, 1, 2, ... |
| 74 | `vicmp.cc` | integer V | V | V | - | P | `icmp`'s codes per lane: -1 or 0 |
| 75 | `vfcmp.cc` | integer V | F(V) | F(V) | - | P | `fcmp`'s codes per lane: -1 or 0 |
| 76 | `vbitsel` | V | I(V) | V | V | P | Per bit: `a` set takes `b`, clear takes `c` |
| 77 | `vs2f` | float V | I(V) | - | - | P,F | Signed lanes to float of the same width |
| 78 | `vrev` | V | V | - | - | P | The lanes in reverse order |
| 79 | `vreduce` | `i32` `i64` | the vector of such lanes | - | - | P | The lanes' wrapping sum |
| 87 | `vinsert` | V | V | lane type | lane immediate | P | Replace lane `c` of `a` with `b` |
| 88 | `vextract` | lane type | V | lane immediate | - | P | Extract lane `b` of `a` |

Lane indices must be smaller than the number of lanes (T2). Insert and extract
preserve the lane's bits, including NaN payloads and signed zeros.

7.5.4 Register allocation: vectors are in the float class; a vector's spill
slot is 16 bytes (15.2), and no v register keeps a vector across a call. A
target may still report float registers as callee-saved (arm64's v8-v15, low
halves only) for a function in which no vector is live across a call.

## 8. Control flow

8.1 Terminators:

| # | Op | a | b | c | Meaning |
|---|---|---|---|---|---|
| 80 | `br` | # block | - | @x arguments | Jump; `extra[c..]` holds one value per parameter of the target, in order |
| 81 | `brif` | v cond | # block if nonzero | # block if zero | Two-way branch |
| 82 | `switch` | v:i32 or i64 | # default block | @x cases | `extra[c]` = n, then n triples (lo, hi, block) for i32, or (lo_lo, lo_hi, hi_lo, hi_hi, block) for i64; ranges inclusive, as unsigned, sorted and disjoint |
| 83 | `ibr` | v:ptr | - | @x targets | Jump to the address; `extra[c]` = n, then n block ids. An address that is not one of them is immediate UB |
| 84 | `asmgoto` | # asm template id | @x operands | @x targets | Inline assembly with label operands; `extra[c]` = n, the fallthrough block, then n label blocks |
| 85 | `ret` | - | - | @x values | Return; values per the function's result group (section 9) |
| 86 | `trap` | - | - | - | Stops the program; reaching it is not UB. Lowered to a trapping instruction, never to nothing |

8.2 **Only `br` passes arguments.** A block with parameters must have only
`br` predecessors. The targets of `brif`, `switch`, `ibr` and `asmgoto` have no
parameters; the builder inserts a block with a single `br` on an edge that
needs arguments. (Consequence: the moves for block arguments always go at the
end of a block with one successor.)

8.3 Block 0 is the entry. It has no predecessors and no parameters; the
function's arguments are its `param` instructions.

8.4 A block with `VB_ADDR` is a possible target of every `ibr` in the function
and must be listed by each. It has no parameters, must not be removed while a
`labeladdr` or an asm operand refers to it, and its edges from `ibr` are never
split.

8.5 Definitions dominate uses: an operand must be a constant, or be defined
earlier in the same block, or in a block that strictly dominates the user. A
`br` argument must dominate the end of the `br`'s block. A `proj` of a
terminator (`asmgoto`) must be in a block that the terminator's block strictly
dominates, and every target of an `asmgoto` must have that block as its only
predecessor.

8.6 **Inline assembly.** The template, constraint strings and clobber list
live in an asm template table (in the compiler: `cc`'s `casm` records; in a
unit: `asm` declarations). Operand list at `extra[b]`: `nout, nin`, then per
output a descriptor word, then per input a descriptor word and a value. A
descriptor is the operand's index in the template. Register outputs are the
instruction's results, in order (`void` for none, one result directly for one,
`multi` and `proj` for more). A memory operand's input value is its address
(`ptr`). The constraint alternatives are resolved at register allocation
(R16). Effects: reads all inputs; if the clobber list has `memory` or any
operand is a memory operand, C without the "may not return" part; otherwise P
with respect to memory but never removed or duplicated.

8.7 Loops may run forever, and an infinite loop without effects is defined
behaviour (it must not be removed).

## 9. ABI signatures

9.1 A **signature** describes how a function type is passed on the target. It
is produced by the target (`LO(classify)`) when the function or call is built
and is opaque to passes. Layout at its extra offset:
`ngroups | fl<<16` (`fl`: bit 0 variadic), the result group, then `ngroups`
argument groups. A group is two words:

- word 0: `kind` (bits 0-3), `n` pieces (4-7), four piece classes of 4 bits
  each (8-23), flags (24-31: bit 0 unnamed/variadic argument);
- word 1: for memory kinds, `size` (bits 0-27) and log2 `align` (28-31).

| Kind | Meaning | Values in a `call`, `ret` or `param` |
|---|---|---|
| `VG_NONE` | Nothing passed (void result, empty record) | None |
| `VG_REG` | `n` pieces in registers (1-4) | One value per piece |
| `VG_MEM` | By value in the argument area, `size` bytes | One `ptr`: the bytes to copy (call) or their address (param) |
| `VG_SRET` | Result returned in memory the caller provides | One `ptr`: the result object; the result group only |

Piece classes: `VC_INT` (value type `i64` or `ptr`, a full register),
`VC_F32`, `VC_F64`.

9.2 `call`: `extra[c]` = number of value words, then the values for each
argument group in order; if the result group is `VG_SRET`, its `ptr` comes
first. The call's `ty` is `void` for `VG_NONE` and `VG_SRET`, the piece's type
for one piece, `multi` for more.

9.3 `param g, p` in block 0 gives piece `p` of argument group `g`, or with
`p` = 0 the address of a `VG_MEM` group's bytes (which belong to the callee
and behave as a slot that is exposed) or the `VG_SRET` pointer when `g` is the
result group (written as group index `0xffff`).

9.4 `ret`: `extra[c]` = n, then one value per piece of the result group. A
function whose result group is `VG_SRET` returns no values; the target returns
the pointer where its ABI requires.

9.5 How groups map to registers and stack offsets is the target's
(`vir_target.assign`), applied identically for callers and callees. Passes
must not reorder, split or merge groups.

9.6 Call flags: `NORETURN` (control does not come back; the block must end in
`trap`), `PURE` (reads memory, writes none, returns), `CONST` (reads no memory
either). A call without them may read and write every exposed object and may
not return.

## 10. Effects and what passes may do

10.1 A transformation is correct if it is a **refinement**: for every
execution of the new function there is an execution of the old one that either
has the same observable behaviour or reaches immediate UB. Observable
behaviour is: the sequence of calls with their argument values and the exposed
memory they can read, volatile and atomic accesses, the returned values and
exposed memory at return, a `trap`, and non-termination.

10.2 Rules that follow, stated so passes can be reviewed against them:

| Action | Allowed when |
|---|---|
| Remove an instruction whose result is unused | Its effects are within P, R, U (never W, C, A, a volatile access, `barrier`, asm, an F operation under `VF_FENV`) |
| Replace an instruction by an equal earlier value | Same `op`, `ty`, `fl`, operands; effects P; or R with no possible write to the location in between (10.3); the earlier one dominates |
| Execute an instruction where it was not executed before | Effects P only, UB flags cleared |
| Replace a `load` by the value last stored | Same address value and width, no possible write in between; a narrower store is converted with `ext`/`trunc`/`bitcast` |
| Remove a `store` | Not volatile, and the bytes are overwritten before any possible read, or the slot is not exposed and never read afterwards |
| Fold a branch | The condition is a constant; a `switch` on a constant; an `ibr` on a `labeladdr` |
| Remove a block | It is unreachable and has no `VB_ADDR`, or its `VB_ADDR` has no remaining `labeladdr` or asm reference |

10.3 **Possible write** to a location L between two points: any `store`,
`memcpy`, `memset`, atomic write or `va_start`/`va_arg` whose pointer may
access L under 6.3-6.5 and whose byte range is not provably disjoint; any
call or asm with memory effects, and `barrier`, if L is exposed. Two accesses
based on the same slot or symbol with constant offsets are disjoint when their
ranges do not overlap, and so are two accesses whose addresses are the same
value plus constant offsets with ranges that do not overlap. Everything else
may overlap.

10.4 No pass may iterate to a fixpoint without a budget. A pass that exceeds
its budget stops and leaves the function in canonical form (R14).

## 11. Text form

11.1 The text form is for tests, dumps and the interpreter. Printing a
canonical function and parsing the result gives a function that prints
identically. Encoding is UTF-8; names are ASCII.

11.2 Lexical elements: a comment is `;` to the end of the line. Names:
`@name` symbol, `%name` signature, `$N` slot, `!N` asm template, `bN` block,
`vN` value (`N` any run of letters, digits, `_` and `.`; at least one). Integers are decimal
or `0x` hexadecimal, optionally negative. Floats are C hexadecimal floats
(`0x1.8p+1`) or `bits:0x...` for an exact pattern. Strings are in double
quotes with C escapes.

11.3 Grammar (EBNF; `{}` repetition, `[]` option):

```
unit      = "vir" INT NL "target" TARGET NL { decl } .
TARGET    = "x86_64-linux" | "arm64-linux" | "arm64-darwin" .
decl      = sigdecl | asmdecl | datadecl | externdecl | func .
sigdecl   = "sig" SIG "=" "(" [ group { "," group } [ "," "..." ] ] ")" "->" group NL .
group     = "none" | piece { ":" piece } | "mem" INT "align" INT | "sret" .
piece     = "int" | "f32" | "f64" .
asmdecl   = "asm" ASM STRING "outs" "(" [ strs ] ")" "ins" "(" [ strs ] ")"
            "clobber" "(" [ strs ] ")" [ "labels" INT ] NL .
datadecl  = ( "data" | "tdata" ) SYM "align" INT "=" "{" item { "," item } "}" NL .
item      = TYPE const | "zero" INT | "bytes" STRING .
externdecl= "extern" SYM NL .
func      = "func" SYM SIG { fflag } "{" NL { slotdecl } block { block } "}" NL .
fflag     = "nofp" | "fenv" | "nullinvalid" | "noreturn" | "local" | "exposure" .
slotdecl  = "slot" SLOT "size" INT "align" INT [ "hint" INT ] { "param" | "exposed" } NL .
block     = BLOCK [ "(" bp { "," bp } ")" ] { bflag } ":" NL { instr NL } term NL .
bp        = VALUE ":" TYPE .
bflag     = "loop" | "addr" | "cold" .
instr     = [ VALUE ":" TYPE "=" ] OP [ "." suffix ] [ operands ] { iflag } .
            (* a constant op (`iconst`, `fconst`, `symaddr`, `slotaddr`, `labeladdr`) written as an
               instruction, `v7:i32 = iconst 5`, names a constant of the constant region as written:
               it is not unified with an equal one and need not be used. The printer never writes
               one; tests use them to build the duplicate and unused constants that `vir_canon`
               removes and the verifier rejects (S2, N1). `bparam` written as an instruction is a
               stray block parameter (S4). *)
operand   = VALUE | const [ ":" TYPE ] .
const     = INT | FLOAT | SYM [ addend ] | SLOT [ addend ] | "&" BLOCK .
iflag     = "nsw" | "nuw" | "exact" | "nz" | "volatile" | "align" INT | "tag" INT
          | "noreturn" | "pure" | "const" | "likely" | "unlikely" .
```

11.4 Instruction syntax by group:

```
v3:i64 = add v1, v2 nsw
v4:i32 = icmp.slt v1, 10
v5:i64 = ext.1s v4                      ; low byte, sign-extended
v6:i32 = load.2u v9 align 2             ; 2 bytes, zero-extended
store.4 v9, v6 align 4 volatile
memcpy v1, v2, 24 align 8
v7:i64 = axadd.8 v1, v2
v8:i64 = call @f %s0 (v1, $0+8) pure
v8:multi = call v2 %s1 (v1)
v9:i64 = proj v8, 0
v10:ptr = param 2.0
v11:multi = asm !3 outs(0, 1) ins(2 v1, 3 v2)
br b2(v3, v4)
brif v4, b3, b4 likely
switch v1, b9 [0: b1, 1..5: b2]
ibr v1 [b3, b4]
asmgoto !4 outs() ins(0 v1) -> b5 [b6]
ret v3
trap
v12:ptr = va_arg v1, int:f64            ; the group as in a signature
v13:ptr = tlsaddr @tls, 0               ; symbol, model
v14:i64 = param 0xffff.0                ; the sret pointer of the result group
memcpy v1, v2, 24 align 8 align 4       ; two alignments: destination, source
br b2                                   ; no parentheses when the block has no parameters
```

The number of arguments of a `br` is the target's number of parameters (8.1);
a text that passes another number is a syntax error, not a verifier failure.
A `call` names its signature as `%name` after the callee; a memory operand
of `asm` is a value of type `ptr` among the inputs (8.6).

11.5 Constants are written in operand position; the parser creates the
constant instruction. A constant's type is the type the operand position
requires; where the position does not fix it (call arguments, `store` values,
`ret` values, asm inputs, the condition of `brif` and `select`, the operand of
`switch`, `ext` and the conversions, both `icmp` or `fcmp` operands constant),
the `:type` suffix is required, and the printer writes it exactly there. The
operands of `br` take the type of the target's parameters. `vir_opnd_fixed`
(the opcode table) answers the question for the parser and the printer. An
address constant (`@x`, `$N`, `&bN`) is a `ptr` whatever the position, and the
parser does not require its suffix.

11.6 The printer names values `v0, v1, ...` and blocks `b0, b1, ...` in order
of appearance, and writes instruction flags in the order of 11.3. Constants
get no name. Signatures are named `%s0 ...` and asm templates `!0 ...` by
their number in the unit (the parser unifies equal signatures). The unit is
printed as: `vir`, `target`, `extern`s, data, signatures, asm templates,
then the functions, each after a blank line. Floats are printed as a
hexadecimal float when that is exact for a normal number, otherwise as
`bits:`.

11.7 `vpp -fvir-dump=STAGE[,FUNC]` prints functions after a stage: `build`,
`simplify`, `gvn`, `mem`, `dce`, `alloc`. In a dump a `; loc FILE:LINE` comment
follows an instruction that has a location.

## 12. Verifier

`vir_verify(f)` checks every rule below and reports the first failures with
function, block, instruction and rule id. It runs after the builder and after
every pass in sanitizer builds and under `VCC_VIR_VERIFY=1`; a failure is an
internal compiler error with a dump.

| Id | Rule |
|---|---|
| S1 | `version` is 1; index 0 is `nop`; regions are ordered as in 2.3 |
| S2 | Constant region: only constant ops, no duplicates, field rules of 5.2; `slotaddr` and `labeladdr` targets exist |
| S3 | Blocks tile the block region; each has `n` at least 1; exactly one terminator, last |
| S4 | The first `nparam` instructions of a block are `bparam`, and no other is |
| S5 | `param` only in block 0, with a valid group and piece and the type that the piece class requires |
| T1 | `op` is a 1.0 opcode; `ty` is allowed for it (7.3) |
| T2 | Every value operand exists and has the type the table requires |
| T3 | Flags: only bits listed for the op; widths and alignments in range; `INB` and `TAIL` are 0 |
| T4 | `load`/`store` width fits the type (7.3); `ext` width does not exceed its operand |
| T5 | `memcpy`/`memset` size is an `iconst` |
| T6 | `proj`: producer is `multi`, index in range, type equals the result piece's |
| C1 | Every block is reachable from block 0; block 0 has no predecessors and no parameters |
| C2 | Every block other than block 0 comes after its immediate dominator |
| C3 | A `br` supplies exactly the target's parameters with equal types |
| C4 | Targets of `brif`, `switch`, `ibr`, `asmgoto` have no parameters |
| C5 | `switch` cases sorted, disjoint, lo not above hi |
| C6 | Every `VB_ADDR` block is listed by every `ibr`; an `ibr` lists only `VB_ADDR` blocks |
| C7 | Every `asmgoto` target has the `asmgoto` block as its only predecessor |
| D1 | Definitions dominate uses (8.5) |
| A1 | `call`: argument words match the signature's groups in number and type; `ty` matches the result group |
| A2 | `ret` values match the function's result group |
| A3 | A `NORETURN` call is followed only by `trap` |
| A4 | Asm: operand counts match the template; a memory operand's value is `ptr` |
| F1 | `VF_NOFP`: no `f32`/`f64` value and no `fconst` |
| F2 | Not `VF_VARIADIC`: no `va_start` |
| M1 | Slot `align` is a power of two; `size` fits; `VS_EXPOSED` equals the result of 6.4 when the function claims it is computed |
| N1 | Canonical form (2.4): no `nop` in blocks, no unused constant, constants in first-use order |

Details the table leaves out, as implemented:

- A failure is reported as `@func bN.I: RULE: message` (block N, instruction I
  of the block; `@func bN` and `@func` for a block or the function). The
  verifier stops after the structural rules (S1-S5, T2's reference checks)
  when the graph cannot be built, and checks C, D, A, F, M and N only then;
  N1 is checked only when nothing else failed.
- T2 also covers a reference that is out of range: a block id that is not a
  block, an operand list that runs past `extra`, a value operand that is
  `void` or not a value.
- T3 includes the condition codes (`icmp` 0-9, signed codes not on `ptr`;
  `fcmp` 0-7). T4 also rejects `S` on a load that is not an integer.
- C3 compares types only: the number of `br` arguments is the target's
  number of parameters by definition (8.1).
- A1 includes that each signature is well formed (register groups of 1-4
  pieces with classes, memory groups with a size, `sret` only as the result
  group, a result that is not a memory group). A2 checks the function's own
  signature the same way.
- A4: for a template with `a` register outputs, `b` memory outputs and `c`
  inputs, the instruction has `nout = a` and `nin = c + b` (the address of a
  memory output is an input value); a template with labels is `asmgoto` with
  as many labels, and `asm` has none; the result is `void`, one value, or
  `multi` for `nout` 0, 1, more. An operand whose constraint contains `m`,
  `o`, `V` or `Q` is a memory operand. It needs the unit's template table.
- D1 for the result of an `asmgoto`: the block that uses it must be strictly
  dominated by the block of the `asmgoto`.
- M1's exposure check runs when the function has `VF_EXPOSURE`.

## 13. Interpreter (`virrun`)

13.1 The interpreter is the reference for sections 5-10. Where this text and
the interpreter disagree, one of them has a bug; tests are written against the
text.

13.2 It executes a unit: `virrun FILE [--entry=@main] [--arg=N ...]
[--seed=N] [--steps=N] [--trace]`. Arguments are `i64` values for the entry's
integer pieces.

13.3 State: a 64-bit address space in which every object (data symbols, one
instance of each slot per activation, heap blocks) gets a distinct range with
unmapped gaps between them; per `ptr` value, its basis (an object, or the
exposed set); per object, whether it is exposed and whether it is live.

13.4 Every case of immediate UB in this document is detected and reported as
`UB: <rule> at @func bN vM`, exit status 2: division by zero, flag violations,
`f2s`/`f2u` out of range, an access outside the object a pointer is based on,
an access through an exposed-set pointer to an unexposed or dead object, an
unmapped address, overlapping `memcpy`, an `ibr` to an unlisted address, a
null access under `nullinvalid`.

13.5 Unspecified values are drawn from a generator seeded by `--seed`:
uninitialized bytes, NaN signs and payloads. A test that must not depend on
them is run with two seeds and the outputs compared.

13.6 Built-in externs, the only observable channel besides the exit status:
`@vir.out_i64(i64)`, `@vir.out_f64(f64)`, `@vir.out_bytes(ptr, i64)`,
`@malloc`, `@free`, `@abort`, `@exit`. Volatile accesses are logged as events.
A call to any other extern, `asm`, `asmgoto`, `tlsaddr`, `frameaddr` and
`retaddr` stop the run with `unsupported`, exit status 3.

13.7 Calls between functions of the unit pass group values directly; no
register assignment is modelled. `va_start`/`va_arg` use an interpreter list
of the unnamed groups.

13.8 `--steps` bounds the number of executed instructions (default 10^8);
reaching it reports `timeout`, exit status 4. A `trap` reports `trap`, exit
status 5. Otherwise the exit status is 0 and the output is the event log.

13.9 **Equivalence check** (`virtest --equiv`): run the function before and
after a pass on the same inputs and seeds. Pass if the "before" run reports UB,
or both logs and results are equal. Timeout on either side is inconclusive
and reported separately.

## 14. Passes

Every pass takes a canonical, verified function and returns one that is a
refinement of it. A pass is a plain function `bool vir_NAME(vir_fn *)` that
returns whether it changed anything. Order at `-O2`: `simplify`, `gvn`, `mem`,
`simplify`, `dce`.

14.1 **Append-time folding** (part of the builder and of every pass that
creates instructions, `vir_emit`): an instruction with effects P whose value
operands are all constants is replaced by its constant, except: a NaN result;
anything with a UB flag whose condition holds. Identities applied: `x+0`,
`x-0`, `x*1`, `x*0`, `x&x`, `x|x`, `x^x`, `x&0`, `x|0`, `x^0`, shifts by 0,
`ext` of a value already known to fit, `trunc(ext)`, `select` on a constant or
with equal arms, `icmp` of a value with itself, `ptradd p, 0`, `ptradd` of a
`slotaddr`/`symaddr` and a constant (folded into the addend). Then a
per-block hash gives each P instruction one definition per block.

14.2 **simplify:** removes a block parameter all of whose arguments are the
same value or the parameter itself; folds `brif`/`switch`/`ibr` on constants;
removes unreachable blocks; merges a block with its only predecessor when that
predecessor ends in `br` to it; re-runs 14.1 on changed users. Budget: each
instruction is revisited at most 4 times.

14.3 **gvn:** one walk over the dominator tree with a scoped hash table;
effects P only; replaces by the dominating equal instruction (10.2). Keys
include `fl`; when two instructions differ only in UB flags the survivor keeps
the intersection.

14.4 **mem:** computes `VS_EXPOSED` (6.4); per block, then across blocks with
a single predecessor chain, forwards stored values to loads and removes
overwritten and unread stores (10.2, 10.3). No memory SSA in 1.0.

14.5 **dce:** removes unused removable instructions (10.2) to a fixpoint by
worklist; removes unused block parameters with their arguments.

14.6 `vir_canon`, `vir_cfg` (predecessors), `vir_dom` (immediate dominators,
Cooper-Harvey-Kennedy), `vir_uses` are utilities, not passes.

## 15. Register allocation and lowering

15.1 The target describes its registers:

```c
typedef struct vir_regs {
	uint64_t alloc[2];	/* allocatable registers per class: 0 integer, 1 float */
	uint64_t callee[2];	/* callee-saved */
	/* fixed operands and clobbers of instruction v (x86 div, shifts, calls, params, ret);
	 * out arrives cleared: every register VR_NONE, nothing clobbered */
	void (*fixed)(const vir_fn *f, uint32_t v, vir_fix *out, void *ctx);
	void *ctx;
	const uint8_t *opfix;	/* per opcode: nonzero if fixed() can say anything (NULL: always ask) */
} vir_regs;

typedef struct vir_fix {
	uint8_t nopnd;		/* operands described in opnd[] */
	uint8_t res;		/* the result's register, VR_NONE: any */
	uint8_t fl;		/* TIED: result in the first operand's register; COMMUT: the first
				 * two may be swapped; RESTMEM: operands past nopnd need no register */
	uint8_t opnd[24];	/* per value operand, in vir_oit order without `multi` ones */
	uint32_t mem;		/* bit i: operand i may be memory or an immediate */
	uint64_t clob[2];	/* written after the operands are read */
	uint64_t eclob[2];	/* written before: no operand without a fixed register may be in one */
} vir_fix;
```

A register is a byte: 0-63 the integer class, 64-127 the float class,
`VR_NONE` (0xff) none. The scratch register of each class (X7) is not in
`alloc`; edge moves name it as `VR_SCR0`/`VR_SCR1`. A fixed register must be
allocatable. `opfix` keeps the indirect call off the instructions that have no
constraints; at the target's discretion, an instruction whose constant operand
is encodable as an immediate reports that operand in `mem`.

15.2 `vir_alloc(f, loops, regs, opt, out)` produces, without changing the
function, in the thread's reservations (valid until its next call):
- per value, a spill slot or none. Spill slots are the allocator's own table
  (`out->nspill`, each 4, 8 or 16 bytes), not VIR slots: no instruction names
  them, and lowering adds them to the frame;
- per value operand and per result, in the order sweep 6 visits them, the
  register it is in at that instruction (one byte each; a value can be in
  different registers at different uses; `VR_NONE` for an immediate or memory
  operand and for an unused result). A block's parameters come first, at its
  top. A `br`'s bytes say where its arguments are before the edge's moves, for
  information: the edge's parallel move reads them;
- an **edit stream** in position order: reload from a slot, rematerialize a
  constant, move between registers, "operands swapped" for a commutative
  instruction;
- per control-flow edge that needs one, a parallel move already ordered with
  cycles broken (block arguments and the moves of 15.8 together); a location
  is a register or a spill slot (a block parameter that starts in its slot is
  stored by the edge);
- the set of used callee-saved registers, each with the first block using it.

Preconditions besides canonical form, checked: block 0's `param`s come
first, and the `proj`s of a call follow the call directly (both read
registers the ABI fills before anything else may run). The loop forest
(15.5) is an input; until `vir_canon` builds it (22.5), `vir_loops_build`
computes it from the block order.

Algorithm: a backward sweep builds liveness and next-use chains (15.5); a
forward sweep over the blocks assigns registers greedily, follows hints, and
when no register is free evicts the value whose next use is farthest (15.6).
There are no live intervals, no interference graph and no fixed point. The
reasons and sources are in `RESEARCH-regalloc.md`.

15.3 **Lowering** walks the blocks once and calls the target's operations with
the assigned registers, by the protocol in X7, applying the edits before the
instruction they are positioned at. After the definition of a value that has a
spill slot it emits the store to the slot (a block parameter's store is at the
top of its block). An edge's moves go before the `br` when the source block
ends in one, else at the top of the target when it has one predecessor, else
(a critical edge) into a block lowering creates for that edge: the branch
targets a new label, the moves follow, then a jump. A value used once, by the next selected instruction in
the same block, may be folded into that instruction by the target (an
immediate, an addressing mode, a compare into a branch). The target then runs
its existing record passes, frame layout and output.

15.4 `switch` is lowered by the shared strategy in `emit/opt/switch.c` (the
1.0 builder does not produce it, X5).
`trap` is the target's trapping instruction. A block that consists of one `br`
without moves produces no code.

15.5 **Liveness and next use** (sweep 5, backward, in the same sweep as
`dce`). Positions are instruction indices. The sweep keeps the live set `W` as
a sparse set (a list and a per-value index that validate each other).
- At a value operand `v` at position `p`: push one word on the **use stack**,
  the position of the next later use of `v` (`nu[v]`, 0 for none) with the top
  bit set when `v` is not in `W` (a **kill**); set `nu[v] = p`; add `v` to `W`.
  Constants are chained in the same way but are never in `W`.
- At the definition of `v`: remove it from `W`. `nu[v]` is its first use; 0
  means unused, and `dce` drops a removable instruction. `xcall[v]` records
  whether the nearest later call outside `VB_COLD` blocks is before `v`'s last
  use. Hints are recorded (15.7).
- At the bottom of a block `W` is the union of the live-in lists of its
  forward successors; at the top `W` is appended to the **live-in pool**.
- At the top of the target `t` of a back edge `s -> t` (`s >= t`) every `v` in
  `W` gets a **loop range**: from the first instruction of the head `H` of the
  innermost loop `L` that contains both `s` and `t`, to an end that depends on
  `v`. If `v`'s definition strictly dominates `H` (it is defined before the
  loop), the end is `L`'s end: `v` is live round the loop. Otherwise every path
  from `H` to `t` passes the definition again, so the edges into `H` do not
  carry `v`: the end is that of `t`'s group, the back edges of `L` to other
  blocks than `H` merged where their target..source ranges overlap. In a
  dispatch loop whose body has backward `goto`s, this keeps a value of one
  case from being live at the latch. `lend[v]` is the largest end, for linear
  mode. A value is live in a block when its live-in list has it or a range of
  its covers the block's first instruction; a use inside a range is not a kill
  (15.6). In irreducible flow `t` can be any entry of a merged range, and
  applying the rule only at the head loses a value that is live into `t` but
  not into the head. This replaces iteration.
- An edge `b -> t` into a loop `L` from before its head (the outermost such
  `L`) also brings into `W` the live-in lists of `L`'s back-edge targets, for
  the values defined before `t` whose definition dominates `b`. They are live
  around `L` without being in `t`'s list when their use is reached only past a
  back edge: an edge into the middle (irreducible), or into the head of a loop
  whose use is behind an irreducible entry. Without this rule a loop around
  `b` would not keep them. The dominance test drops values defined inside `L`
  (strict SSA: such a value cannot be live at `b`); taking them in made a value
  of one inner loop live across the whole enclosing loop.
- A value live at the top of a block that starts with everything in its slot
  (`VB_ADDR`, or a block with no earlier predecessor, 15.8) gets a slot when
  it is defined. That includes a value live around a loop that contains such
  a block, before or after the back-edge target (it is live there by `lend`
  without being in the block's list).
- A **loop** here is a range of the block array: for each edge to the same or
  an earlier block, target to source, with partially overlapping ranges
  merged. `vir_canon` builds the forest with the dominators (22.5). On
  irreducible control flow a value can be listed as live in a block its
  definition does not dominate; it is never read there (15.8).
- **Linear mode.** If the live-in pool would exceed its limit (the
  reservation, or `opt->pool` for tests) the sweep restarts without lists and
  without resetting `W` at block bottoms, and a value is live in block `B`
  exactly when its definition is before `B` and the later of its last use and
  `lend[v]` is at or after `B`'s first instruction. Always correct; a value
  then holds its register through a branch arm that does not use it.
- `dce` is fused into this sweep from V5 on; until then a result without a
  use is assigned a register that is freed at once.

15.6 **Assignment** (sweep 6, forward). State: `regval[r]`, the value in a
register; `reg[v]`, valid when `regval[reg[v]] == v`; `slot[v]`; `nu[v]`, kept
current by popping the use stack in the reverse of the order it was pushed.
At an instruction, the use stack is popped per operand first (so `nu` names
the next use after this one), then in order:
1. the fixed operands the target reports (15.1, X7), as a parallel move: an
   operand whose required register holds another fixed operand that is not
   placed yet waits for it (an argument that shifts from one argument register
   to the next is copied before its register is written); in a cycle the
   first one goes, and the value in its register is moved aside. A live value
   in a required register is moved to a free register, else evicted; the
   operand is copied into its required register (it stays where it was too);
2. each other value operand that is not in a register gets one (free, else by
   eviction) and a reload or rematerialize edit, unless the target reports
   that this operand may be memory or an immediate and it is a constant or
   has a slot; an operand in an `eclob` register moves out;
3. the registers of killed values are freed, and the copies of step 1;
4. clobbers: a live value in a clobbered register moves to a free register
   that is not clobbered, else is evicted (at a call: every value in a
   caller-saved register that is live after it);
5. the result takes its fixed register, else its hint if that register is
   free, else a free register, else one freed by eviction. For an operation
   whose result must be in its first operand's register (X7): that register
   if the operand was killed; else, if the operation is commutative and the
   second operand was killed, the operands are swapped; else its own
   register, and lowering copies; and if every register holds an operand, the
   first operand is evicted and the result is computed in place.

**Eviction** takes the value with the largest next-use distance among the
registers of the class that the instruction does not use. The distance is
`nu[v] - p`, except: when `nu[v]` is 0 or beyond the innermost loop around `p`
and `v` was defined or last used inside that loop, the next use is the loop's
end; a use beyond the innermost loop has 100000 added per loop left. At equal
distance a constant goes first. Evicting writes no code: it gives `v` a slot
if it has none and clears the register. A value with a slot is stored once,
after its definition (15.3), so its slot is current wherever it is live.

A slot is shared with an earlier value of the same size class whose linear
range ended before `v`'s definition (bounded probe of a free list).

At a loop header, when the loop's largest `W` (recorded by sweep 5, the loops
inside included; it counts the values live through the loop) exceeds the
class's registers, that many values of the entry state without a use before
the loop's end leave it (they get a slot). In the other direction, a live-in
value that reaches the header in its slot and has a use before the loop's end
takes a register in the entry state, soonest use first (at most 16), so the
edges into the loop load it once instead of the body on every round. It does
so while the registers in use, plus the loop's largest `W` beyond the values
live at the header, plus one, stay below the class's registers; in a loop
with a call outside `VB_COLD` blocks only callee-saved registers count and
are taken. Constants are not taken in this way: measured on sqlite it made
more rematerializations on the edges than it saved (PLAN-vir.md). At a call
every value in a caller-saved register that is live after it is evicted.

15.7 **Hints.** `hint[v]` is a register or a value whose register `v` should
share: the ABI register for a call argument that is not live after the call;
the return register for a returned value; a required register (15.6 step 1);
for a `br` argument, the block parameter it feeds (the first argument assigned
sets the parameter's tentative register; a loop header's parameter is assigned
before its back-edge argument); and the hint of a result-in-first-operand
instruction is copied to that operand if it has none. A value with `xcall`
prefers a free callee-saved register, one the function already uses first (in
a caller-saved one it is moved to a callee-saved one at the call anyway, or
stored and loaded). A hint is taken only when the register is free.

15.8 **Blocks and edges.** A block with one predecessor starts from that
predecessor's end state without the values that are not live in. A block with
several starts from the end state of one already visited predecessor: the one
with the most live-in values in registers, then the deepest loop, then
not `VB_COLD`, then the preceding block; that edge needs no moves unless the
block has parameters. A block with no earlier predecessor (irreducible) starts
like a `VB_ADDR` block. Every other edge gets a parallel move
that produces the entry state: a move for a value in another register, a
reload for a value only in its slot, nothing for a value the entry state has
in its slot; block arguments are part of the same parallel move. A move whose
source is neither in a register nor has a slot is omitted (15.5). A `VB_ADDR`
block starts with every live-in value in its slot and none in a register. A
cycle is broken with a free register (one the target's entry state does not
use), else an exchange of two registers, else the scratch register (a cycle
through a slot); a move an exchange already did is dropped.

**Webs.** A block parameter and its `br` arguments (not constants) are one
variable, a web (union-find). Two webs are not joined when both have a
parameter of the same block, since those are live together at its top. A member
leaves its web when another member is live at its definition. At a parameter,
the members live there leave instead of the parameter. After sweep 5, the
members a member's loop ranges cover leave too. The members that stay share:
- one spill slot, given the first time one of them needs a slot and never
  returned to the free list, so an edge moves nothing for an argument that is
  already stored there;
- one register: the first register a parameter of the web takes, which
  `choose` returns after the hint when it is free and the value is not about
  to cross a call;
- the parameter's entry: a web parameter that crosses a call and is not used
  before one (`CALLFIRST`), or is only passed on to the block's `br`, enters in
  the web's slot. One that is only passed on to a loop header's parameter
  already placed takes that parameter's place, register or slot.
On the edge, an argument stored in the web's slot needs no move to a parameter
that enters in it. Effect on `sqlite3VdbeExec` (1664 functions of sqlite3.c
through VIR): stack accesses fell from 5197 to 2587. The dispatch latch is
`add x25, x25, #24` in place. Each of its predecessors used to reload three
values and copy `pOp`; now they store only a changed `rc` in its web slot.
Cycles of speedtest1 did not change measurably (9.00 G before, 9.05 G after;
instructions 33.78 G to 32.51 G).

15.9 **Constants** (`iconst`, `fconst`, `symaddr`, `slotaddr`, `labeladdr`)
are never stored: they are materialized into a register at a use that needs
one, stay there like a value, are dropped on eviction and recomputed on
reload. One is kept across an edge only into a block with one predecessor, or
into a loop header when its next use is inside the loop. No other value is
rematerialized in 1.0.

15.10 **Inline assembly** (after X6). The operands' classes, fixed registers,
tied outputs and the clobber mask are constraints as in 15.6 step 1. An
operand that allows both a register and memory takes a register if its value
is in one or one is free; else memory through the value's slot; else a
register by eviction. An early-clobber output takes a register no input of
the instruction holds. Operands that cannot be satisfied with every other
value evicted are an error.

15.12 **Shrink-wrapping** (on request: `vir_aopt.shrink`; numbered after 15.11, which
follows). The **save point** S is the block that dominates every block that
needs the frame (a call the target does not report as needing none, `memcpy`,
`memset`, inline assembly, `va_start`, `frameaddr`, `retaddr`, `tlsaddr`, an
operand that is a slot's address, a `VB_ADDR` block), is in no loop, and
dominates every block reachable from it, blocks that only return (through lone
`br`s) excepted. There is none when that is the entry block. In the blocks S
does not dominate a callee-saved register is chosen only when no other is
free, and a fixed result is not moved to one at its definition. At S the
values in caller-saved registers that live across a call move to free
callee-saved ones, by edits before S's first instruction. The output names S
(`spoint`); the lowering places the prologue there and decides, from the code it
produced, whether the blocks in front can run without the frame.

15.11 **Checking.** `vir_alloc_check(f, regs, out, msg)` walks the function
forward with the allocator's output, tracking which value each register and
slot holds (a forward dataflow over locations, meet by intersection, to a
fixed point, then one checking pass); every operand must read its own value
on every path. It also checks the fixed registers, `eclob`, the tied rule, that
a swap is only on a commutative instruction, block parameters at edges, and
that every callee-saved register written is in the reported set. It runs
after allocation whenever the verifier does (section 12), and `fuzz-vir` runs
the allocator with a random number of registers. `viralloc` and
`bench/vir-alloc.py` run it on a synthetic target (any number of registers,
x86-like constraints) until V2 brings real input.

## 16. Tools

| Tool | Use |
|---|---|
| `virtest [--no-verify] [--bless] FILE` | Parse, run the passes named in the file (only `canon` until the passes exist), verify, print, and parse and print the result again (11.1); `--equiv` adds 13.9 (with the interpreter); `--bless` rewrites the expected output |
| `virrun FILE` | Section 13 |
| `virgen --seed=N [--size=N]` | A random verified unit using the built-in externs only, for `--equiv` and for `fuzz-vir` |
| `vpp -fvir-dump=...` | 11.7 |
| `ccs/fuzz/fuzz-vir` | libFuzzer over the parser, verifier, passes and interpreter: parse must not crash; a verified function must stay verified and equivalent after each pass |

Environment: `VCC_VIR=1` enables VIR before it is the `-O2` default;
`VCC_VIR_VERIFY=1` verifies after every pass; `VCC_VIR_STATS=1` prints per TU
how many functions were built as VIR and the reason for each that was not;
`VCC_VIR_PASSES=a,b,...` overrides the pass list.

Reasons a function is not built (stable names, counted): `returns_twice`,
`alloca`, `wide_int`, `long_double`, `vector`, `asm_unsupported`, `tail`,
`naked`, `too_large`, `unimplemented:<what>`.

## 17. Tests

17.1 `ccs/vcc/tests/vir/*.vir`. A test file is a unit with directives in
comments at the top:

```
; PASSES: simplify,gvn
; EQUIV: @f 1 2 ; @f -1 0        (inputs for --equiv)
; EXPECT-UB: no
```

followed by the unit, a line `; ---`, and the expected output of
`virtest` (the printed unit, or the error lines). The unit is the text before
that line. `PASSES: canon` puts each function in canonical form before it is
verified; `VERIFY: no` is `--no-verify`; `DOM: yes` appends each function's
dominator tree (`; dom @f bN idom bM pre P post Q`) as comments. The tests
under `tests/vir/bad/` must make `virtest` exit with status 1; a file named
`bad/RULE-name.vir` (`RULE` a rule of section 12) must produce failures of
exactly that rule. `bench/vir-test.py -j 6` runs them all; `--bless PATTERN`
rewrites expectations; `--mutate N` runs random mutants of the units and
requires that `virtest` never crashes.

17.2 Required test sets before a step is done:
- parser/printer: every instruction and flag once; round trip identical;
- verifier: one failing unit per rule id in section 12 (`tests/vir/bad/`),
  each expecting that id;
- interpreter: one unit per instruction with boundary values, and one per
  immediate-UB case expecting its report;
- each pass: positive cases, and negative cases for every "allowed when"
  condition in 10.2 that the pass uses.

17.3 End-to-end checks are the existing C suites with `VCC_VIR=1`
(`PLAN-vir.md`, steps V2-V7).

## 18. Limits

| Quantity | Limit |
|---|---|
| Instructions per function | 2^28; beyond: reason `too_large` |
| Blocks | 2^24 |
| Block parameters | 65,535 per block |
| Slots | 2^24; size below 2^40 |
| Signature groups | 65,535; pieces per group 4; memory group size below 2^28 |
| `switch` cases | 2^20 (the walker's `WK_MAXCASE`) |
| Loop depth | Saturates at 65,535 |

## 19. Integration with the walker

Read against `origin/main` on 2026-11-11 (`emit/walk/walk.h`, `expr.c`,
`stmt.c`; `arch/x64/emit/lower.c`, `locals.c`, `abi.c`, `asm.c`). File and
function names below are from that tree. Each item states what the code does
today, what VIR needs, and the smallest change that gives it.

Summary:

| # | Subject | Change outside `vir/` | First needed |
|---|---|---|---|
| X1 | A second instantiation of the walker | A symbol-prefix mechanism for `emit/walk/` | V2 |
| X2 | Slot identity | One notification per frame allocation | V2 |
| X3 | Locals as variables | A new `lval` kind | V2 |
| X4 | Calls and parameters | None for building; two new target operations for good code | V4 |
| X5 | `switch` | None: the walker's lowering is kept | — |
| X6 | Inline assembly | Split `x64_asm` into planning and emission | its own step |
| X7 | Result registers | A hint operation and a table of fixed registers | V4 |
| X8 | Deciding whether to build VIR | A check before the walk; walks must be repeatable | V2 |
| X9 | 8-bit register numbers | None: VIR uses a bounded pool of names | V2 |

### X1. A second instantiation of the walker

**Today.** `walk.h` has `#define LO(name) x64_##name`. The walker's own
functions are `wk_*`, `VPRIV` (static in the unity TU, external with
`make VCC_UNITY=`). `emit/tu.c:do_item` picks `wk_function` for x86. Apart
from `x64_ecx_free` (`stmt.c:48`) the walker names no x86 function.

**Needed.** The same source compiled twice in one program: once with `LO` as
`x64_`, once as `vir_`, with distinct `wk_*` names, in both the unity and the
file-by-file build.

**Change (landed).** A generated header `emit/walk/prefix.h` that, when
`WK_PREFIX` is defined, maps every `wk_*` function the walker defines (130
names) to `WK_PREFIX` followed by the name, and `LO(name)` to `LO_PREFIX`
followed by the name. The paste needs two macro levels (`WK_PASTE`,
`LO_PASTE`), otherwise the prefix macro is not expanded. `walk.h` includes
`prefix.h` before `emitimpl.h`, because `emitimpl.h` declares
`wk_switch_values`. `stmt.c` named `x64_ecx_free` outside `LO()`; it is
`LO(ecx_free)` now. `bench/gen-walkprefix.py [--check]` regenerates the header
from the definitions (as `gen-diagids.py` does for diagnostics). The VIR
instantiation is one more file, `emit/vir/walk.c`, that sets the two prefixes
and includes `walk/*.c`. It must be its own translation unit, not part of the
`emit` unity TU: the walker files share types, macros and file-scope names
that a second inclusion in one TU would redefine (the mechanism was checked
only file by file, with `-DWK_PREFIX=vw_ -DLO_PREFIX=vw_`). This is the mechanism the arm64 move needs as well (PLAN-x86,
layer 2), so it is built once. Shared state (`wkst`, `ecx`) is not duplicated:
only one walker runs for a function at a time.

### X2. Slot identity

**Today.** `wk_pslot` (`stmt.c:110`) bumps `foff` and returns a frame offset.
`wk_slot` (`stmt.c:85`) reuses a free temporary slot of the same size and
alignment; `wk_local` (`expr.c:167`) does the same for locals of inline
copies. The target only ever sees `lval{L_FRAME, off}`. So the same offset
means different objects at different times, and nothing tells the target when.

**Needed.** Section 6 treats slots as objects (3.3, 6.8). The shared cell
analysis has the same requirement: `emit/rec/slots.h` says allocation
identities "must survive stack-offset reuse".

**Change.** One call each time an offset is handed out, fresh or reused:
`LO(slot)(e, off, size, align, kind)` from `wk_pslot`, both reuse paths of
`wk_slot`, and `wk_local`; `kind` is temporary, local, parameter or
whole-function (`WK_SLOT_TEMP`, `_LOCAL`, `_PARAM`, `_FUNC` in `walk.h`).
`wk_pslot_kind` is `wk_pslot` with a kind, so a hand-out through
`wk_slot` or `wk_local` is notified once. The static helper `x64_slot` in
`arch/x64/emit/fp.c` was renamed `x64_tmpslot` to free the name. `x64_slot`
does nothing today (landed) (and can feed `em_slot_*` ids
when OPT5 wants them). `vir_slot` creates a `vir_slot` with `hint = off` and
makes it the current object for that offset range; a later `L_FRAME` access is
resolved to the slot that currently owns its offset. Accesses spanning two
slots are an internal error.

### X3. Locals as variables

**Today.** `LO(local)(e, s, &l)` already lets the target claim a local:
`x64_local` (`locals.c:52`) answers with `lval{L_VREG, r}` for up to four
selected scalars and refuses inside inline copies (`e->wk->inst`). The walker
never looks at `L_VREG`; it passes the `lval` back to `LO(load)`/`LO(store)`.
`lval.r` is 8 bits.

**Needed.** Every scalar local without `lnoreg` as an SSA variable, including
those of inline copies, with no limit of 256.

**Change.** A new kind `L_VAR` in `emitimpl.h`'s `lval` (landed; `x64_load`,
`x64_store` and `x64_laddr` fail with an internal error on it), with the variable
number in the existing 32-bit `ref` field. `vir_local` returns it for an
eligible local, keyed by (symbol, inline instance) so recursive expansion
gets separate variables. `vir_load`/`vir_store` on `L_VAR` read and write the
variable (Braun: per-block current definition, block parameters at joins).
`LO(laddr)` on `L_VAR` is an internal error, as it is for `L_VREG` today;
`lnoreg` guarantees it does not happen. Eligibility is the existing shared
rule (`em_select_locals`' type and escape tests in `emit/opt/locals.c`)
without the top-K limit. No walker source changes beyond the enum.

### X4. Calls and parameters

**Today.** `wk_argslot` (`expr.c:1990`) stores every argument into a fresh
8-byte-word frame slot; `LO(call)` receives `wkarg[]` with those slots and
loads the argument registers itself. `LO(params)` (`abi.c:571`) moves incoming
registers to slots, or to the home `LO(local)` chose. A record result is
received in a slot. Because arguments wait in slots, no temporary is live
across a call.

**Building needs no change.** `vir_store` keeps, per block, the value last
stored to each frame range. `vir_call` reads each piece through that table and
gets the SSA value directly; when a label intervened it emits a `load` and the
`mem` pass forwards it. The stores to the argument slots become dead (the slot
is not exposed) and `mem` removes them. A memory-class argument passes its
slot's address (`VG_MEM`). The signature (section 9) is filled from the
`wkarg` array: `n`, `cls[]`, `mem`, `size`, `align`, `unnamed`. An argument
with `x87`, `vector` or `wide_record` set makes the function ineligible in 1.0.

**Lowering needs two operations** to keep values in registers at calls:
- `LO(callv)(e, ref, fn, a, n, variadic, r, rt, const val *pieces)`: as
  `LO(call)`, but each register-class piece is a `val` (register or constant)
  instead of a word in `a[i].slot`; the target performs the parallel move into
  its argument registers.
- `LO(paramv)(e, a, n, r, variadic, const uint8_t *home)`: as `LO(params)`,
  with the register each incoming piece must end up in (or none: leave it in
  its slot).

Until they exist (V2, V3) lowering stores to slots and calls `LO(call)` and
`LO(params)` unchanged, which is correct and slow.

### X5. `switch`

**Today.** `wk_switch` (`stmt.c:626`) picks a strategy with
`em_switch_strategy` and emits it through ordinary operations: a compare tree
(`LO(cmpbr)`), a bit-set test (`LO(switch_bitset)`), or a value table. The
target never sees a switch.

**Decision.** Keep that. The 1.0 builder therefore never produces the
`switch` terminator; it sees `brif` trees, and `vir_switch_bitset` expands the
recipe into `sub`, `lshr`, `and`, `brif`. The `switch` instruction stays in
the format, verifier, interpreter and `virtest` so that a later walker hook
(`LO(switch)` with the sorted case list) needs no format change. 15.4's
reference to `emit/opt/switch.c` applies from then.

### X6. Inline assembly

**Today.** `LO(asm)(e, n)` gets the AST node. `x64_asm` (`asm.c:214`, about
600 lines) does everything in one function: chooses a constraint alternative,
evaluates the operand expressions by calling back into the walker, places
values in registers and slots (`xopnd`), substitutes the template, emits the
raw text record, stores outputs, and handles `asm goto` edges. An empty asm
with only `memory`/`cc` clobbers is `LO(barrier)`.

**Needed.** For VIR the operands must be values before the template is
emitted, and registers must come from the allocator (8.6, 15.2).

**Change.** Split `x64_asm` in two, with a target-neutral description between
them:

```c
typedef struct em_asmop {
	uint8_t kind;		/* register, memory, immediate */
	uint8_t dir;		/* in, out, inout */
	uint8_t cls;		/* integer or float register class */
	uint8_t size;
	int16_t fixed;		/* a specific register the constraint names, -1 none */
	int8_t tied;		/* the output an input shares a register with, -1 none */
	uint32_t expr;		/* the operand's AST node */
	qty t;
} em_asmop;
bool LO(asm_plan)(ecx *e, uint32_t n, em_asmop *ops, uint64_t *clobber, bool *memory);
void LO(asm_emit)(ecx *e, uint32_t n, const em_asmop *ops, const val *in, val *out, const uint32_t *labels);
```

`asm_plan` selects the alternative and classifies; the walker (not the
target) evaluates the operand expressions; `asm_emit` substitutes and emits
with the registers it is given. `x64_asm` becomes plan, evaluate, emit in
sequence with unchanged output, which `bench/vas-corpus.py` and the kernel
objects check. `vir_asm` records the plan as an `asm`/`asmgoto` instruction.

**Scope.** This is the largest change outside `vir/`, and it touches code the
x86 kernel build depends on. It is its own step after register allocation.
Until then `LO(barrier)` is supported and any other asm makes the function
ineligible (`asm_unsupported`).

### X7. Result registers

**Today.** x86 operations are destructive on physical registers:
`x64_bin` (`lower.c:432`) brings `a` into a register, emits
`op ra, ra, rb`, frees `rb` and returns `a`. Operations that need a new
register take the first free one in a fixed order (`x64_treg`, `lower.c:139`)
from the `live` mask. Shifts, division, `rep movs` and compare-exchange use
specific registers.

**Needed.** After allocation every VIR value has a register; lowering must get
each result there without a copy per instruction, and the target's own scratch
registers must not hold a live value.

**Protocol** (no change to existing operations):
1. Before each instruction lowering sets the target's `live` mask to the
   registers holding values live across it (`tkeep`/`tfree`).
2. For an operation that returns its first operand's register: if the result
   register differs from that operand's, lowering emits `LO(mov)` into the
   result register first and passes that. The allocator hints the result to
   the first operand's register when the operand dies here, so the move is
   usually absent.
3. For an operation that allocates its result, lowering calls
   `LO(prefer)(e, r)` first: the next `treg`/`ftreg` returns `r` if it is
   free.
4. If the returned register is still not the assigned one, lowering emits
   `LO(mov)` and frees the other. `emit/rec/copies.c` removes what remains.

**Changes.** `LO(prefer)` (a few lines per target), and a table the allocator
reads through `vir_regs.fixed` (15.1): per operation and width, the registers
it writes or requires (x86: variable shifts `rcx`; division `rax`, `rdx`;
`copy`/`zero` `rdi`, `rsi`, `rcx`; compare-exchange `rax`; calls the
caller-saved set). The allocator keeps values that live across such an
instruction out of those registers and leaves two integer registers and one
float register unallocated as scratch.

### X8. Deciding whether to build VIR

**Today.** A walk has side effects in the emitter (`note_ref`, the asm serial
`e->asmk`, known-value state) and no restart path: `efatal` jumps to
`e->bail` and the function is dropped.

**Needed.** A function that VIR cannot handle must come out exactly as it
does without VIR.

**Change.** Eligibility is decided before any walk, by `vir_eligible(e, s)`,
from the AST and the prepass tables: a scan of the body for the node kinds and
types listed in 1.3 and 16 (asm other than a barrier, `setjmp`-like callees,
`alloca`/VLA, wide integers, `long double`, vectors, record arguments the
target passes specially). If it says no, `wk_function` runs as today. If it
says yes, the VIR walker runs and must succeed; a failure after that point is
an internal compiler error in development builds, and in release builds falls
back by walking again with the real target. That fallback requires the
walker's emitter side effects to be repeatable; `VCC_VIR_STATS` counts such
late failures and the target is zero.

### X9. 8-bit register numbers

**Today.** `val.r`, `val.r2` and `lval.r` are `uint8_t`, and the walker casts
`treg` results to it.

**Decision.** No change. The VIR target hands out names from a pool: 0-191
integer, 192-254 float, 255 none. A name is a variable for SSA construction
(`mov`, `ext` and reuse after `tfree` redefine it). The walker holds few
temporaries at once, so the pool does not run out in practice; if it does the
function fails with `too_large` (X8). `vir_nfree` reports the free count, so
the walker's "wait in a slot" paths (`wk_short`, `expr.c:1227`) are not taken.
Locals do not use these names (X3).

### What VIR's implementation of `LO` consists of

Of the 69 operations the walker calls, by how VIR handles them:

| Handling | Operations |
|---|---|
| One instruction, or a constant | `bin un cmp ext mov toreg fconst fbin fneg fsqrt fcmp cvt fbits umulhi smulhi bitop atomic tlsaddr frame trap barrier copy zero laddr load store va_start va_arg` |
| Control flow | `label jump cmpbr brz jumpr named ret retrec` |
| Bookkeeping only | `treg ftreg tfree tkeep nfree nffree dbgrow begin locals_begin local finish slot` |
| Signatures and calls | `classify call params` (the real target classifies) |
| Expanded into several instructions | `switch_bitset root4 pcmpstr` |
| Answered by the real target | `ldmem i128mem` |
| Function not eligible in 1.0 | `wconst wbitint wbin wun wcmp wcvt bigconst bigbin bigcmp bigcvt alloca spget spset setjmp longjmp tail_return asm` |

## 20. Open issues

| # | Issue | Proposed answer |
|---|---|---|
| O1 | Should `VC_INT` pieces carry `i32` values, leaving the upper bits to the ABI? | No in 1.0: the builder extends, as the walker does today |
| O2 | Two distinct symbols are treated as possibly the same object (aliases, weak symbols). Is a "distinct definition" flag worth it? | Later, with measurements from `mem` |
| O3 | `f2s`/`f2u` out of range as immediate UB differs from "defined by default" | Keep: no build flag defines it in C either |
| O4 | Bit-field extract and insert as dedicated ops (arm64 `ubfx`/`bfi`) | Not in 1.0; lowering matches shift-and-mask pairs |
| O5 | Atomic orderings other than sequentially consistent | Three flag bits reserved when the walker passes an ordering |
| O6 | A binary on-disk form for vccd's cache | After 1.0; the arrays are already position-independent |
| O7 | The alias tag field on `load`/`store` | Reserved as 0; its meaning is defined when the first pass uses it |

## 21. Traceability

| Research rule | Sections |
|---|---|
| R1 no undef/poison | 5.1, 6.1, 13.5 |
| R2 flags are immediate UB | 7.2, 10.2 |
| R3 pointers and exposure | 4.3, 6.3-6.7 |
| R4 slots, no lifetime markers | 3.3, 6.8, 6.9 |
| R5 one alias rule, tags as a field | 6.5, 10.3, O7 |
| R6 effects per opcode | 7.3, 10.2 |
| R7 interpreter as specification | 13 |
| R8 flat instructions, no use lists | 3.1, 3.6 |
| R9 nothing shared between functions | 2.1, 3.4 |
| R10 block parameters | 3.2, 8.2 |
| R11 locations in a side array | 3.4, 11.7, 22.7 |
| R12 two levels | 15.3 |
| R13 SSA on the fly | X1, X3, X9 |
| R14 fixed pass list, budgets | 10.4, 14 |
| R15 calls carry the classification | 9 |
| R16 asm constraints kept | 8.6, 15.2 |
| R17 one resolution point for `__builtin_constant_p` | Resolved by the walker before VIR; no VIR instruction |
| R18 `unreachable` is a trap | 8.1, 15.4 |
| R19 explicit width, alignment, volatility | 6.10, 7.2 |
| R20 one set of FP operations | 7.3, 7.4 |

## 22. Performance design

The format in section 3 decides what is stored; this section decides what
building and transforming it may cost. The numbers are budgets to be measured
in V1 and V2 with `bench/vbench.py`, not results.

### 22.1 Budgets

| Quantity | Budget | Reference today |
|---|---|---|
| Calls to `malloc`, `mmap`, `free` per function | 0 after the thread's first function | The walker and x86 lowering already work this way |
| Bytes cleared per function | Proportional to what the function used, never to capacity | — |
| Building, per VIR instruction (the `vir_*` operations only) | 30 ns | The arm64 emitter spends 103 ns per machine instruction in total (PLAN-fas2, status 2.6) |
| Register allocation, sweeps 5 and 6 together, per VIR instruction | 200 ns for now; the target is 60 ns (`RESEARCH-regalloc.md` 7), after code quality | 177 ns on an interpreter-shaped function, 81–83 ns on random functions (PLAN-vir V4) |
| Sweeps over the instruction array at `-O2`, build to lowered records | 7 | — |
| `-O0` and `-O1` | No VIR code runs; output byte-identical | — |

### 22.2 Where the memory comes from

- **Not the stack for anything that grows with the function.** A function can
  have millions of instructions, the emitter runs on up to 64 worker threads
  with ordinary stacks, and a stack array cannot be kept warm between
  functions. The stack is used for small bounded scratch with a fallback, as
  `wk_function` does with `pabuf[256]`: argument lists, a `switch`'s cases, the
  parallel-move worklist.
- **Per-thread reservations for everything else,** the emitter's existing
  idiom (`reserve()` in `emit/tu.c`: `mmap` with `MAP_NORESERVE`, made once per
  thread). VIR adds, per thread: two instruction arrays (source and
  destination of a copying sweep), `blk`, `slot`, `extra`, the location table,
  `tmp`, and the builder's tables below. Starting a function sets counts to
  zero. The pages a previous function touched are still mapped and in cache,
  so a small function builds entirely in memory that is already hot. This is
  as cheap as a stack frame (a pointer bump) without its limits.
- A function that exceeds a reservation fails with `too_large` (X8); nothing
  grows or moves.

### 22.3 Nothing is cleared

Every per-function table is validated by a generation number, the scheme the
walker uses for `sgen`/`kgen`: an entry carries the generation it was written
in and is empty if that is not the current one. Bumping one counter resets the
table. This applies to the constant table, the per-block CSE table, the
variable definitions, the frame-range tables and `tmp`. No `memset` of a
table, ever; arrays indexed by value are written before they are read.

### 22.4 The builder's tables

| Table | Shape | Cost per use |
|---|---|---|
| Current SSA value of a walker register name (X9) | `uint32_t cur[255]` plus the block it was set in | One load or store. `treg` gives a fresh instance, so nothing is searched |
| Definitions of an `L_VAR` local per block (X3) | Per variable, the newest (block, value) pair inline; older pairs in a bump-allocated chain | One compare when the read is in the block of the last write, the common case; otherwise a walk up single predecessors |
| Constants | Open-addressed table of value ids keyed by (op, ty, a, b, c), generation-stamped | One probe |
| Local CSE of pure instructions | The same table, with the block generation in the key | One probe; cleared by entering a block |
| Frame offset to slot (X2) | Array indexed by offset / 8, generation-stamped | One load |
| Last value stored to a frame range (X4) | Per slot: value and block generation | One load; this is what makes argument slots free |
| Label to block | Array indexed by the walker's label number | One load |

Braun's algorithm needs a block **sealed** (all predecessors known) before a
read in it can be answered without a provisional parameter. A label that is
jumped to only from above is sealed when it is placed. A label that may still
get a jump from below (a loop head, a user label with `LR_BACK`) stays open
until the function ends, and every variable read in it gets a provisional
block parameter. The finalizing sweep removes the ones that turn out to carry
a single value. If measurement shows the provisional parameters matter, the
walker can say when a loop is complete (one call at the end of each loop
statement); the format does not change.

### 22.5 From built to canonical in one sweep

The builder appends in the walker's order with a few `nop`s and provisional
parameters. One finalizing sweep (`vir_canon`) does all of the following
while copying to the second array (after a read-only loop over the operands
that finds the constants in order of first use and so how many there are): drops `nop`s and unreachable blocks,
resolves the alias table (a removed parameter becomes its value), writes
`br` argument lists, renumbers values, and orders constants by first use. When
the walker's order already has every block after its immediate dominator (true
for structured code that does not rotate loops) the block order is kept;
otherwise blocks are emitted in reverse postorder (the depth-first search
visits a block's successors last to first, so that the first successor comes
first) in the same sweep. Dominators
are computed once here (Cooper-Harvey-Kennedy on the block array) and stored
per block with preorder and postorder numbers, so "does A dominate B" is two
compares for every later pass.

### 22.6 Sweeps at `-O2`

| # | Sweep | Direction | Does |
|---|---|---|---|
| 1 | Build | Walker order | Folding, local CSE, SSA construction (14.1) |
| 2 | Canon | Forward, copying | 22.5 |
| 3 | `simplify` + `gvn` | Forward | One pass does both: the value table is global and a hit counts if its defining block dominates (22.5), so no scoped table and no dominator-tree walk |
| 4 | `mem` | Forward | Exposure, forwarding, dead stores |
| 5 | `dce` + liveness | Backward | One pass marks live instructions and builds the allocator's next-use chains and per-block live-in lists (a value live into a loop head is live to the loop end; no iterative dataflow), 15.5 |
| 6 | Allocation | Forward over blocks | 15.6-15.10; pops the use stack sweep 5 pushed |
| 7 | Lowering | Forward | 15.3; reads the operand registers and edits sequentially |

Section 14 describes `simplify`, `gvn` and `dce` as separate contracts; they
stay separately callable for tests (`VCC_VIR_PASSES`), and the default
pipeline fuses them as above.

### 22.7 Smaller choices

- **Source locations** are a run table, not a per-value array: one
  (first instruction index, location) pair each time the location changes,
  written by `vir_dbgrow`. A statement's instructions share one entry.
- **No `tmp` initialization:** a pass that uses `tmp` writes every entry it
  later reads, or stamps it.
- **`extra` is append-only** during building; lists are never edited in place
  (`vir_canon` rewrites the value and block words of the lists it keeps, each
  of which belongs to one instruction, and `vir_cfg` appends the predecessor
  lists, replacing those of its last call).
- **Use counts** are a `uint8_t` per value saturating at 255, computed in
  sweep 5 where the allocator and lowering need "used once".
- **No pass allocates** except from the thread's reservations; a pass that
  needs a worklist uses `tmp` as an intrusive list.
- **The verifier is not on this path:** it runs in test and sanitizer builds
  and under `VCC_VIR_VERIFY=1` only (section 12).

### 22.8 Measuring

- V1 adds `virgen`-driven microbenchmarks to `bench/vbench.py`: appends per
  second, canon, each sweep, in instructions executed per VIR instruction.
- From V2, `vbench.py ab emit` reports the emit phase with `VCC_VIR=1` against
  without, per corpus, and `VCC_VIR_STATS=1` prints per-sweep time and the
  count of provisional parameters created and removed.
- The gate from V2 on: with the flag off, emit within 0.1 % of the merge
  base; with it on, the numbers go into `PLAN-vir.md` with each step.
