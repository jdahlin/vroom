/* as-internal.h: what the files of the assembler module (as/ and arch/<cpu>/as/) share: as_context,
 * its record types, the character-class and hash helpers, and the prototypes of the VPRIV functions
 * one file defines and another calls.  Nothing outside the module includes it (the public interface
 * is include/as/as.h).
 *
 * One as_context belongs to one thread and is never shared; its three regions (expansion text,
 * definitions, current statement) are reset per object by as_begin and everything allocated from them
 * (macro bodies, .set variables, frame descriptions, line rows ...) lives until the next as_begin.
 * All errors go through as_error_report: the first one is kept in a->messages, a->failed stops the statement
 * loop, and the caller sees -1 and as_message_get.  The per-CPU files are told apart by o->machine,
 * not by a second context type. */
#ifndef VCC_AS_INTERNAL_H
#define VCC_AS_INTERNAL_H

#include "as/as.h"
#include "port/job-regions.h"
#include "common/region.h"
#include "arch/a64/a64.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __aarch64__
#include <arm_neon.h>
#endif

/* functions one file defines and another calls: static when the module is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

#define AS_FRAMES_MAX 512
#define AS_CONDITIONS_MAX 1024
#define AS_SECTION_STACK_MAX 256
#define AS_OPERANDS_MAX 8

typedef struct { const char *text; uint32_t length; } as_text;

typedef struct {
	as_text name, default_value;
	uint8_t is_required, is_variadic;
} as_macro_parameter;

/* a body split at its substitutions: body[off, off + len), then k (-1 nothing,
 * -2 \@, -3 \+, >= 0 argument k) */
typedef struct { uint32_t offset, length; int32_t argument_index; } as_template_piece;
typedef struct { as_template_piece *pieces; uint32_t count; } as_template;

typedef struct as_macro {
	as_text name;
	as_macro_parameter *params; uint32_t parameter_count;
	as_text body;
	uint32_t invocation_count;
	as_template template;			/* compiled at the first instantiation (template.pieces == 0 before) */
	struct as_macro *next;
} as_macro;

typedef struct {
	const char *cursor, *end;
	uint8_t kind;		/* AS_FRAME_FILE, AS_FRAME_MACRO, AS_FRAME_LOOP */
	uint8_t at_line_start;		/* at beginning of a line */
	uint32_t condition_count;
	size_t region_mark;
	uint32_t line;
	uint32_t statement_line;		/* line of the current statement */
	uint32_t caller_line;		/* the instantiating statement's line in the frame below */
} as_frame;
enum { AS_FRAME_FILE, AS_FRAME_MACRO, AS_FRAME_LOOP };

typedef struct { uint8_t kind, is_met, is_ignored; } as_condition;	/* kind: 1 if, 2 elseif, 3 else */

typedef struct { uint32_t number, current, forward; } as_numeric_label;

typedef struct { as_text name; as_text register_name; uint32_t older; } as_register_alias;	/* next: older entry in the bucket, index + 1 */
#define AS_ALIAS_BUCKETS 64

typedef ElfVal as_value;

typedef struct { uint32_t symbol; ElfVal value; } as_variable;	/* .set with a non-absolute value */


