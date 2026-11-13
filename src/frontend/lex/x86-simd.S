/* SysV x86-64 lexer kernels. Dispatch checks CPUID and XCR0 before entry.
 * The input mapping has padding for the lookahead load one byte past a block.
 * Classifiers take (bytes, first block, past-last block, mask pointers). */
#include "masks.h"
#ifdef __APPLE__
#define SYM(x) _##x
#define LOCAL(x) .private_extern x
#else
#define SYM(x) x
#define LOCAL(x) .hidden x
#endif

.macro function name
	.text
	.p2align 4
	.globl \name
	LOCAL(\name)
\name:
.endm

/* Each VBMI permutation selects a byte from the structure or group table.
 * Zero the result for indices outside the table (128 and 64 respectively). */
function SYM(lex_bytes_classify_avx512)
	cmpq %rdx, %rsi
	jae .Ldone512
	vmovdqu64 SYM(lex_structure_table)(%rip), %zmm0
	vmovdqu64 SYM(lex_structure_table)+64(%rip), %zmm1
	vmovdqu64 SYM(lex_group_table)(%rip), %zmm2
	vbroadcasti32x4 SYM(lex_low_nibble_table)(%rip), %zmm3
	vbroadcasti32x4 SYM(lex_high_nibble_table)(%rip), %zmm4
	vpbroadcastb .Lnibble(%rip), %zmm5
	vpbroadcastb .Lgroup_limit(%rip), %zmm6
	vpbroadcastb .Lprefix(%rip), %zmm7
	vpbroadcastb .Lpair(%rip), %zmm8
	vpbroadcastb .Ldouble(%rip), %zmm9
	vpbroadcastb .Lidentifier(%rip), %zmm10
	vpbroadcastb .Lwhitespace(%rip), %zmm11
	vpbroadcastb .Ldigit(%rip), %zmm12
	movq %rsi, %rax
	shlq $6, %rax
	addq %rax, %rdi
.macro store_mask k, index
	movq 8*\index(%rcx), %rax
	kmovq \k, (%rax,%rsi,8)
.endm
.Lblock512:
	vmovdqu64 (%rdi), %zmm13
	vmovdqu64 1(%rdi), %zmm14
	vpmovb2m %zmm13, %k0
	knotq %k0, %k1
	vmovdqa64 %zmm0, %zmm15
	vpermt2b %zmm1, %zmm13, %zmm15 {%k1} {z}
	vpcmpub $1, %zmm6, %zmm14, %k1
	vpermb %zmm2, %zmm14, %zmm16 {%k1} {z}
	vpandq %zmm15, %zmm16, %zmm17
	vpandq %zmm5, %zmm13, %zmm18
	vpshufb %zmm18, %zmm3, %zmm18
	vpsrlw $4, %zmm13, %zmm19
	vpandq %zmm5, %zmm19, %zmm19
	vpshufb %zmm19, %zmm4, %zmm19
	vpandq %zmm18, %zmm19, %zmm18
	vptestmb %zmm7, %zmm15, %k0
	store_mask %k0, LEX_MASK_PREFIX
	vptestmb %zmm8, %zmm17, %k0
	vptestmb %zmm9, %zmm15, %k1
	vpcmpeqb %zmm13, %zmm14, %k2
	kandq %k2, %k1, %k1
	korq %k1, %k0, %k0
	store_mask %k0, LEX_MASK_PAIR
	vporq %zmm13, %zmm15, %zmm19
	vpmovb2m %zmm19, %k0
	vptestmb %zmm6, %zmm17, %k1
	korq %k1, %k0, %k0
	store_mask %k0, LEX_MASK_HANDLER
	vptestmb %zmm10, %zmm18, %k0
	store_mask %k0, LEX_MASK_IDENTIFIER
	vptestmb %zmm11, %zmm18, %k0
	store_mask %k0, LEX_MASK_WHITESPACE
	vpmovb2m %zmm18, %k0
	store_mask %k0, LEX_MASK_NEWLINE
	vptestmb %zmm12, %zmm18, %k0
	store_mask %k0, LEX_MASK_DIGIT
	vpmovb2m %zmm16, %k0
	store_mask %k0, LEX_MASK_SIGN_NEXT
	addq $64, %rdi
	incq %rsi
	cmpq %rdx, %rsi
	jb .Lblock512
	vzeroupper