struct as_context {
	ElfObj *object;
	const char *incbin_directory;	/* relative .incbin paths (NULL: process cwd) */
	as_frame frames[AS_FRAMES_MAX]; int frame_count;
	as_condition conditions[AS_CONDITIONS_MAX]; int condition_count;
	uint32_t fragment_current, fragment_previous;	/* current/previous frag */
	struct { uint32_t cur, prev; } section_stack[AS_SECTION_STACK_MAX]; int section_stack_count;
	vregion expansion_text;		/* expansion text stack */
	vregion definitions;		/* macro definitions etc. (per run) */
	vregion statement_buffer;		/* current statement */
	as_macro **macro_table; uint32_t macro_mask, macro_count;
	as_numeric_label *numeric_labels; uint32_t numeric_label_mask, numeric_label_count;
	as_register_alias *aliases; uint32_t alias_count, alias_capacity;
	uint32_t alias_buckets[AS_ALIAS_BUCKETS];	/* .req buckets: newest entry, index + 1 */
	as_variable *variables; uint32_t variable_mask, variable_count;
	struct PoolS *literal_pools; uint32_t literal_pool_count, literal_pool_capacity;
	uint32_t macro_instance_count;		/* \@ */
	uint64_t statement_count, statement_bytes;	/* statistics: statements and their bytes */
	const char *file_name;
	size_t ident_count;		/* .ident directives seen (.comment's leading NUL) */
	int end_seen;		/* .end seen */
	int altmacro_mode;		/* .altmacro mode */
	uint32_t end_line;	/* the line after the input's last (errors at the end) */
	/* .cfi_*: instructions of all FDEs, the FDEs, the open one */
	struct as_cfi_instruction { uint8_t operation; uint32_t register_first, register_second, label; int64_t offset; const uint8_t *escape_bytes; } *cfi_instructions;
	uint32_t cfi_instruction_count, cfi_instruction_capacity;
	struct as_frame_description { uint32_t start, end, first_instruction, instruction_count, return_register; uint8_t is_simple, is_signal, uses_b_key; } *frame_descriptions;
	uint32_t frame_description_count, frame_description_capacity;
	int in_frame_description, cfi_eh_frame_selected;	/* inside .cfi_startproc; .cfi_sections without .eh_frame: 1 */
	struct { uint32_t orig, ver, rm; } *symvers; uint32_t symver_count, symver_capacity;	/* .symver */
	uint32_t *addrsig_symbols; uint32_t addrsig_symbol_count, addrsig_symbol_capacity;	/* .addrsig_sym */
	/* .file N / .loc: DWARF line table (MC's MCDwarfLineTable, version 5) */
	struct as_dwarf_file { as_text name; uint32_t directory; uint8_t md5[16]; uint8_t is_set, has_md5; } *dwarf_files;
	uint32_t dwarf_file_count, dwarf_file_capacity;		/* file slots by number (0 = root) */
	as_text *dwarf_directories; uint32_t dwarf_directory_count, dwarf_directory_capacity;	/* directories; [0] is the root file's */
	struct as_dwarf_row { uint32_t fragment, file, line, column, discriminator, isa; uint64_t offset; uint8_t flags; } *dwarf_rows, dwarf_row_pending;
	uint32_t dwarf_row_count, dwarf_row_capacity;
	int dwarf_row_is_pending, dwarf_table_is_due;
	/* variable-size items at the end of a split frag (.prefalign, alignment
	   after one, .uleb128/.sleb128 of a label difference), relaxed at the end */
	struct as_variable_item { uint32_t fragment, end_symbol; uint8_t kind, log2_alignment, code, fill; uint64_t max, size; as_value value; } *variable_items;
	uint32_t variable_item_count, variable_item_capacity;		/* a .loc waits for its instruction; a table is due */
	uint32_t *variable_rank, variable_rank_count;	/* section-order ranks during relaxation */
	uint64_t *variable_size_tree, *variable_offsets_previous;	/* live sizes (Fenwick), previous-pass offsets */
	struct { uint32_t sym; char reg[8]; } *register_aliases;	/* x86: name = %reg, the register by another name */
	uint32_t register_alias_count, register_alias_capacity;
	uint32_t coff_symbol_definition;	/* COFF: the symbol of the open .def, 0 none */
	int x64_lock_prefix, x64_operand_size;	/* x86: "lock insn" on one line (after 0x66, as MC); the operation's size (a constant immediate is taken at it) */
	char messages[4096];		/* the errors so far, one per line group (clang goes on after one) */
	uint32_t message_length, error_count;
	int failed;
};


/* ---------------------------------------------------------------- utilities */

static inline void *as_region_reserve(size_t n) { return vreserve(n, MAP_NORESERVE, "vcc: as: mmap"); }
static inline void *as_region_alloc(vregion *r, size_t n)
{
	size_t u = (r->used + 7) & ~(size_t)7;
	if (u + n > r->cap || u + n < u)
		vjob_fail("assembly too large (expansion or statement buffer full)");
	r->used = u + n;
	return r->base + u;
}