.Ldone512:
	ret

/* AVX2 PSHUFB looks up 16 entries per lane. Accumulate the matching
 * high-nibble row for each byte. Each iteration writes half a mask. */
function SYM(lex_bytes_classify_avx2)
	addq %rsi, %rsi
	addq %rdx, %rdx
	cmpq %rdx, %rsi
	jae .Ldone256
	movq %rsi, %rax
	shlq $5, %rax
	addq %rax, %rdi
	vbroadcasti128 SYM(lex_low_nibble_table)(%rip), %ymm0
	vbroadcasti128 SYM(lex_high_nibble_table)(%rip), %ymm1
	vpbroadcastb .Lnibble(%rip), %ymm2
	vpxor %ymm3, %ymm3, %ymm3
	vpbroadcastb .Ldouble(%rip), %ymm4
	leaq SYM(lex_structure_table)(%rip), %r9
	leaq SYM(lex_group_table)(%rip), %r10
.macro lookup table, rows, bytes, out
	vpand \bytes, %ymm2, %ymm11
	vpsrlw $4, \bytes, %ymm12
	vpand %ymm2, %ymm12, %ymm12
	vpxor %ymm13, %ymm13, %ymm13
	vpxor \out, \out, \out
	xorl %eax, %eax
.Lrow\@:
	vbroadcasti128 (\table,%rax), %ymm14
	vpshufb %ymm11, %ymm14, %ymm14
	vpcmpeqb %ymm13, %ymm12, %ymm15
	vpand %ymm15, %ymm14, %ymm14
	vpor \out, %ymm14, \out
	vpaddb %ymm4, %ymm13, %ymm13
	addq $16, %rax
	cmpq $(16*\rows), %rax
	jb .Lrow\@
.endm
.macro test_bits constant, bytes, out
	vpbroadcastb \constant(%rip), %ymm11
	vpand \bytes, %ymm11, %ymm11
	vpcmpeqb %ymm3, %ymm11, %ymm11
	vpmovmskb %ymm11, \out
	notl \out
.endm
.macro store_half index
	movq 8*\index(%rcx), %rax
	movl %r8d, (%rax,%rsi,4)
.endm
.Lblock256:
	vmovdqu (%rdi), %ymm5
	vmovdqu 1(%rdi), %ymm6
	lookup %r9, 8, %ymm5, %ymm7
	lookup %r10, 4, %ymm6, %ymm8
	vpand %ymm7, %ymm8, %ymm9
	vpand %ymm2, %ymm5, %ymm11
	vpshufb %ymm11, %ymm0, %ymm10
	vpsrlw $4, %ymm5, %ymm11
	vpand %ymm2, %ymm11, %ymm11
	vpshufb %ymm11, %ymm1, %ymm11
	vpand %ymm11, %ymm10, %ymm10
	test_bits .Lprefix, %ymm7, %r8d
	store_half LEX_MASK_PREFIX
	test_bits .Ldouble, %ymm7, %r11d
	vpcmpeqb %ymm5, %ymm6, %ymm11
	vpmovmskb %ymm11, %r8d
	andl %r8d, %r11d
	test_bits .Lpair, %ymm9, %r8d
	orl %r11d, %r8d
	store_half LEX_MASK_PAIR
	vpor %ymm5, %ymm7, %ymm11
	vpmovmskb %ymm11, %r11d
	test_bits .Lgroup_limit, %ymm9, %r8d
	orl %r11d, %r8d
	store_half LEX_MASK_HANDLER
	test_bits .Lidentifier, %ymm10, %r8d
	store_half LEX_MASK_IDENTIFIER
	test_bits .Lwhitespace, %ymm10, %r8d
	store_half LEX_MASK_WHITESPACE
	vpmovmskb %ymm10, %r8d
	store_half LEX_MASK_NEWLINE
	test_bits .Ldigit, %ymm10, %r8d
	store_half LEX_MASK_DIGIT
	vpmovmskb %ymm8, %r8d
	store_half LEX_MASK_SIGN_NEXT
	addq $32, %rdi
	incq %rsi
	cmpq %rdx, %rsi
	jb .Lblock256
	vzeroupper