/* ASCII classes without libc's locale lookups (hot in the scanner) */
enum { AS_CHAR_DIGIT = 1, AS_CHAR_HEX = 2, AS_CHAR_ALPHA = 4, AS_CHAR_SPACE = 8, AS_CHAR_IDENTIFIER = 16, AS_CHAR_BLANK = 32, AS_CHAR_OPERAND_START = 64 };	/* AS_CHAR_BLANK: blanks ws() skips; AS_CHAR_OPERAND_START: starts an immediate operand */
static const uint8_t as_char_class[256] = {
	['0' ... '9'] = AS_CHAR_DIGIT | AS_CHAR_HEX | AS_CHAR_IDENTIFIER | AS_CHAR_OPERAND_START, ['#'] = AS_CHAR_OPERAND_START, [':'] = AS_CHAR_OPERAND_START, ['('] = AS_CHAR_OPERAND_START, ['-'] = AS_CHAR_OPERAND_START, ['+'] = AS_CHAR_OPERAND_START, ['~'] = AS_CHAR_OPERAND_START, ['a' ... 'f'] = AS_CHAR_ALPHA | AS_CHAR_HEX | AS_CHAR_IDENTIFIER, ['A' ... 'F'] = AS_CHAR_ALPHA | AS_CHAR_HEX | AS_CHAR_IDENTIFIER,
	['g' ... 'z'] = AS_CHAR_ALPHA | AS_CHAR_IDENTIFIER, ['G' ... 'Z'] = AS_CHAR_ALPHA | AS_CHAR_IDENTIFIER, ['_'] = AS_CHAR_IDENTIFIER, ['.'] = AS_CHAR_IDENTIFIER, ['$'] = AS_CHAR_IDENTIFIER,
	[0x80 ... 0xff] = AS_CHAR_IDENTIFIER,	/* UTF-8 in symbol names (LLVM's lexer takes any byte >= 0x80) */
	[' '] = AS_CHAR_SPACE | AS_CHAR_BLANK, ['\t'] = AS_CHAR_SPACE | AS_CHAR_BLANK, ['\n'] = AS_CHAR_SPACE, ['\v'] = AS_CHAR_SPACE | AS_CHAR_BLANK, ['\f'] = AS_CHAR_SPACE | AS_CHAR_BLANK, ['\r'] = AS_CHAR_SPACE | AS_CHAR_BLANK,
};
static inline int as_char_is_digit(int c) { return (unsigned)c - '0' < 10; }
static inline int as_char_is_hex_digit(int c) { return as_char_class[(uint8_t)c] & AS_CHAR_HEX; }
static inline int as_char_to_lower(int c) { return (unsigned)c - 'A' < 26 ? c | 0x20 : c; }
static inline int as_char_is_identifier(int c) { return as_char_class[(uint8_t)c] & AS_CHAR_IDENTIFIER; }

static inline int as_char_is_identifier_start(int c) { return (as_char_class[(uint8_t)c] & (AS_CHAR_IDENTIFIER | AS_CHAR_DIGIT)) == AS_CHAR_IDENTIFIER; }	/* letter _ . $ */
static inline const char *as_whitespace_skip(const char *p, const char *e) { while (p < e && (as_char_class[(uint8_t)*p] & AS_CHAR_BLANK)) p++; return p; }
static inline int as_text_equals(as_text a, const char *s) { size_t n = strlen(s); return a.length == n && !memcmp(a.text, s, n); }
static inline int as_text_equals_ignore_case(const char *p, size_t n, const char *s)
{
	if (!n || as_char_to_lower((unsigned char)p[0]) != s[0]) return 0;
	size_t m = strlen(s);
	if (m != n) return 0;
	for (size_t i = 0; i < n; i++) if (as_char_to_lower((unsigned char)p[i]) != s[i]) return 0;
	return 1;
}

/* word-at-a-time hash */
static inline uint32_t as_text_hash(const char *s, size_t n)
{
	uint64_t h = 0x9e3779b97f4a7c15ull ^ n, w;
	const char *e = s + n;
	size_t n0 = n, r = n & 7;
	while (n >= 8) { memcpy(&w, s, 8); h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32; s += 8; n -= 8; }
	/* the tail (n % 8 bytes, little-endian in the low bits, as a
	 * memcpy into a zeroed word would put them) from whole loads */
	if (r && n0 >= 8) { memcpy(&w, e - 8, 8); w >>= 8 * (8 - r); }
	else if (r >= 4) { uint32_t x, y; memcpy(&x, s, 4); memcpy(&y, e - 4, 4); w = x | (uint64_t)y << (8 * (r - 4)); }
	else if (r) w = (uint64_t)(uint8_t)s[0] | (uint64_t)(uint8_t)s[r / 2] << (8 * (r / 2)) | (uint64_t)(uint8_t)e[-1] << (8 * (r - 1));
	else w = 0;
	h = (h ^ w) * 0xc4ceb9fe1a85ec53ull;
	return (uint32_t)(h ^ h >> 29);
}


/* .req names are case-insensitive; buckets chain entries newest first, so
 * the first match is the newest, as a scan from the end of a->aliases */
static inline uint32_t as_alias_hash(const char *s, size_t n)
{
	if (!n) return 0;
	uint32_t h = (uint32_t)n * 0x9e3779b1u ^ (uint32_t)as_char_to_lower((unsigned char)s[0]) * 0x85ebca6bu ^
		     (uint32_t)as_char_to_lower((unsigned char)s[n - 1]) * 0xc2b2ae35u;
	return (h ^ h >> 15) & (AS_ALIAS_BUCKETS - 1);
}

/* as.c */
VPRIV as_text as_text_duplicate(as_context *a, const char *s, size_t n);
VPRIV uint32_t as_line_current(as_context *a);
VPRIV int as_error_report(as_context *a, const char *fmt, ...);
/* symbol.c */
VPRIV uint32_t as_symbol_temporary(as_context *a);
VPRIV const char *as_register_alias_find(as_context *a, uint32_t y);
/* expression.c */
VPRIV void as_value_fold(as_context *a, as_value *v);
VPRIV int as_expression_absolute_parse(as_context *a, const char **pp, const char *e, int64_t *out);
VPRIV int as_expression_value_parse(as_context *a, const char **pp, const char *e, as_value *v);
/* data-directives.c */
VPRIV int as_value_emit(as_context *a, int size, as_value *v);
VPRIV int as_value_emit_relocation(as_context *a, int size, as_value *v, uint32_t type);	/* with this fixup type */
/* coff-directives.c: a COFF object's directives (1: taken, the result in *rc) */
VPRIV int as_directive_coff(as_context *a, as_text w, const char *p, const char *e, int *rc);

/* COFF (AllowAtInName): '@' is part of a name unless what follows is a relocation specifier (sym@IMGREL) */
static inline int as_at_is_specifier(const char *p, const char *e)
{
	static const char *const names[] = { "plt", "gotpcrel", "gotpcrel_norelax", "tpoff", "gottpoff", "tlsgd", "tlsld", "tlsldm",
		"dtpoff", "ntpoff", "gotntpoff", "indntpoff", "gotoff", "got", "gotpc", "pltoff", "size", "imgrel", "secrel32", "abs8" };
	const char *s = p;
	while (p < e && as_char_is_identifier((unsigned char)*p) && *p != '.' && *p != '$') p++;
	size_t n = (size_t)(p - s);
	if (p < e && (*p == '@' || *p == '.' || *p == '$')) return 0;
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (as_text_equals_ignore_case(s, n, names[i])) return 1;
	return 0;
}
static inline int as_names_have_at(const as_context *a) { return a->object->format == OBJ_COFF; }
/* arch/<cpu>/as/parse.c */
VPRIV void as_alias_rehash(as_context *a);
VPRIV int a64_as_pools_flush(as_context *a);
VPRIV int a64_as_pools_ltorg(as_context *a);
VPRIV int a64_as_instruction_assemble(as_context *a, const char *p, const char *e, size_t wn);
/* dwarf-line-table.c */
VPRIV void as_dwarf_row_emit(as_context *a);
/* arch/x64/as/asm.c (EM_X86_64 objects) */
VPRIV int x64_as_instruction_assemble(as_context *a, const char *p, const char *e, size_t wn);
/* variable-items.c: an x86-64 jump as a relaxed variable item (cc 16: jmp) */
VPRIV int as_variable_item_branch_add(as_context *a, int cc, as_value v, int mod);

/* what the files of as/ define for each other, by the file that defines it */
enum { AS_VARIABLE_ITEM_PREFALIGN, AS_VARIABLE_ITEM_ALIGN, AS_VARIABLE_ITEM_ULEB, AS_VARIABLE_ITEM_SLEB, AS_VARIABLE_ITEM_X64_BRANCH, AS_VARIABLE_ITEM_SKIP };	/* AS_VARIABLE_ITEM_SKIP: .skip v, fill with v settled by the layout */	/* AS_VARIABLE_ITEM_X64_BRANCH: jmp (code 16) / jcc (code cc) to v */
typedef struct { uint8_t *p; size_t n; } as_byte_span;
/* LEB128 into a caller's bytes */
static inline void as_leb128_unsigned_append(as_byte_span *b, uint64_t v) { do { uint8_t c = v & 0x7f; v >>= 7; b->p[b->n++] = (uint8_t)(c | (v ? 0x80 : 0)); } while (v); }
static inline void as_leb128_signed_append(as_byte_span *b, int64_t v)
{
	for (;;) {
		uint8_t c = v & 0x7f; v >>= 7;
		if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) { b->p[b->n++] = c; return; }
		b->p[b->n++] = (uint8_t)(c | 0x80);
	}
}