.Ldone256:
	ret

/* Search 32 bytes at a time, returning the first matching byte offset. */
function SYM(lex_comment_end_find_avx2)
	vpbroadcastb .Lstar(%rip), %ymm0
	vpbroadcastb .Lslash(%rip), %ymm1
	vpbroadcastb .Lescape(%rip), %ymm2
	vpxor %ymm3, %ymm3, %ymm3
.Lcomment:
	vmovdqu (%rdi,%rsi), %ymm4
	vpcmpeqb %ymm0, %ymm4, %ymm5
	vpcmpeqb 1(%rdi,%rsi), %ymm1, %ymm6
	vpand %ymm6, %ymm5, %ymm5
	vpcmpeqb %ymm2, %ymm4, %ymm6
	vpor %ymm6, %ymm5, %ymm5
	vpcmpeqb %ymm3, %ymm4, %ymm6
	vpor %ymm6, %ymm5, %ymm5
	vpmovmskb %ymm5, %eax
	testl %eax, %eax
	jnz .Lfound
	addq $32, %rsi
	jmp .Lcomment

function SYM(lex_line_end_find_avx2)
	vpbroadcastb .Llf(%rip), %ymm0
	vpbroadcastb .Lcr(%rip), %ymm1
	vpxor %ymm2, %ymm2, %ymm2
.Lline:
	vmovdqu (%rdi,%rsi), %ymm3
	vpcmpeqb %ymm0, %ymm3, %ymm4
	vpcmpeqb %ymm1, %ymm3, %ymm5
	vpor %ymm5, %ymm4, %ymm4
	vpcmpeqb %ymm2, %ymm3, %ymm5
	vpor %ymm5, %ymm4, %ymm4
	vpmovmskb %ymm4, %eax
	testl %eax, %eax
	jnz .Lfound
	addq $32, %rsi
	jmp .Lline

function SYM(lex_quote_find_avx2)
	vmovd %edx, %xmm0
	vpbroadcastb %xmm0, %ymm0
	vpbroadcastb .Lescape(%rip), %ymm1
	vpbroadcastb .Lcr(%rip), %ymm2
.Lquote:
	vmovdqu (%rdi,%rsi), %ymm3
	vpcmpeqb %ymm0, %ymm3, %ymm4
	vpcmpeqb %ymm1, %ymm3, %ymm5
	vpor %ymm5, %ymm4, %ymm4
	vpminub %ymm3, %ymm2, %ymm5
	vpcmpeqb %ymm5, %ymm3, %ymm5
	vpor %ymm5, %ymm4, %ymm4
	vpmovmskb %ymm4, %eax
	testl %eax, %eax
	jnz .Lfound
	addq $32, %rsi
	jmp .Lquote
.Lfound:
	bsfl %eax, %eax
	addq %rsi, %rax
	vzeroupper
	ret

#ifdef __APPLE__
.const
#else
.section .rodata
#endif
.Lnibble: .byte 15
.Lgroup_limit: .byte 64
.Lprefix: .byte 16
.Lpair: .byte 14
.Ldouble: .byte 1
.Lidentifier: .byte 31
.Lwhitespace: .byte 224
.Ldigit: .byte 4
.Lstar: .byte 42
.Lslash: .byte 47
.Lescape: .byte 92
.Llf: .byte 10
.Lcr: .byte 13
#ifndef __APPLE__
.section .note.GNU-stack,"",@progbits
#endif