/* as.c */
VPRIV size_t as_statement_next(as_context *a, as_frame *f, const char **raw, const char **s);
VPRIV void as_frame_pop(as_context *a);
VPRIV int as_frame_push(as_context *a, const char *p, size_t n, int kind, size_t mark);
/* call-frame-information.c */
VPRIV void as_cfi_frame_emit(as_context *a);
VPRIV int as_directive_cfi(as_context *a, as_text w, const char *p, const char *e);
/* conditional.c */
VPRIV int as_directive_conditional(as_context *a, as_text w, const char *p, const char *e, int *rc);
VPRIV int as_condition_is_ignored(as_context *a);
VPRIV as_text as_text_trim(const char *p, const char *e);
/* data-directives.c */
VPRIV int as_directive_align(as_context *a, const char *p, const char *e, int p2);
VPRIV int as_directive_align_width(as_context *a, const char *p, const char *e, int p2, int fsz);
VPRIV int as_directive_ascii(as_context *a, const char *p, const char *e, int z);
VPRIV int as_directive_base64(as_context *a, const char *p, const char *e);
VPRIV int as_directive_comm(as_context *a, const char *p, const char *e, int local);
VPRIV int as_directive_data(as_context *a, const char *p, const char *e, int size);
VPRIV int as_directive_fill(as_context *a, const char *p, const char *e);
VPRIV int as_directive_float(as_context *a, const char *p, const char *e, int size);
VPRIV int as_directive_incbin(as_context *a, const char *p, const char *e);
VPRIV int as_directive_inst(as_context *a, const char *p, const char *e);
VPRIV int as_directive_leb128(as_context *a, const char *p, const char *e, int sgn);
VPRIV int as_directive_octa(as_context *a, const char *p, const char *e);
VPRIV int as_directive_org(as_context *a, const char *p, const char *e);
VPRIV int as_directive_reloc(as_context *a, const char *p, const char *e);
VPRIV int as_directive_space(as_context *a, const char *p, const char *e);
VPRIV int as_string_parse(as_context *a, const char **pp, const char *e, char *out, size_t *n);
/* dwarf-line-table.c */
VPRIV int as_directive_file(as_context *a, const char *p, const char *e);
VPRIV int as_directive_loc(as_context *a, const char *p, const char *e);
VPRIV void as_dwarf_line_finish(as_context *a);
/* expression.c */
VPRIV int as_value_is_deferrable(as_context *a, const as_value *v);
VPRIV int as_value_is_absolute(as_context *a, as_value *v);
/* macro.c */
VPRIV int as_directive_exitm(as_context *a);
VPRIV int as_directive_irp(as_context *a, const char *p, const char *e, int chars);
VPRIV int as_directive_macro(as_context *a, const char *p, const char *e);
VPRIV int as_directive_purgem(as_context *a, const char *p, const char *e);
VPRIV int as_directive_rept(as_context *a, const char *p, const char *e);
VPRIV as_macro *as_macro_find(as_context *a, const char *s, size_t n);
VPRIV as_text as_text_first_word(const char *p, const char *e);
VPRIV int as_macro_invoke(as_context *a, as_macro *m, const char *p, const char *e);
/* variable-items.c */
VPRIV int as_directive_prefalign(as_context *a, const char *p, const char *e);
VPRIV void as_fragment_offsets_cache(ElfObj *o);
VPRIV void as_variable_items_finish(as_context *a);
VPRIV struct as_variable_item *as_variable_item_add(as_context *a, int kind);
/* section.c */
VPRIV int as_directive_section(as_context *a, const char *p, const char *e, int push);
VPRIV int as_name_parse(as_context *a, const char **pp, const char *e, as_text *out);
VPRIV uint32_t as_section_standard_find(as_context *a, const char *name);
VPRIV void as_fragment_switch(as_context *a, uint32_t frag);
/* symbol.c */
VPRIV int as_directive_size(as_context *a, const char *p, const char *e);
VPRIV int as_directive_symbol_attribute(as_context *a, const char *p, const char *e, int what);
VPRIV int as_directive_symver(as_context *a, const char *p, const char *e);
VPRIV int as_directive_type(as_context *a, const char *p, const char *e);
VPRIV uint32_t as_label_temporary_define(as_context *a);
VPRIV as_numeric_label *as_numeric_label_find(as_context *a, uint32_t num);
VPRIV uint32_t as_symbol_intern(as_context *a, const char *s, size_t n);
VPRIV void as_symver_finish(as_context *a);
VPRIV as_variable *as_variable_slot_find(as_context *a, uint32_t y, int create);
/* section.c */
VPRIV uint32_t as_fragment_latest(as_context *a, uint32_t frag);

#endif
