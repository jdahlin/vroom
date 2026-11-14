/*
 * The text form, parsing (SPEC-vir.md 11.3-11.5).  Safe on arbitrary bytes:
 * every error is reported as file:line:col by the caller and never crashes.
 *
 * The whole text is lexed first.  A function is read into parser-local
 * records (instructions in text order, operands as `pop`s); at its closing
 * brace the names are resolved, the constants created with the type their
 * position requires (11.5), and the function assembled in the constants-
 * first layout of 2.3.  Nothing is repaired: what the text says is what the
 * verifier sees (a block without a terminator is S3, not a syntax error).
 */
#include <errno.h>
#include <setjmp.h>

#include "vir-internal.h"

/* ---------------- growable vectors and name maps ---------------- */

#define PV(T) struct { T *v; uint32_t n, cap; }
#define VIR_PARSE_VECTOR_PUSH(a, x) do { \
	if ((a).n == (a).cap) { \
		(a).cap = (a).cap ? (a).cap * 2 : 64; \
		(a).v = realloc((a).v, (size_t)(a).cap * sizeof(*(a).v)); \
		if (!(a).v) abort(); \
	} \
	(a).v[(a).n++] = (x); \
} while (0)
#define VIR_PARSE_VECTOR_FREE(a) do { free((a).v); (a).v = NULL; (a).n = (a).cap = 0; } while (0)

typedef struct vir_parse_name_entry {
	const char *string;
	uint32_t name_length, hash;
	uint32_t state;		/* 0 not defined, 1 defined */
	uint32_t id;		/* what it names */
	uint32_t line, column;	/* first mention */
} vir_parse_name_entry;
typedef struct vir_parse_names {
	uint32_t *table, capacity;
	PV(vir_parse_name_entry) entries;
} vir_parse_names;

static uint32_t vir_parse_name_hash(const char *s, uint32_t n)
{
	uint32_t h = 2166136261u;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ (unsigned char)s[i]) * 16777619u;
	return h;
}

/* the entry for a name; *isnew when it was created (id and st 0) */
static uint32_t vir_parse_name_get(vir_parse_names *m, const char *s, uint32_t n, bool *isnew)
{
	uint32_t h = vir_parse_name_hash(s, n);

	if (m->entries.n * 2 >= m->capacity) {
		uint32_t nc = m->capacity ? m->capacity * 2 : 64;

		free(m->table);
		m->table = calloc(nc, 4);
		if (!m->table)
			abort();
		m->capacity = nc;
		for (uint32_t i = 0; i < m->entries.n; i++) {
			uint32_t j = m->entries.v[i].hash & (nc - 1);

			while (m->table[j])
				j = (j + 1) & (nc - 1);
			m->table[j] = i + 1;
		}
	}
	for (uint32_t j = h & (m->capacity - 1); m->table[j]; j = (j + 1) & (m->capacity - 1)) {
		vir_parse_name_entry *e = &m->entries.v[m->table[j] - 1];

		if (e->hash == h && e->name_length == n && !memcmp(e->string, s, n)) {
			*isnew = false;
			return m->table[j] - 1;
		}
	}
	{
		vir_parse_name_entry e = { s, n, h, 0, 0, 0, 0 };
		uint32_t j = h & (m->capacity - 1);

		while (m->table[j])
			j = (j + 1) & (m->capacity - 1);
		VIR_PARSE_VECTOR_PUSH(m->entries, e);
		m->table[j] = m->entries.n;
		*isnew = true;
		return m->entries.n - 1;
	}
}

static void vir_parse_names_reset(vir_parse_names *m)
{
	m->entries.n = 0;
	if (m->table)
		memset(m->table, 0, (size_t)m->capacity * 4);
}

static void vir_parse_names_free(vir_parse_names *m)
{
	free(m->table);
	VIR_PARSE_VECTOR_FREE(m->entries);
	m->table = NULL;
	m->capacity = 0;
}

/* ---------------- tokens ---------------- */

enum { VIR_PARSE_TOKEN_EOF, VIR_PARSE_TOKEN_NL, VIR_PARSE_TOKEN_ID, VIR_PARSE_TOKEN_INT, VIR_PARSE_TOKEN_FLT, VIR_PARSE_TOKEN_STR, VIR_PARSE_TOKEN_SYM, VIR_PARSE_TOKEN_SLOT, VIR_PARSE_TOKEN_ASM, VIR_PARSE_TOKEN_SIG, VIR_PARSE_TOKEN_PUNCT, VIR_PARSE_TOKEN_ARROW, VIR_PARSE_TOKEN_DOTS, VIR_PARSE_TOKEN_DOTDOT };

typedef struct vir_parse_token {
	uint8_t kind, character;
	uint32_t line, column, len;
	const char *string;
	uint64_t value;		/* INT: the value; STR: offset in the string buffer */
} vir_parse_token;

/* a pop: an operand as written; resolved when the function is complete */
enum { VIR_PARSE_OPERAND_KIND_NAME, VIR_PARSE_OPERAND_KIND_INT, VIR_PARSE_OPERAND_KIND_FLT, VIR_PARSE_OPERAND_KIND_BITS, VIR_PARSE_OPERAND_KIND_SYM, VIR_PARSE_OPERAND_KIND_SLOT, VIR_PARSE_OPERAND_KIND_LABEL };
typedef struct vir_parse_operand_record {
	uint8_t kind, type, is_negative;	/* ty: the suffix, or 0xff */
	uint32_t line, column;
	uint32_t id;		/* name entry, unit symbol, slot, block name entry */
	uint64_t value;		/* integer (two's complement), float bits (as a double or the bits) */
	int64_t addend;		/* addend of a symbol or slot */
} vir_parse_operand_record;

enum { VIR_PARSE_FIXUP_VAL, VIR_PARSE_FIXUP_BLK };
enum { VIR_PARSE_LIST_MODE_POS, VIR_PARSE_LIST_MODE_LIST, VIR_PARSE_LIST_MODE_BR };	/* how the wanted type of a value is known */
typedef struct vir_parse_fixup {
	uint8_t kind, mode, field;	/* fld: 0..2 an instruction field, 3 an extra word */
	uint32_t kind_code;		/* the instruction (index into B) */
	uint32_t position;		/* the extra word */
	uint32_t operand;		/* VIR_PARSE_FIXUP_VAL: the pop; VIR_PARSE_FIXUP_BLK: the block name entry */
	uint32_t auxiliary;		/* VIR_PARSE_LIST_MODE_BR: the target's name entry (the argument index is `at - the list start` via cnt) */
	uint32_t index;		/* VIR_PARSE_LIST_MODE_BR: the argument index */
	uint32_t line, col;
} vir_parse_fixup;

typedef struct vir_parse_switch_case { uint64_t low, high; uint32_t block, line, column; } vir_parse_switch_case;
typedef struct vir_parse_position { uint32_t line, column, count, column_start, column_end; } vir_parse_position;	/* per instruction; cnt: br args, switch cases [c0, c1) */
typedef struct vir_parse_block { uint32_t first, insn_count, parameter_count, flags, name; } vir_parse_block;
typedef struct vir_parse_constant_definition { vir_insn insn; uint32_t operand, name; } vir_parse_constant_definition;	/* a constant written as an instruction line */

typedef struct vir_parse_state {
	const char *source;
	vir_unit *unit;
	vir_parse_error *error_record;
	jmp_buf join_block;
	PV(vir_parse_token) tokens;
	char *string_buffer;		/* decoded strings */
	size_t block_name_count, string_capacity;
	uint32_t position;		/* the token index */
	/* unit level */
	vir_parse_names signature_name, asm_name;
	/* per function */
	vir_function function;
	bool in_function;
	vir_parse_names values, block_records, slot_names;
	PV(vir_insn) blocks;
	PV(vir_parse_position) block_parameter_count;
	PV(vir_parse_block) parsed_block;
	PV(vir_parse_operand_record) operands;
	PV(vir_parse_fixup) fixups;
	PV(vir_parse_switch_case) switch_cases;
	PV(vir_parse_constant_definition) constant_definitions;
	PV(uint32_t) signature_offset;	/* per unit signature: its offset in the function's extra, or ~0 */
	PV(uint32_t) scratch;
	uint32_t function_symbol;
	uint32_t function_signature_offset;
} vir_parse_state;

static void vir_parse_error_at(vir_parse_state *p, uint32_t line, uint32_t col, const char *fmt, ...) __attribute__((noreturn, format(printf, 4, 5)));
static void vir_parse_error_at(vir_parse_state *p, uint32_t line, uint32_t col, const char *fmt, ...)
{
	va_list ap;

	p->error_record->line = line;
	p->error_record->col = col;
	va_start(ap, fmt);
	vsnprintf(p->error_record->message, sizeof(p->error_record->message), fmt, ap);
	va_end(ap);
	longjmp(p->join_block, 1);
}

#define VIR_PARSE_ERROR(p, t, ...) vir_parse_error_at((p), (t)->line, (t)->column, __VA_ARGS__)

/* ---------------- lexer ---------------- */

static inline bool vir_parse_character_is_identifier_start(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static inline bool vir_parse_character_is_identifier(int c) { return vir_parse_character_is_identifier_start(c) || (c >= '0' && c <= '9') || c == '.'; }
static inline bool vir_parse_character_is_symbol(int c) { return vir_parse_character_is_identifier(c) || c == '$'; }
static inline int vir_parse_hex_digit_value(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void vir_parse_string_append(vir_parse_state *p, int c)
{
	if (p->block_name_count == p->string_capacity) {
		p->string_capacity = p->string_capacity ? p->string_capacity * 2 : 256;
		p->string_buffer = realloc(p->string_buffer, p->string_capacity);
		if (!p->string_buffer)
			abort();
	}
	p->string_buffer[p->block_name_count++] = (char)c;
}

static void vir_parse_tokens_lex(vir_parse_state *p, const char *src, size_t n)
{
	const char *c = src, *end = src + n, *ls = src;
	uint32_t line = 1;
	bool nl = true;

	while (c < end) {
		vir_parse_token t = { 0 };

		if (*c == ' ' || *c == '\t' || *c == '\r') {
			c++;
			continue;
		}
		if (*c == ';') {
			while (c < end && *c != '\n')
				c++;
			continue;
		}
		t.line = line;
		t.column = (uint32_t)(c - ls) + 1;
		t.string = c;
		if (*c == '\n') {
			c++;
			line++;
			ls = c;
			if (!nl) {
				t.kind = VIR_PARSE_TOKEN_NL;
				t.len = 1;
				VIR_PARSE_VECTOR_PUSH(p->tokens, t);
				nl = true;
			}
			continue;
		}
		nl = false;
		if (vir_parse_character_is_identifier_start(*c)) {
			while (c < end && vir_parse_character_is_identifier(*c))
				c++;
			t.kind = VIR_PARSE_TOKEN_ID;
		} else if (*c >= '0' && *c <= '9') {
			uint64_t v = 0;
			bool ovf = false, flt = false;

			if (*c == '0' && c + 1 < end && (c[1] == 'x' || c[1] == 'X') && c + 2 < end && vir_parse_hex_digit_value(c[2]) >= 0) {
				const char *q = c + 2;

				while (q < end && vir_parse_hex_digit_value(*q) >= 0)
					q++;
				if (q < end && *q == '.' && q + 1 < end) {	/* a hexadecimal float needs its p exponent */
					const char *r = q + 1;

					while (r < end && vir_parse_hex_digit_value(*r) >= 0)
						r++;
					if (r < end && (*r == 'p' || *r == 'P'))
						flt = true, q = r;
				}
				if (!flt && q < end && (*q == 'p' || *q == 'P'))
					flt = true;
				if (flt) {	/* p, an optional sign, digits */
					q++;
					if (q < end && (*q == '+' || *q == '-'))
						q++;
					if (q >= end || *q < '0' || *q > '9') {
						p->error_record->line = t.line, p->error_record->col = t.column;
						snprintf(p->error_record->message, sizeof(p->error_record->message), "malformed hexadecimal float");
						longjmp(p->join_block, 1);
					}
					while (q < end && *q >= '0' && *q <= '9')
						q++;
					c = q;
					t.kind = VIR_PARSE_TOKEN_FLT;
				} else {
					for (c += 2; c < end && vir_parse_hex_digit_value(*c) >= 0; c++) {
						if (v >> 60)
							ovf = true;
						v = v << 4 | (uint64_t)vir_parse_hex_digit_value(*c);
					}
					t.kind = VIR_PARSE_TOKEN_INT;
				}
			} else {
				for (; c < end && *c >= '0' && *c <= '9'; c++) {
					unsigned d = (unsigned)(*c - '0');

					if (v > (UINT64_MAX - d) / 10)
						ovf = true;
					v = v * 10 + d;
				}
				t.kind = VIR_PARSE_TOKEN_INT;
			}
			if (ovf || (c < end && (vir_parse_character_is_identifier_start(*c) || (*c >= '0' && *c <= '9')))) {
				p->error_record->line = t.line, p->error_record->col = t.column;
				snprintf(p->error_record->message, sizeof(p->error_record->message), ovf ? "integer constant too large" : "malformed number");
				longjmp(p->join_block, 1);
			}
			t.value = v;
		} else if (*c == '"') {
			t.kind = VIR_PARSE_TOKEN_STR;
			t.value = p->block_name_count;
			c++;
			for (;;) {
				int ch;

				if (c >= end || *c == '\n') {
					p->error_record->line = t.line, p->error_record->col = t.column;
					snprintf(p->error_record->message, sizeof(p->error_record->message), "unterminated string");
					longjmp(p->join_block, 1);
				}
				if (*c == '"') {
					c++;
					break;
				}
				if (*c != '\\') {
					vir_parse_string_append(p, (unsigned char)*c++);
					continue;
				}
				c++;
				if (c >= end)
					continue;
				ch = *c++;
				switch (ch) {
				case 'n': vir_parse_string_append(p, '\n'); break;
				case 't': vir_parse_string_append(p, '\t'); break;
				case 'r': vir_parse_string_append(p, '\r'); break;
				case 'a': vir_parse_string_append(p, 7); break;
				case 'b': vir_parse_string_append(p, 8); break;
				case 'f': vir_parse_string_append(p, 12); break;
				case 'v': vir_parse_string_append(p, 11); break;
				case '\\': case '"': case '\'': case '?': vir_parse_string_append(p, ch); break;
				case 'x': {
					int v = 0, nd = 0;

					while (c < end && vir_parse_hex_digit_value(*c) >= 0) {
						v = (v << 4 | vir_parse_hex_digit_value(*c)) & 255;
						c++;
						nd++;
					}
					if (!nd) {
						p->error_record->line = line, p->error_record->col = (uint32_t)(c - ls) + 1;
						snprintf(p->error_record->message, sizeof(p->error_record->message), "\\x without digits");
						longjmp(p->join_block, 1);
					}
					vir_parse_string_append(p, v);
					break;
				}
				default:
					if (ch >= '0' && ch <= '7') {
						int v = ch - '0';

						for (int i = 0; i < 2 && c < end && *c >= '0' && *c <= '7'; i++)
							v = v * 8 + (*c++ - '0');
						vir_parse_string_append(p, v & 255);
					} else {
						p->error_record->line = line, p->error_record->col = (uint32_t)(c - ls);
						snprintf(p->error_record->message, sizeof(p->error_record->message), "unknown escape sequence");
						longjmp(p->join_block, 1);
					}
				}
			}
			t.len = (uint32_t)(p->block_name_count - t.value);
			VIR_PARSE_VECTOR_PUSH(p->tokens, t);
			continue;
		} else if (*c == '@' || *c == '$' || *c == '!' || *c == '%') {
			t.kind = *c == '@' ? VIR_PARSE_TOKEN_SYM : *c == '$' ? VIR_PARSE_TOKEN_SLOT : *c == '!' ? VIR_PARSE_TOKEN_ASM : VIR_PARSE_TOKEN_SIG;
			c++;
			t.string = c;
			while (c < end && (t.kind == VIR_PARSE_TOKEN_SYM ? vir_parse_character_is_symbol(*c) : vir_parse_character_is_identifier(*c)))
				c++;
			if (c == t.string) {
				p->error_record->line = t.line, p->error_record->col = t.column;
				snprintf(p->error_record->message, sizeof(p->error_record->message), "a name is expected after `%c`", t.string[-1]);
				longjmp(p->join_block, 1);
			}
			t.len = (uint32_t)(c - t.string);
			VIR_PARSE_VECTOR_PUSH(p->tokens, t);
			continue;
		} else if (c + 2 < end + 0 && c[0] == '.' && c[1] == '.' && c[2] == '.') {
			t.kind = VIR_PARSE_TOKEN_DOTS;
			c += 3;
		} else if (c + 1 < end && c[0] == '.' && c[1] == '.') {
			t.kind = VIR_PARSE_TOKEN_DOTDOT;
			c += 2;
		} else if (c + 1 < end && c[0] == '-' && c[1] == '>') {
			t.kind = VIR_PARSE_TOKEN_ARROW;
			c += 2;
		} else if (strchr(",:=(){}[]&+-.", *c) && *c) {
			t.kind = VIR_PARSE_TOKEN_PUNCT;
			t.character = (uint8_t)*c++;
		} else {
			p->error_record->line = t.line;
			p->error_record->col = t.column;
			snprintf(p->error_record->message, sizeof(p->error_record->message), "unexpected character 0x%02x", (unsigned char)*c);
			longjmp(p->join_block, 1);
		}
		t.len = (uint32_t)(c - t.string);
		VIR_PARSE_VECTOR_PUSH(p->tokens, t);
	}
	{
		vir_parse_token t = { 0 };

		if (!nl) {
			t.kind = VIR_PARSE_TOKEN_NL;
			t.line = line;
			t.column = (uint32_t)(c - ls) + 1;
			VIR_PARSE_VECTOR_PUSH(p->tokens, t);
		}
		t.kind = VIR_PARSE_TOKEN_EOF;
		t.line = line;
		t.column = (uint32_t)(c - ls) + 1;
		VIR_PARSE_VECTOR_PUSH(p->tokens, t);
	}
}

/* ---------------- token helpers ---------------- */

static vir_parse_token *vir_parse_token_current(vir_parse_state *p) { return &p->tokens.v[p->position]; }
static vir_parse_token *vir_parse_token_peek(vir_parse_state *p, uint32_t k) { return &p->tokens.v[p->position + k < p->tokens.n ? p->position + k : p->tokens.n - 1]; }
static vir_parse_token *vir_parse_token_next(vir_parse_state *p)
{
	vir_parse_token *t = vir_parse_token_current(p);

	if (t->kind != VIR_PARSE_TOKEN_EOF)
		p->position++;
	return t;
}
static bool vir_parse_token_is_punctuation(const vir_parse_token *t, char ch) { return t->kind == VIR_PARSE_TOKEN_PUNCT && t->character == ch; }
static bool vir_parse_token_is_identifier(const vir_parse_token *t, const char *s) { return t->kind == VIR_PARSE_TOKEN_ID && strlen(s) == t->len && !memcmp(t->string, s, t->len); }
static bool vir_parse_punctuation_accept(vir_parse_state *p, char ch)
{
	if (vir_parse_token_is_punctuation(vir_parse_token_current(p), ch)) {
		p->position++;
		return true;
	}
	return false;
}
static void vir_parse_punctuation_expect(vir_parse_state *p, char ch)
{
	if (!vir_parse_punctuation_accept(p, ch))
		VIR_PARSE_ERROR(p, vir_parse_token_current(p), "expected `%c`", ch);
}
static vir_parse_token *vir_parse_token_kind_expect(vir_parse_state *p, int k, const char *what)
{
	if (vir_parse_token_current(p)->kind != k)
		VIR_PARSE_ERROR(p, vir_parse_token_current(p), "expected %s", what);
	return vir_parse_token_next(p);
}
static void vir_parse_keyword_expect(vir_parse_state *p, const char *kw)
{
	if (!vir_parse_token_is_identifier(vir_parse_token_current(p), kw))
		VIR_PARSE_ERROR(p, vir_parse_token_current(p), "expected `%s`", kw);
	p->position++;
}
static void vir_parse_newline_expect(vir_parse_state *p)
{
	if (vir_parse_token_current(p)->kind != VIR_PARSE_TOKEN_NL && vir_parse_token_current(p)->kind != VIR_PARSE_TOKEN_EOF)
		VIR_PARSE_ERROR(p, vir_parse_token_current(p), "unexpected text at the end of the line");
	if (vir_parse_token_current(p)->kind == VIR_PARSE_TOKEN_NL)
		p->position++;
}

static uint64_t vir_parse_integer_expect(vir_parse_state *p, bool allow_neg, bool *neg)
{
	bool ng = false;
	vir_parse_token *t;

	if (allow_neg && vir_parse_punctuation_accept(p, '-'))
		ng = true;
	t = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_INT, "an integer");
	if (neg)
		*neg = ng;
	if (ng && t->value > (uint64_t)INT64_MAX + 1)
		VIR_PARSE_ERROR(p, t, "integer constant too large");
	return ng ? -t->value : t->value;
}

static uint32_t vir_parse_unsigned_expect(vir_parse_state *p, const char *what, uint32_t max)
{
	vir_parse_token *t = vir_parse_token_current(p);
	uint64_t v = vir_parse_integer_expect(p, false, NULL);

	if (v > max)
		VIR_PARSE_ERROR(p, t, "%s out of range", what);
	return (uint32_t)v;
}

static uint32_t vir_parse_token_type(const vir_parse_token *t)
{
	for (uint32_t i = 1; i < VIR_TYPE_COUNT; i++)
		if (t->kind == VIR_PARSE_TOKEN_ID && strlen(vir_type_names[i]) == t->len && !memcmp(vir_type_names[i], t->string, t->len))
			return i;
	return 0;
}

static uint32_t vir_parse_type_expect(vir_parse_state *p)
{
	vir_parse_token *t = vir_parse_token_current(p);
	uint32_t ty = vir_parse_token_type(t);

	if (!ty)
		VIR_PARSE_ERROR(p, t, "expected a type (i32, i64, f32, f64, ptr, multi)");
	p->position++;
	return ty;
}

static const char *vir_parse_token_string(vir_parse_state *p, const vir_parse_token *t, size_t *n)
{
	*n = t->len;
	return vir_unit_string_copy(p->unit, p->string_buffer ? p->string_buffer + t->value : "", t->len);
}

static uint32_t vir_parse_alignment_log2(vir_parse_state *p, const vir_parse_token *t, uint64_t a, uint32_t maxlog)
{
	uint32_t l = 0;

	if (!a || (a & (a - 1)))
		VIR_PARSE_ERROR(p, t, "an alignment must be a power of two");
	while (((uint64_t)1 << l) < a)
		l++;
	if (l > maxlog)
		VIR_PARSE_ERROR(p, t, "alignment too large");
	return l;
}

/* ---------------- signatures, groups ---------------- */

static uint32_t vir_parse_group(vir_parse_state *p, uint32_t *w1)
{
	vir_parse_token *t = vir_parse_token_current(p);

	*w1 = 0;
	if (vir_parse_token_is_identifier(t, "none")) {
		p->position++;
		return VIR_GROUP_WORD0(VIR_GROUP_NONE, 0, 0, 0, 0, 0, 0);
	}
	if (vir_parse_token_is_identifier(t, "sret")) {
		p->position++;
		return VIR_GROUP_WORD0(VIR_GROUP_SRET, 0, 0, 0, 0, 0, 0);
	}
	if (vir_parse_token_is_identifier(t, "mem")) {
		uint64_t sz, al;
		vir_parse_token *at;

		p->position++;
		at = vir_parse_token_current(p);
		sz = vir_parse_integer_expect(p, false, NULL);
		if (sz >= ((uint64_t)1 << 28))
			VIR_PARSE_ERROR(p, at, "a memory group's size must be below 2^28");
		vir_parse_keyword_expect(p, "align");
		at = vir_parse_token_current(p);
		al = vir_parse_integer_expect(p, false, NULL);
		*w1 = VIR_GROUP_WORD1(sz, vir_parse_alignment_log2(p, at, al, 15));
		return VIR_GROUP_WORD0(VIR_GROUP_MEM, 0, 0, 0, 0, 0, 0);
	}
	{
		uint32_t cl[4] = { 0, 0, 0, 0 }, n = 0;

		for (;;) {
			vir_parse_token *pt = vir_parse_token_current(p);
			uint32_t c = vir_parse_token_is_identifier(pt, "int") ? VIR_CLASS_INT : vir_parse_token_is_identifier(pt, "f32") ? VIR_CLASS_F32 : vir_parse_token_is_identifier(pt, "f64") ? VIR_CLASS_F64 : 0;

			if (!c)
				VIR_PARSE_ERROR(p, pt, "expected a group (none, int, f32, f64, mem, sret)");
			if (n == 4)
				VIR_PARSE_ERROR(p, pt, "at most 4 pieces in a group");
			cl[n++] = c;
			p->position++;
			if (!vir_parse_punctuation_accept(p, ':'))
				break;
		}
		return VIR_GROUP_WORD0(VIR_GROUP_REG, n, cl[0], cl[1], cl[2], cl[3], 0);
	}
}

static void vir_parse_signature_declaration(vir_parse_state *p)
{
	vir_parse_token *nt;
	bool isnew;
	uint32_t e, ng = 0, var = 0, w1, res0, res1;
	PV(uint32_t) w = { 0 };

	vir_parse_token_next(p);
	nt = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_SIG, "a signature name (%name)");
	e = vir_parse_name_get(&p->signature_name, nt->string, nt->len, &isnew);
	if (!isnew)
		VIR_PARSE_ERROR(p, nt, "signature %%%.*s declared twice", (int)nt->len, nt->string);
	vir_parse_punctuation_expect(p, '=');
	vir_parse_punctuation_expect(p, '(');
	if (!vir_parse_token_is_punctuation(vir_parse_token_current(p), ')')) {
		for (;;) {
			uint32_t w0;

			if (vir_parse_token_current(p)->kind == VIR_PARSE_TOKEN_DOTS) {
				p->position++;
				var = 1;
				break;
			}
			w0 = vir_parse_group(p, &w1);
			if (ng == 0xffff)
				VIR_PARSE_ERROR(p, vir_parse_token_current(p), "too many groups");
			VIR_PARSE_VECTOR_PUSH(w, w0);
			VIR_PARSE_VECTOR_PUSH(w, w1);
			ng++;
			if (!vir_parse_punctuation_accept(p, ','))
				break;
		}
	}
	vir_parse_punctuation_expect(p, ')');
	vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_ARROW, "`->`");
	res0 = vir_parse_group(p, &res1);
	vir_parse_newline_expect(p);
	{
		uint32_t *all = malloc((size_t)(3 + w.n) * 4);

		if (!all)
			abort();
		all[0] = VIR_SIGNATURE_HEADER(ng, var);
		all[1] = res0;
		all[2] = res1;
		if (w.n)
			memcpy(&all[3], w.v, (size_t)w.n * 4);
		p->signature_name.entries.v[e].state = 1;
		p->signature_name.entries.v[e].id = vir_unit_signature_words_intern(p->unit, all, 3 + w.n);
		free(all);
	}
	VIR_PARSE_VECTOR_FREE(w);
}

/* ---------------- unit level ---------------- */

static uint32_t vir_parse_symbol_reference(vir_parse_state *p, const vir_parse_token *t)
{
	uint32_t i = vir_unit_symbol_intern(p->unit, t->string, t->len);

	if (!p->unit->symbols[i].line) {
		p->unit->symbols[i].line = t->line;
		p->unit->symbols[i].column = t->column;
	}
	return i;
}

static void vir_parse_asm_declaration(vir_parse_state *p)
{
	vir_parse_token *nt, *st;
	bool isnew;
	uint32_t e;
	vir_unit_asm a;
	const char *kw[3] = { "outs", "ins", "clobber" };
	uint32_t *cnt[3] = { &a.output_count, &a.input_count, &a.clobber_count };
	const char ***lst[3] = { &a.outputs, &a.inputs, &a.clobbers };

	memset(&a, 0, sizeof(a));
	vir_parse_token_next(p);
	nt = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_ASM, "an asm template name (!name)");
	e = vir_parse_name_get(&p->asm_name, nt->string, nt->len, &isnew);
	if (!isnew)
		VIR_PARSE_ERROR(p, nt, "asm template !%.*s declared twice", (int)nt->len, nt->string);
	st = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_STR, "the template string");
	{
		size_t n;

		a.template = vir_parse_token_string(p, st, &n);
	}
	for (int k = 0; k < 3; k++) {
		PV(const char *) l = { 0 };

		vir_parse_keyword_expect(p, kw[k]);
		vir_parse_punctuation_expect(p, '(');
		if (!vir_parse_token_is_punctuation(vir_parse_token_current(p), ')'))
			for (;;) {
				vir_parse_token *s = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_STR, "a string");
				size_t n;
				const char *c = vir_parse_token_string(p, s, &n);

				VIR_PARSE_VECTOR_PUSH(l, c);
				if (!vir_parse_punctuation_accept(p, ','))
					break;
			}
		vir_parse_punctuation_expect(p, ')');
		*cnt[k] = l.n;
		*lst[k] = vir_unit_alloc(p->unit, (size_t)l.n * sizeof(char *));
		if (l.n)
			memcpy((void *)*lst[k], l.v, (size_t)l.n * sizeof(char *));
		VIR_PARSE_VECTOR_FREE(l);
	}
	if (vir_parse_token_is_identifier(vir_parse_token_current(p), "labels")) {
		p->position++;
		a.labels = vir_parse_unsigned_expect(p, "the label count", 65535);
	}
	vir_parse_newline_expect(p);
	if (p->unit->asm_count == p->unit->asm_capacity) {
		p->unit->asm_capacity = p->unit->asm_capacity ? p->unit->asm_capacity * 2 : 8;
		p->unit->asms = realloc(p->unit->asms, (size_t)p->unit->asm_capacity * sizeof(vir_unit_asm));
		if (!p->unit->asms)
			abort();
	}
	p->unit->asms[p->unit->asm_count] = a;
	p->asm_name.entries.v[e].state = 1;
	p->asm_name.entries.v[e].id = p->unit->asm_count++;
}

static void vir_parse_symbol_define(vir_parse_state *p, vir_parse_token *t, uint32_t kind)
{
	uint32_t s = vir_parse_symbol_reference(p, t);
	vir_unit_symbol *y = &p->unit->symbols[s];

	if (y->kind != VIR_UNIT_ITEM_UNDECL)
		VIR_PARSE_ERROR(p, t, "symbol @%.*s defined twice", (int)t->len, t->string);
	y->kind = (uint8_t)kind;
}

static void vir_parse_data_declaration(vir_parse_state *p)
{
	bool tls = vir_parse_token_is_identifier(vir_parse_token_current(p), "tdata");
	vir_parse_token *st, *at;
	uint32_t s, al;
	vir_unit *u = p->unit;

	vir_parse_token_next(p);
	st = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_SYM, "a symbol (@name)");
	vir_parse_symbol_define(p, st, tls ? VIR_UNIT_ITEM_TDATA : VIR_UNIT_ITEM_DATA);
	s = vir_parse_symbol_reference(p, st);
	vir_parse_keyword_expect(p, "align");
	at = vir_parse_token_current(p);
	al = vir_parse_unsigned_expect(p, "the alignment", 1u << 15);
	if (!al || (al & (al - 1)))
		VIR_PARSE_ERROR(p, at, "an alignment must be a power of two");
	u->symbols[s].align = al;
	vir_parse_punctuation_expect(p, '=');
	vir_parse_punctuation_expect(p, '{');
	u->symbols[s].item_first = u->item_count;
	if (!vir_parse_token_is_punctuation(vir_parse_token_current(p), '}'))
		for (;;) {
			vir_item it;
			vir_parse_token *t = vir_parse_token_current(p);

			memset(&it, 0, sizeof(it));
			if (vir_parse_token_is_identifier(t, "zero")) {
				p->position++;
				it.type = 0xff;
				it.value = vir_parse_integer_expect(p, false, NULL);
			} else if (vir_parse_token_is_identifier(t, "bytes")) {
				vir_parse_token *b;
				size_t n;

				p->position++;
				b = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_STR, "a string");
				it.type = 0xfe;
				it.bytes = vir_parse_token_string(p, b, &n);
				it.value = n;
			} else {
				uint32_t ty = vir_parse_token_type(t);
				bool neg = false;

				if (!ty || ty == VIR_TYPE_VOID || ty == VIR_TYPE_MULTI)
					VIR_PARSE_ERROR(p, t, "expected a data item (i32, i64, f32, f64, ptr, zero, bytes)");
				p->position++;
				it.type = (uint8_t)ty;
				if (ty == VIR_TYPE_PTR && vir_parse_token_current(p)->kind == VIR_PARSE_TOKEN_SYM) {
					vir_parse_token *y = vir_parse_token_next(p);

					it.symbol = vir_parse_symbol_reference(p, y) + 1;
					if (vir_parse_token_is_punctuation(vir_parse_token_current(p), '+') || vir_parse_token_is_punctuation(vir_parse_token_current(p), '-')) {
						bool ng = vir_parse_token_is_punctuation(vir_parse_token_current(p), '-');

						p->position++;
						it.value = vir_parse_integer_expect(p, false, NULL);
						if (ng)
							it.value = -it.value;
					}
				} else if (ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64) {
					bool ng = vir_parse_punctuation_accept(p, '-');
					vir_parse_token *f = vir_parse_token_current(p);
					char tmp[96];

					if (f->kind == VIR_PARSE_TOKEN_ID && vir_parse_token_is_identifier(f, "bits") && vir_parse_token_is_punctuation(vir_parse_token_peek(p, 1), ':')) {
						p->position += 2;
						it.value = vir_parse_integer_expect(p, false, NULL);
					} else {
						double d;

						if (f->kind != VIR_PARSE_TOKEN_FLT && f->kind != VIR_PARSE_TOKEN_INT)
							VIR_PARSE_ERROR(p, f, "expected a floating-point constant");
						if (f->len >= sizeof(tmp))
							VIR_PARSE_ERROR(p, f, "floating-point constant too long");
						memcpy(tmp, f->string, f->len);
						tmp[f->len] = 0;
						d = strtod(tmp, NULL);
						p->position++;
						if (ng)
							d = -d;
						if (ty == VIR_TYPE_F32) {
							float fl = (float)d;

							if ((double)fl != d)
								VIR_PARSE_ERROR(p, f, "not exactly representable as f32");
							memcpy(&it.value, &fl, 4);
							it.value &= 0xffffffffu;
						} else {
							memcpy(&it.value, &d, 8);
						}
					}
				} else {
					it.value = vir_parse_integer_expect(p, true, &neg);
					if (ty == VIR_TYPE_I32 && (neg ? (int64_t)it.value < INT32_MIN : it.value > UINT32_MAX))
						VIR_PARSE_ERROR(p, t, "constant out of range for i32");
				}
			}
			if (u->item_count == u->item_capacity) {
				u->item_capacity = u->item_capacity ? u->item_capacity * 2 : 64;
				u->item = realloc(u->item, (size_t)u->item_capacity * sizeof(vir_item));
				if (!u->item)
					abort();
			}
			u->item[u->item_count++] = it;
			u->symbols[s].item_count++;
			if (!vir_parse_punctuation_accept(p, ','))
				break;
		}
	vir_parse_punctuation_expect(p, '}');
	vir_parse_newline_expect(p);
}

/* ---------------- operands ---------------- */

static uint32_t vir_parse_operand_push(vir_parse_state *p, vir_parse_operand_record o)
{
	VIR_PARSE_VECTOR_PUSH(p->operands, o);
	return p->operands.n - 1;
}

static void vir_parse_value_name_check(vir_parse_state *p, const vir_parse_token *t)
{
	if (t->len < 2 || t->string[0] != 'v')
		VIR_PARSE_ERROR(p, t, "expected a value (vN) or a constant");
}

/* the entry of a value or block name; blocks and values are separate tables */
static uint32_t vir_parse_name_reference(vir_parse_state *p, vir_parse_names *m, const vir_parse_token *t)
{
	bool isnew;
	uint32_t e = vir_parse_name_get(m, t->string, t->len, &isnew);

	if (isnew) {
		m->entries.v[e].line = t->line;
		m->entries.v[e].column = t->column;
	}
	return e;
}

static uint32_t vir_parse_block_reference(vir_parse_state *p)
{
	vir_parse_token *t = vir_parse_token_current(p);

	if (t->kind != VIR_PARSE_TOKEN_ID || t->len < 2 || t->string[0] != 'b')
		VIR_PARSE_ERROR(p, t, "expected a block (bN)");
	p->position++;
	return vir_parse_name_reference(p, &p->block_records, t);
}

/* an operand as written: a value name or a constant with its optional :type */
static uint32_t vir_parse_operand_read(vir_parse_state *p)
{
	vir_parse_token *t = vir_parse_token_current(p);
	vir_parse_operand_record o;
	bool neg = false;

	memset(&o, 0, sizeof(o));
	o.type = 0xff;
	o.line = t->line;
	o.column = t->column;
	if (t->kind == VIR_PARSE_TOKEN_ID && !(vir_parse_token_is_identifier(t, "bits") && vir_parse_token_is_punctuation(vir_parse_token_peek(p, 1), ':') && vir_parse_token_peek(p, 2)->kind == VIR_PARSE_TOKEN_INT)) {
		vir_parse_value_name_check(p, t);
		p->position++;
		o.kind = VIR_PARSE_OPERAND_KIND_NAME;
		o.id = vir_parse_name_reference(p, &p->values, t);
		return vir_parse_operand_push(p, o);
	}
	if (t->kind == VIR_PARSE_TOKEN_ID) {	/* bits:0x... */
		p->position += 2;
		o.kind = VIR_PARSE_OPERAND_KIND_BITS;
		o.value = vir_parse_integer_expect(p, false, NULL);
	} else if (t->kind == VIR_PARSE_TOKEN_SYM || t->kind == VIR_PARSE_TOKEN_SLOT) {
		bool ng = false;

		p->position++;
		o.addend = 0;
		if (vir_parse_token_is_punctuation(vir_parse_token_current(p), '+') || vir_parse_token_is_punctuation(vir_parse_token_current(p), '-')) {
			ng = vir_parse_token_is_punctuation(vir_parse_token_current(p), '-');
			p->position++;
			o.addend = (int64_t)vir_parse_integer_expect(p, false, NULL);
			if (ng)
				o.addend = -o.addend;
		}
		if (t->kind == VIR_PARSE_TOKEN_SYM) {
			o.kind = VIR_PARSE_OPERAND_KIND_SYM;
			o.id = vir_parse_symbol_reference(p, t);
		} else {
			bool isnew;
			uint32_t e = vir_parse_name_get(&p->slot_names, t->string, t->len, &isnew);

			if (isnew || !p->slot_names.entries.v[e].state)
				VIR_PARSE_ERROR(p, t, "undefined slot $%.*s", (int)t->len, t->string);
			o.kind = VIR_PARSE_OPERAND_KIND_SLOT;
			o.id = p->slot_names.entries.v[e].id;
		}
	} else if (vir_parse_token_is_punctuation(t, '&')) {
		vir_parse_token *b;

		p->position++;
		b = vir_parse_token_current(p);
		if (b->kind != VIR_PARSE_TOKEN_ID || b->len < 2 || b->string[0] != 'b')
			VIR_PARSE_ERROR(p, b, "expected a block (bN) after `&`");
		p->position++;
		o.kind = VIR_PARSE_OPERAND_KIND_LABEL;
		o.id = vir_parse_name_reference(p, &p->block_records, b);
	} else if (vir_parse_token_is_punctuation(t, '-') || t->kind == VIR_PARSE_TOKEN_INT || t->kind == VIR_PARSE_TOKEN_FLT) {
		vir_parse_token *n;

		if (vir_parse_punctuation_accept(p, '-'))
			neg = true;
		n = vir_parse_token_current(p);
		if (n->kind == VIR_PARSE_TOKEN_INT) {
			o.kind = VIR_PARSE_OPERAND_KIND_INT;
			o.value = vir_parse_integer_expect(p, false, NULL);
			if (neg) {
				if (o.value > (uint64_t)INT64_MAX + 1)
					VIR_PARSE_ERROR(p, n, "integer constant too large");
				o.value = -o.value;
			}
		} else if (n->kind == VIR_PARSE_TOKEN_FLT) {
			char tmp[96];
			double d;

			if (n->len >= sizeof(tmp))
				VIR_PARSE_ERROR(p, n, "floating-point constant too long");
			memcpy(tmp, n->string, n->len);
			tmp[n->len] = 0;
			d = strtod(tmp, NULL);
			if (neg)
				d = -d;
			p->position++;
			o.kind = VIR_PARSE_OPERAND_KIND_FLT;
			memcpy(&o.value, &d, 8);
		} else {
			VIR_PARSE_ERROR(p, n, "expected a constant");
		}
		o.is_negative = neg;
	} else {
		VIR_PARSE_ERROR(p, t, "expected a value or a constant");
	}
	if (vir_parse_token_is_punctuation(vir_parse_token_current(p), ':'))	/* the type suffix of a constant */
		if (vir_parse_token_type(vir_parse_token_peek(p, 1))) {
			p->position++;
			o.type = (uint8_t)vir_parse_type_expect(p);
		}
	return vir_parse_operand_push(p, o);
}

static void vir_parse_fixup_add(vir_parse_state *p, vir_parse_fixup x) { VIR_PARSE_VECTOR_PUSH(p->fixups, x); }

/* an operand in field fld of the instruction about to be pushed */
static uint32_t vir_parse_field_value(vir_parse_state *p, uint32_t fld)
{
	uint32_t po = vir_parse_operand_read(p);
	vir_parse_fixup x = { VIR_PARSE_FIXUP_VAL, VIR_PARSE_LIST_MODE_POS, (uint8_t)fld, p->blocks.n, 0, po, 0, 0, p->operands.v[po].line, p->operands.v[po].column };

	vir_parse_fixup_add(p, x);
	return 0;
}

static uint32_t vir_parse_field_block(vir_parse_state *p, uint32_t fld)
{
	vir_parse_token *t = vir_parse_token_current(p);
	uint32_t bn = vir_parse_block_reference(p);
	vir_parse_fixup x = { VIR_PARSE_FIXUP_BLK, 0, (uint8_t)fld, p->blocks.n, 0, bn, 0, 0, t->line, t->column };

	vir_parse_fixup_add(p, x);
	return 0;
}

/* an extra word that holds a value or a block id: reserved by the caller, fixed up later */
static void vir_parse_list_value(vir_parse_state *p, uint32_t at, uint32_t mode, uint32_t aux, uint32_t idx, uint32_t po)
{
	vir_parse_fixup x = { VIR_PARSE_FIXUP_VAL, (uint8_t)mode, 3, p->blocks.n, at, po, aux, idx, p->operands.v[po].line, p->operands.v[po].column };

	vir_parse_fixup_add(p, x);
}

static void vir_parse_list_block(vir_parse_state *p, uint32_t at, uint32_t bn, vir_parse_token *t)
{
	vir_parse_fixup x = { VIR_PARSE_FIXUP_BLK, 0, 3, p->blocks.n, at, bn, 0, 0, t->line, t->column };

	vir_parse_fixup_add(p, x);
}

/* ---------------- flags ---------------- */

static bool vir_parse_form_has_low_bit_fields(uint32_t form)
{
	return form == VIR_FORM_LOAD || form == VIR_FORM_STORE || form == VIR_FORM_ATOM || form == VIR_FORM_EXT || form == VIR_FORM_CC;
}

static void vir_parse_flags(vir_parse_state *p, vir_insn *in)
{
	const vir_opcode_info *oi = &vir_opcode_table[in->op];
	static const struct { const char *n; uint32_t bit; bool low; } F[] = {
		{ "nsw", VIR_INSN_FLAG_NSW, 1 }, { "nuw", VIR_INSN_FLAG_NUW, 1 }, { "exact", VIR_INSN_FLAG_EXACT, 1 }, { "nz", VIR_INSN_FLAG_NZ, 1 },
		{ "volatile", VIR_INSN_FLAG_VOL, 0 }, { "noreturn", VIR_INSN_FLAG_CALL_NORETURN, 1 }, { "pure", VIR_INSN_FLAG_CALL_PURE, 1 },
		{ "const", VIR_INSN_FLAG_CALL_CONST, 1 }, { "likely", VIR_INSN_FLAG_LIKELY, 1 }, { "unlikely", VIR_INSN_FLAG_UNLIKELY, 1 },
		{ "negp", VIR_INSN_FLAG_NEGP, 1 }, { "negc", VIR_INSN_FLAG_NEGC, 1 },
	};
	int nalign = 0;

	for (;;) {
		vir_parse_token *t = vir_parse_token_current(p);
		bool found = false;

		if (t->kind != VIR_PARSE_TOKEN_ID)
			return;
		if (vir_parse_token_is_identifier(t, "align")) {
			vir_parse_token *at;
			uint32_t l;

			p->position++;
			at = vir_parse_token_current(p);
			l = vir_parse_alignment_log2(p, at, vir_parse_integer_expect(p, false, NULL), 15);
			switch (oi->form) {
			case VIR_FORM_LOAD: case VIR_FORM_STORE: case VIR_FORM_ATOM:
				in->flags = (uint16_t)((in->flags & ~0xf0) | l << 4);
				break;
			case VIR_FORM_MEMCPY:
				if (in->op == VIR_OP_MEMCPY && nalign == 1) {
					in->flags = (uint16_t)((in->flags & ~0xf00) | l << 8);
					nalign = 2;
					continue;
				}
				if (nalign)
					VIR_PARSE_ERROR(p, t, "too many alignments");
				in->flags = (uint16_t)((in->flags & ~0xff0) | l << 4 | (in->op == VIR_OP_MEMCPY ? l << 8 : 0));
				nalign = 1;
				continue;
			default:
				VIR_PARSE_ERROR(p, t, "`align` is not allowed on %s", oi->name);
			}
			continue;
		}
		if (vir_parse_token_is_identifier(t, "tag")) {
			uint32_t v;

			p->position++;
			v = vir_parse_unsigned_expect(p, "the tag", UINT32_MAX);
			if (in->op == VIR_OP_LOAD)
				in->b = v;
			else if (in->op == VIR_OP_STORE)
				in->c = v;
			else
				VIR_PARSE_ERROR(p, t, "`tag` is not allowed on %s", oi->name);
			continue;
		}
		for (size_t i = 0; i < sizeof(F) / sizeof(F[0]); i++)
			if (vir_parse_token_is_identifier(t, F[i].n)) {
				if (F[i].low && vir_parse_form_has_low_bit_fields(oi->form))
					VIR_PARSE_ERROR(p, t, "flag `%s` is not allowed on %s", F[i].n, oi->name);
				in->flags |= F[i].bit;
				p->position++;
				found = true;
				break;
			}
		if (!found)
			VIR_PARSE_ERROR(p, t, "unknown flag `%.*s`", (int)t->len, t->string);
	}
}

/* ---------------- instructions ---------------- */

/* the suffix of an op token: "slt" of `icmp.slt` (NULL when there is none) */
static void vir_parse_opcode_split(const vir_parse_token *t, const char **nm_, size_t *nl, const char **sfx, size_t *sl)
{
	const char *d = memchr(t->string, '.', t->len);

	*nm_ = t->string;
	if (d) {
		*nl = (size_t)(d - t->string);
		*sfx = d + 1;
		*sl = t->len - *nl - 1;
	} else {
		*nl = t->len;
		*sfx = NULL;
		*sl = 0;
	}
}

static void vir_parse_memory_suffix(vir_parse_state *p, vir_parse_token *t, const char *sfx, size_t sl, vir_insn *in, bool sign_ok, bool sign_req)
{
	uint32_t w = 0;
	size_t i = 0;

	if (!sfx || !sl || sfx[0] < '1' || sfx[0] > '8')
		VIR_PARSE_ERROR(p, t, "expected a width suffix (.1 .2 .4 .8) on %s", vir_opcode_table[in->op].name);
	w = (uint32_t)(sfx[0] - '0');
	i = 1;
	if (w != 1 && w != 2 && w != 4 && w != 8)
		VIR_PARSE_ERROR(p, t, "the width must be 1, 2, 4 or 8");
	in->flags |= (uint16_t)(w == 1 ? 0 : w == 2 ? 1 : w == 4 ? 2 : 3);
	if (i < sl && (sfx[i] == 's' || sfx[i] == 'u')) {
		if (!sign_ok)
			VIR_PARSE_ERROR(p, t, "%s has no sign suffix", vir_opcode_table[in->op].name);
		if (sfx[i] == 's')
			in->flags |= VIR_INSN_FLAG_MS;
		i++;
	} else if (sign_req) {
		VIR_PARSE_ERROR(p, t, "expected `s` or `u` after the width");
	}
	if (i != sl)
		VIR_PARSE_ERROR(p, t, "malformed suffix");
}

/* operand list `v1, 5:i64` into the extra words [off, off+n) (a vector of pops first) */
static void vir_parse_operand_list(vir_parse_state *p, uint32_t term, uint32_t mode, uint32_t aux, uint32_t *npops)
{
	/* the pops are collected in scratch, then the caller reserves the words */
	p->scratch.n = 0;
	if (!(term == 1 ? vir_parse_token_is_punctuation(vir_parse_token_current(p), ')') : term == 2 ? vir_parse_token_current(p)->kind == VIR_PARSE_TOKEN_NL || vir_parse_token_current(p)->kind == VIR_PARSE_TOKEN_EOF : false))
		for (;;) {
			VIR_PARSE_VECTOR_PUSH(p->scratch, vir_parse_operand_read(p));
			if (!vir_parse_punctuation_accept(p, ','))
				break;
		}
	(void)mode;
	(void)aux;
	*npops = p->scratch.n;
}

static void vir_parse_insn(vir_parse_state *p, uint32_t rty)
{
	vir_parse_token *ot = vir_parse_token_next(p);
	const char *nmp, *sfx;
	size_t nl, sl;
	const vir_opcode_info *oi;
	vir_insn in;
	vir_parse_position pos = { ot->line, ot->column, 0, 0, 0 };

	vir_parse_opcode_split(ot, &nmp, &nl, &sfx, &sl);
	oi = vir_opcode_find(nmp, nl);
	if (!oi)
		VIR_PARSE_ERROR(p, ot, "unknown instruction `%.*s`", (int)nl, nmp);
	memset(&in, 0, sizeof(in));
	in.op = oi->op;
	in.type = (uint8_t)rty;
	if (!p->in_function || !p->parsed_block.n)
		VIR_PARSE_ERROR(p, ot, "an instruction must be inside a block");
	if (sfx && oi->form != VIR_FORM_CC && oi->form != VIR_FORM_EXT && oi->form != VIR_FORM_LOAD && oi->form != VIR_FORM_STORE &&
	    oi->form != VIR_FORM_ATOM)
		VIR_PARSE_ERROR(p, ot, "%s takes no suffix", oi->name);
	switch (oi->form) {
	case VIR_FORM_CONST: {	/* a named constant: it becomes an entry of the constant region as written */
		vir_parse_constant_definition d;
		uint32_t po = vir_parse_operand_read(p);

		memset(&d, 0, sizeof(d));
		d.insn = in;
		d.operand = po;
		d.name = ~0u;
		vir_parse_flags(p, &d.insn);
		vir_parse_newline_expect(p);
		if (d.insn.flags)
			VIR_PARSE_ERROR(p, ot, "a constant has no flags");
		VIR_PARSE_VECTOR_PUSH(p->constant_definitions, d);
		return;	/* the caller records the name */
	}
	case VIR_FORM_BPARAM:	/* normally written in the block header; here it is a stray one (verifier rule S4) */
	case VIR_FORM_NONE:
		break;
	case VIR_FORM_CC: {
		uint32_t cc = ~0u;

		if (!sfx)
			VIR_PARSE_ERROR(p, ot, "expected a condition code suffix");
		if (oi->op == VIR_OP_ICMP || oi->op == VIR_OP_VICMP) {
			for (uint32_t i = 0; i < 10; i++)
				if (strlen(vir_integer_condition_names[i]) == sl && !memcmp(vir_integer_condition_names[i], sfx, sl))
					cc = i;
		} else {
			for (uint32_t i = 0; i < 8; i++)
				if (strlen(vir_float_condition_names[i]) == sl && !memcmp(vir_float_condition_names[i], sfx, sl))
					cc = i;
		}
		if (cc == ~0u)
			VIR_PARSE_ERROR(p, ot, "unknown condition code `%.*s`", (int)sl, sfx);
		in.flags = (uint16_t)cc;
		in.a = vir_parse_field_value(p, 0);
		vir_parse_punctuation_expect(p, ',');
		in.b = vir_parse_field_value(p, 1);
		break;
	}
	case VIR_FORM_EXT: {
		uint32_t w;

		if (!sfx || sl < 2 || sfx[0] < '1' || sfx[0] > '8' || (sfx[1] != 's' && sfx[1] != 'u') || sl != 2)
			VIR_PARSE_ERROR(p, ot, "expected ext.<width><s|u>");
		w = (uint32_t)(sfx[0] - '0');
		if (w != 1 && w != 2 && w != 4 && w != 8)
			VIR_PARSE_ERROR(p, ot, "the width must be 1, 2, 4 or 8");
		in.flags = (uint16_t)((w == 1 ? 0 : w == 2 ? 1 : w == 4 ? 2 : 3) | (sfx[1] == 's' ? VIR_INSN_FLAG_MS : 0));
		in.a = vir_parse_field_value(p, 0);
		break;
	}
	case VIR_FORM_LOAD:
		vir_parse_memory_suffix(p, ot, sfx, sl, &in, true, false);
		in.a = vir_parse_field_value(p, 0);
		break;
	case VIR_FORM_STORE:
		vir_parse_memory_suffix(p, ot, sfx, sl, &in, false, false);
		in.a = vir_parse_field_value(p, 0);
		vir_parse_punctuation_expect(p, ',');
		in.b = vir_parse_field_value(p, 1);
		break;
	case VIR_FORM_ATOM:
		vir_parse_memory_suffix(p, ot, sfx, sl, &in, false, false);
		for (uint32_t k = 0; k < 3 && oi->operand_kinds[k] != VIR_OPERAND_KIND_NONE; k++) {
			uint32_t v;

			if (k)
				vir_parse_punctuation_expect(p, ',');
			v = vir_parse_field_value(p, k);
			if (k == 0) in.a = v; else if (k == 1) in.b = v; else in.c = v;
		}
		break;
	case VIR_FORM_MEMCPY:
		in.a = vir_parse_field_value(p, 0);
		vir_parse_punctuation_expect(p, ',');
		in.b = vir_parse_field_value(p, 1);
		vir_parse_punctuation_expect(p, ',');
		in.c = vir_parse_field_value(p, 2);
		break;
	case VIR_FORM_PARAM: {
		vir_parse_token *g = vir_parse_token_current(p);

		in.a = vir_parse_unsigned_expect(p, "the group", 0xffff);
		(void)g;
		vir_parse_punctuation_expect(p, '.');
		in.b = vir_parse_unsigned_expect(p, "the piece", 0xffff);
		break;
	}
	case VIR_FORM_TLS: {
		vir_parse_token *s = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_SYM, "a symbol (@name)");

		in.a = vir_parse_symbol_reference(p, s);
		vir_parse_punctuation_expect(p, ',');
		in.b = vir_parse_unsigned_expect(p, "the model", UINT32_MAX);
		break;
	}
	case VIR_FORM_VAARG: {
		uint32_t w0, w1, g[2];

		in.a = vir_parse_field_value(p, 0);
		vir_parse_punctuation_expect(p, ',');
		w0 = vir_parse_group(p, &w1);
		g[0] = w0;
		g[1] = w1;
		in.b = vir_extra_add(&p->function, g, 2);
		break;
	}
	case VIR_FORM_CALL: {
		vir_parse_token *st;
		bool isnew;
		uint32_t e, si, n, off;

		in.a = vir_parse_field_value(p, 0);
		st = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_SIG, "a signature (%name)");
		e = vir_parse_name_get(&p->signature_name, st->string, st->len, &isnew);
		if (isnew || !p->signature_name.entries.v[e].state)
			VIR_PARSE_ERROR(p, st, "undeclared signature %%%.*s", (int)st->len, st->string);
		si = p->signature_name.entries.v[e].id;
		if (p->signature_offset.v[si] == ~0u)
			p->signature_offset.v[si] = vir_extra_add(&p->function, &p->unit->signature_words[p->unit->signatures[si].offset], p->unit->signatures[si].count);
		in.b = p->signature_offset.v[si];
		vir_parse_punctuation_expect(p, '(');
		vir_parse_operand_list(p, 1, VIR_PARSE_LIST_MODE_LIST, 0, &n);
		vir_parse_punctuation_expect(p, ')');
		{
			uint32_t *w = calloc((size_t)n + 1, 4);

			if (!w)
				abort();
			w[0] = n;
			off = vir_extra_add(&p->function, w, n + 1);
			free(w);
		}
		in.c = off;
		for (uint32_t i = 0; i < n; i++)
			vir_parse_list_value(p, off + 1 + i, VIR_PARSE_LIST_MODE_LIST, 0, i, p->scratch.v[i]);
		break;
	}
	case VIR_FORM_ASM: case VIR_FORM_ASMGOTO: {
		vir_parse_token *tt = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_ASM, "an asm template (!name)");
		bool isnew;
		uint32_t e = vir_parse_name_get(&p->asm_name, tt->string, tt->len, &isnew), no, ni, off;
		PV(uint32_t) w = { 0 };
		PV(uint32_t) vs = { 0 };

		if (isnew || !p->asm_name.entries.v[e].state)
			VIR_PARSE_ERROR(p, tt, "undeclared asm template !%.*s", (int)tt->len, tt->string);
		in.a = p->asm_name.entries.v[e].id;
		vir_parse_keyword_expect(p, "outs");
		vir_parse_punctuation_expect(p, '(');
		if (!vir_parse_token_is_punctuation(vir_parse_token_current(p), ')'))
			for (;;) {
				VIR_PARSE_VECTOR_PUSH(w, vir_parse_unsigned_expect(p, "the operand index", UINT32_MAX));
				if (!vir_parse_punctuation_accept(p, ','))
					break;
			}
		vir_parse_punctuation_expect(p, ')');
		no = w.n;
		vir_parse_keyword_expect(p, "ins");
		vir_parse_punctuation_expect(p, '(');
		ni = 0;
		if (!vir_parse_token_is_punctuation(vir_parse_token_current(p), ')'))
			for (;;) {
				VIR_PARSE_VECTOR_PUSH(w, vir_parse_unsigned_expect(p, "the operand index", UINT32_MAX));
				VIR_PARSE_VECTOR_PUSH(w, 0);
				VIR_PARSE_VECTOR_PUSH(vs, vir_parse_operand_read(p));
				ni++;
				if (!vir_parse_punctuation_accept(p, ','))
					break;
			}
		vir_parse_punctuation_expect(p, ')');
		{
			uint32_t *all = malloc(((size_t)w.n + 2) * 4);

			if (!all)
				abort();
			all[0] = no;
			all[1] = ni;
			if (w.n)
				memcpy(&all[2], w.v, (size_t)w.n * 4);
			off = vir_extra_add(&p->function, all, w.n + 2);
			free(all);
		}
		in.b = off;
		for (uint32_t i = 0; i < ni; i++)
			vir_parse_list_value(p, off + 2 + no + 2 * i + 1, VIR_PARSE_LIST_MODE_LIST, 0, i, vs.v[i]);
		VIR_PARSE_VECTOR_FREE(w);
		VIR_PARSE_VECTOR_FREE(vs);
		if (oi->form == VIR_FORM_ASMGOTO) {
			uint32_t ft, nl2 = 0, loff;
			PV(uint32_t) ls = { 0 };
			vir_parse_token *ftt;

			vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_ARROW, "`->`");
			ftt = vir_parse_token_current(p);
			ft = vir_parse_block_reference(p);
			vir_parse_punctuation_expect(p, '[');
			PV(vir_parse_token *) lts = { 0 };

			if (!vir_parse_token_is_punctuation(vir_parse_token_current(p), ']'))
				for (;;) {
					vir_parse_token *bt = vir_parse_token_current(p);

					VIR_PARSE_VECTOR_PUSH(ls, vir_parse_block_reference(p));
					VIR_PARSE_VECTOR_PUSH(lts, bt);
					nl2++;
					if (!vir_parse_punctuation_accept(p, ','))
						break;
				}
			vir_parse_punctuation_expect(p, ']');
			{
				uint32_t *all = calloc((size_t)nl2 + 2, 4);

				if (!all)
					abort();
				all[0] = nl2;
				loff = vir_extra_add(&p->function, all, nl2 + 2);
				free(all);
			}
			in.c = loff;
			vir_parse_list_block(p, loff + 1, ft, ftt);
			for (uint32_t i = 0; i < nl2; i++)
				vir_parse_list_block(p, loff + 2 + i, ls.v[i], lts.v[i]);
			VIR_PARSE_VECTOR_FREE(ls);
			VIR_PARSE_VECTOR_FREE(lts);
		}
		break;
	}
	case VIR_FORM_BR: {
		vir_parse_token *bt = vir_parse_token_current(p);
		uint32_t bn = vir_parse_block_reference(p), n = 0, off = 0;

		vir_parse_fixup x = { VIR_PARSE_FIXUP_BLK, 0, 0, p->blocks.n, 0, bn, 0, 0, bt->line, bt->column };

		vir_parse_fixup_add(p, x);
		if (vir_parse_punctuation_accept(p, '(')) {
			vir_parse_operand_list(p, 1, VIR_PARSE_LIST_MODE_BR, bn, &n);
			vir_parse_punctuation_expect(p, ')');
			if (n) {
				off = vir_extra_add(&p->function, p->scratch.v, n);	/* the words hold pop indices for now */
				for (uint32_t i = 0; i < n; i++)
					vir_parse_list_value(p, off + i, VIR_PARSE_LIST_MODE_BR, bn, i, p->scratch.v[i]);
			}
		}
		in.c = off;
		pos.count = n;
		break;
	}
	case VIR_FORM_SWITCH: {
		uint32_t bd;
		vir_parse_token *bt;

		in.a = vir_parse_field_value(p, 0);
		vir_parse_punctuation_expect(p, ',');
		bt = vir_parse_token_current(p);
		bd = vir_parse_block_reference(p);
		{
			vir_parse_fixup x = { VIR_PARSE_FIXUP_BLK, 0, 1, p->blocks.n, 0, bd, 0, 0, bt->line, bt->column };

			vir_parse_fixup_add(p, x);
		}
		vir_parse_punctuation_expect(p, '[');
		pos.column_start = p->switch_cases.n;
		if (!vir_parse_token_is_punctuation(vir_parse_token_current(p), ']'))
			for (;;) {
				vir_parse_switch_case c;
				bool n1, n2;
				vir_parse_token *ct = vir_parse_token_current(p);

				c.low = vir_parse_integer_expect(p, true, &n1);
				c.high = c.low;
				if (vir_parse_token_current(p)->kind == VIR_PARSE_TOKEN_DOTDOT) {
					p->position++;
					c.high = vir_parse_integer_expect(p, true, &n2);
					(void)n2;
				}
				(void)n1;
				vir_parse_punctuation_expect(p, ':');
				c.line = ct->line;
				c.column = ct->column;
				c.block = vir_parse_block_reference(p);
				if (p->switch_cases.n - pos.column_start >= VIR_MAX_SWITCH)
					VIR_PARSE_ERROR(p, ct, "too many switch cases");
				VIR_PARSE_VECTOR_PUSH(p->switch_cases, c);
				if (!vir_parse_punctuation_accept(p, ','))
					break;
			}
		vir_parse_punctuation_expect(p, ']');
		pos.column_end = p->switch_cases.n;
		break;
	}
	case VIR_FORM_IBR: {
		uint32_t n = 0, off;
		PV(uint32_t) ls = { 0 };
		PV(vir_parse_token *) lts = { 0 };

		in.a = vir_parse_field_value(p, 0);
		vir_parse_punctuation_expect(p, '[');
		if (!vir_parse_token_is_punctuation(vir_parse_token_current(p), ']'))
			for (;;) {
				vir_parse_token *bt = vir_parse_token_current(p);

				VIR_PARSE_VECTOR_PUSH(ls, vir_parse_block_reference(p));
				VIR_PARSE_VECTOR_PUSH(lts, bt);
				n++;
				if (!vir_parse_punctuation_accept(p, ','))
					break;
			}
		vir_parse_punctuation_expect(p, ']');
		{
			uint32_t *all = calloc((size_t)n + 1, 4);

			if (!all)
				abort();
			all[0] = n;
			off = vir_extra_add(&p->function, all, n + 1);
			free(all);
		}
		in.c = off;
		for (uint32_t i = 0; i < n; i++)
			vir_parse_list_block(p, off + 1 + i, ls.v[i], lts.v[i]);
		VIR_PARSE_VECTOR_FREE(ls);
		VIR_PARSE_VECTOR_FREE(lts);
		break;
	}
	case VIR_FORM_RET: {
		uint32_t n, off;

		vir_parse_operand_list(p, 2, VIR_PARSE_LIST_MODE_LIST, 0, &n);
		{
			uint32_t *all = calloc((size_t)n + 1, 4);

			if (!all)
				abort();
			all[0] = n;
			off = vir_extra_add(&p->function, all, n + 1);
			free(all);
		}
		in.c = off;
		for (uint32_t i = 0; i < n; i++)
			vir_parse_list_value(p, off + 1 + i, VIR_PARSE_LIST_MODE_LIST, 0, i, p->scratch.v[i]);
		break;
	}
	default: {	/* VIR_FORM_STD: the operands the table lists, in order */
		uint32_t nop = 0;

		for (uint32_t k = 0; k < 3; k++) {
			uint32_t kd = oi->operand_kinds[k], v = 0;

			if (kd == VIR_OPERAND_KIND_NONE)
				continue;
			if (nop++)
				vir_parse_punctuation_expect(p, ',');
			if (kd == VIR_OPERAND_KIND_IMM)
				v = vir_parse_unsigned_expect(p, "the immediate", UINT32_MAX);
			else if (kd == VIR_OPERAND_KIND_BLK)
				v = vir_parse_field_block(p, k);
			else
				v = vir_parse_field_value(p, k);
			if (k == 0) in.a = v; else if (k == 1) in.b = v; else in.c = v;
		}
		break;
	}
	}
	vir_parse_flags(p, &in);
	vir_parse_newline_expect(p);
	VIR_PARSE_VECTOR_PUSH(p->blocks, in);
	VIR_PARSE_VECTOR_PUSH(p->block_parameter_count, pos);
	p->parsed_block.v[p->parsed_block.n - 1].insn_count++;
}

/* ---------------- function ---------------- */

static void vir_parse_function_reset(vir_parse_state *p)
{
	vir_parse_names_reset(&p->values);
	vir_parse_names_reset(&p->block_records);
	vir_parse_names_reset(&p->slot_names);
	p->blocks.n = p->block_parameter_count.n = p->parsed_block.n = p->operands.n = p->fixups.n = p->switch_cases.n = p->constant_definitions.n = 0;
	p->signature_offset.n = 0;
	for (uint32_t i = 0; i < p->unit->signature_count; i++)
		VIR_PARSE_VECTOR_PUSH(p->signature_offset, ~0u);
	p->scratch.n = 0;
}

static void vir_parse_value_define(vir_parse_state *p, const vir_parse_token *t, uint32_t st, uint32_t id)
{
	bool isnew;
	uint32_t e = vir_parse_name_get(&p->values, t->string, t->len, &isnew);
	vir_parse_name_entry *n;

	n = &p->values.entries.v[e];
	if (isnew) {
		n->line = t->line;
		n->column = t->column;
	}
	if (n->state)
		VIR_PARSE_ERROR(p, t, "value %.*s defined twice", (int)t->len, t->string);
	n->state = st;
	n->id = id;
}

static void vir_parse_block_header(vir_parse_state *p)
{
	vir_parse_token *bt = vir_parse_token_next(p);
	bool isnew;
	uint32_t e = vir_parse_name_get(&p->block_records, bt->string, bt->len, &isnew);
	vir_parse_block b = { p->blocks.n, 0, 0, 0, e };

	if (isnew) {
		p->block_records.entries.v[e].line = bt->line;
		p->block_records.entries.v[e].column = bt->column;
	}
	if (p->block_records.entries.v[e].state)
		VIR_PARSE_ERROR(p, bt, "block %.*s defined twice", (int)bt->len, bt->string);
	p->block_records.entries.v[e].state = 1;
	p->block_records.entries.v[e].id = p->parsed_block.n;
	if (bt->len < 2 || bt->string[0] != 'b')
		VIR_PARSE_ERROR(p, bt, "a block is named bN");
	VIR_PARSE_VECTOR_PUSH(p->parsed_block, b);
	if (vir_parse_punctuation_accept(p, '(')) {
		for (;;) {
			vir_parse_token *vt = vir_parse_token_current(p);
			vir_insn in;
			vir_parse_position pos = { vt->line, vt->column, 0, 0, 0 };

			vir_parse_value_name_check(p, vt);
			if (p->parsed_block.v[p->parsed_block.n - 1].parameter_count >= VIR_MAX_BPARAM)
				VIR_PARSE_ERROR(p, vt, "too many block parameters");
			p->position++;
			vir_parse_punctuation_expect(p, ':');
			memset(&in, 0, sizeof(in));
			in.op = VIR_OP_BPARAM;
			in.type = (uint8_t)vir_parse_type_expect(p);
			vir_parse_value_define(p, vt, 1, p->blocks.n);
			VIR_PARSE_VECTOR_PUSH(p->blocks, in);
			VIR_PARSE_VECTOR_PUSH(p->block_parameter_count, pos);
			p->parsed_block.v[p->parsed_block.n - 1].insn_count++;
			p->parsed_block.v[p->parsed_block.n - 1].parameter_count++;
			if (!vir_parse_punctuation_accept(p, ','))
				break;
		}
		vir_parse_punctuation_expect(p, ')');
	}
	for (;;) {
		vir_parse_token *t = vir_parse_token_current(p);

		if (vir_parse_token_is_identifier(t, "loop"))
			p->parsed_block.v[p->parsed_block.n - 1].flags |= VIR_BLOCK_FLAG_LOOP;
		else if (vir_parse_token_is_identifier(t, "addr"))
			p->parsed_block.v[p->parsed_block.n - 1].flags |= VIR_BLOCK_FLAG_ADDR;
		else if (vir_parse_token_is_identifier(t, "cold"))
			p->parsed_block.v[p->parsed_block.n - 1].flags |= VIR_BLOCK_FLAG_COLD;
		else
			break;
		p->position++;
	}
	vir_parse_punctuation_expect(p, ':');
	vir_parse_newline_expect(p);
}

/* the type of a value operand, or 0 when it is a constant without a suffix */
static uint32_t vir_parse_operand_type(vir_parse_state *p, const vir_parse_operand_record *o)
{
	if (o->kind == VIR_PARSE_OPERAND_KIND_NAME) {
		const vir_parse_name_entry *e = &p->values.entries.v[o->id];

		if (!e->state)
			vir_parse_error_at(p, o->line, o->column, "undefined value %.*s", (int)e->name_length, e->string);
		return e->state == 1 ? p->blocks.v[e->id].type : p->constant_definitions.v[e->id].insn.type;
	}
	return o->type == 0xff ? 0 : o->type;
}

/* the constant instruction for a pop of a given type; vir_constant_add or vir_constant_raw_add */
static uint32_t vir_parse_constant_make(vir_parse_state *p, const vir_parse_operand_record *o, uint32_t ty, bool raw)
{
	uint32_t op, a = 0, b = 0, c = 0, v;
	uint64_t x;

	if (!ty)
		vir_parse_error_at(p, o->line, o->column, "a `:type` suffix is required here");
	switch (o->kind) {
	case VIR_PARSE_OPERAND_KIND_INT:
		if (ty != VIR_TYPE_I32 && ty != VIR_TYPE_I64 && ty != VIR_TYPE_PTR)
			vir_parse_error_at(p, o->line, o->column, "an integer constant needs an integer or pointer type, not %s", vir_type_names[ty]);
		op = VIR_OP_ICONST;
		x = o->value;
		if (ty == VIR_TYPE_I32 && (o->is_negative ? (int64_t)x < INT32_MIN : x > UINT32_MAX))
			vir_parse_error_at(p, o->line, o->column, "constant out of range for i32");
		if (ty == VIR_TYPE_I32 && o->is_negative)
			x &= 0xffffffffu;
		b = (uint32_t)x;
		c = ty == VIR_TYPE_I32 ? 0 : (uint32_t)(x >> 32);
		break;
	case VIR_PARSE_OPERAND_KIND_FLT: case VIR_PARSE_OPERAND_KIND_BITS: {
		if (ty != VIR_TYPE_F32 && ty != VIR_TYPE_F64)
			vir_parse_error_at(p, o->line, o->column, "a floating-point constant needs f32 or f64, not %s", vir_type_names[ty]);
		op = VIR_OP_FCONST;
		x = o->value;
		if (o->kind == VIR_PARSE_OPERAND_KIND_FLT && ty == VIR_TYPE_F32) {
			double d;
			float fl;

			memcpy(&d, &x, 8);
			fl = (float)d;
			if ((double)fl != d)
				vir_parse_error_at(p, o->line, o->column, "not exactly representable as f32");
			x = 0;
			memcpy(&x, &fl, 4);
		}
		if (ty == VIR_TYPE_F32 && x > UINT32_MAX)
			vir_parse_error_at(p, o->line, o->column, "an f32 pattern has 32 bits");
		b = (uint32_t)x;
		c = ty == VIR_TYPE_F32 ? 0 : (uint32_t)(x >> 32);
		break;
	}
	case VIR_PARSE_OPERAND_KIND_SYM: case VIR_PARSE_OPERAND_KIND_SLOT:
		if (ty != VIR_TYPE_PTR)
			vir_parse_error_at(p, o->line, o->column, "an address constant has type ptr, not %s", vir_type_names[ty]);
		op = o->kind == VIR_PARSE_OPERAND_KIND_SYM ? VIR_OP_SYMADDR : VIR_OP_SLOTADDR;
		a = o->id;
		b = (uint32_t)(uint64_t)o->addend;
		c = (uint32_t)((uint64_t)o->addend >> 32);
		break;
	default:	/* VIR_PARSE_OPERAND_KIND_LABEL */
		if (ty != VIR_TYPE_PTR)
			vir_parse_error_at(p, o->line, o->column, "an address constant has type ptr, not %s", vir_type_names[ty]);
		op = VIR_OP_LABELADDR;
		a = p->block_records.entries.v[o->id].id;
		break;
	}
	v = raw ? vir_constant_raw_add(&p->function, op, ty, a, b, c) : vir_constant_add(&p->function, op, ty, a, b, c);
	if (!v)
		vir_parse_error_at(p, o->line, o->column, "function too large");
	return v;
}

static void vir_parse_function_finish(vir_parse_state *p, const vir_parse_token *close)
{
	vir_function *f = &p->function;
	uint32_t *cid, nconst, nb = p->parsed_block.n;
	PV(uint32_t) pc = { 0 };	/* the constant made for each fixup's pop (or 0) */

	if (!nb)
		vir_parse_error_at(p, close->line, close->column, "a function needs at least one block");
	/* every block mentioned is defined */
	for (uint32_t i = 0; i < p->block_records.entries.n; i++)
		if (!p->block_records.entries.v[i].state)
			vir_parse_error_at(p, p->block_records.entries.v[i].line, p->block_records.entries.v[i].column, "undefined block %.*s", (int)p->block_records.entries.v[i].name_length,
				p->block_records.entries.v[i].string);
	cid = calloc((size_t)p->constant_definitions.n + 1, 4);
	if (!cid)
		abort();
	/* constants written as instructions: in text order, not unified */
	for (uint32_t i = 0; i < p->constant_definitions.n; i++) {
		vir_parse_constant_definition *d = &p->constant_definitions.v[i];
		const vir_parse_operand_record *o = &p->operands.v[d->operand];

		cid[i] = vir_parse_constant_make(p, o, d->insn.type, true);
		if (d->insn.op != f->insns[cid[i]].op)
			vir_parse_error_at(p, o->line, o->column, "a constant of this kind is written %s",
				vir_opcode_table[f->insns[cid[i]].op].name);
	}
	/* the constants operands need, with the type their position gives (11.5) */
	for (uint32_t i = 0; i < p->fixups.n; i++) {
		vir_parse_fixup *x = &p->fixups.v[i];
		const vir_parse_operand_record *o;
		uint32_t ty, k, op, ity;

		VIR_PARSE_VECTOR_PUSH(pc, 0);
		if (x->kind != VIR_PARSE_FIXUP_VAL)
			continue;
		o = &p->operands.v[x->operand];
		if (o->kind == VIR_PARSE_OPERAND_KIND_NAME) {
			vir_parse_operand_type(p, o);	/* reports an undefined name */
			continue;
		}
		k = x->kind_code;
		op = p->blocks.v[k].op;
		ity = p->blocks.v[k].type;
		ty = o->type == 0xff ? 0 : o->type;
		if (!ty && x->mode == VIR_PARSE_LIST_MODE_POS) {
			ty = vir_operand_type_fixed(op, ity, x->field);
			if (!ty && (op == VIR_OP_ICMP || op == VIR_OP_FCMP)) {	/* the other operand fixes it */
				for (uint32_t j = 0; j < p->fixups.n; j++) {
					vir_parse_fixup *y = &p->fixups.v[j];

					if (j != i && y->kind == VIR_PARSE_FIXUP_VAL && y->kind_code == k && y->mode == VIR_PARSE_LIST_MODE_POS && y->field != x->field)
						ty = vir_parse_operand_type(p, &p->operands.v[y->operand]);
				}
			}
		} else if (!ty && x->mode == VIR_PARSE_LIST_MODE_BR) {
			uint32_t tb = p->block_records.entries.v[x->auxiliary].id;

			if (x->index < p->parsed_block.v[tb].parameter_count)
				ty = p->blocks.v[p->parsed_block.v[tb].first + x->index].type;
		}
		if (!ty && (o->kind == VIR_PARSE_OPERAND_KIND_SYM || o->kind == VIR_PARSE_OPERAND_KIND_SLOT || o->kind == VIR_PARSE_OPERAND_KIND_LABEL))
			ty = VIR_TYPE_PTR;	/* an address is a pointer, whatever the position */
		pc.v[i] = vir_parse_constant_make(p, o, ty, false);
	}
	nconst = f->insn_count;
	/* the values: constants first, then the block instructions in text order */
	for (uint32_t i = 0; i < p->fixups.n; i++) {
		vir_parse_fixup *x = &p->fixups.v[i];
		uint32_t v;
		uint32_t *dst;

		if (x->kind == VIR_PARSE_FIXUP_VAL) {
			const vir_parse_operand_record *o = &p->operands.v[x->operand];

			if (o->kind == VIR_PARSE_OPERAND_KIND_NAME) {
				const vir_parse_name_entry *e = &p->values.entries.v[o->id];

				v = e->state == 1 ? nconst + e->id : cid[e->id];
			} else {
				v = pc.v[i];
			}
		} else {
			v = p->block_records.entries.v[x->operand].id;
		}
		if (x->field == 3) {
			dst = &f->extra[x->position];
		} else {
			vir_insn *in = &p->blocks.v[x->kind_code];

			dst = x->field == 0 ? &in->a : x->field == 1 ? &in->b : &in->c;
		}
		*dst = v;
	}
	VIR_PARSE_VECTOR_FREE(pc);
	free(cid);
	/* br: the arguments are the target's parameters, exactly (the format holds no count) */
	for (uint32_t k = 0; k < p->blocks.n; k++) {
		const vir_insn *in = &p->blocks.v[k];
		const vir_parse_position *ps_ = &p->block_parameter_count.v[k];

		if (in->op == VIR_OP_BR && in->a < nb && ps_->count != p->parsed_block.v[in->a].parameter_count)
			vir_parse_error_at(p, ps_->line, ps_->column, "br passes %u arguments, block b%u takes %u", ps_->count, in->a,
				p->parsed_block.v[in->a].parameter_count);
		if (in->op == VIR_OP_SWITCH) {	/* the case list, now that the operand's type is known */
			uint32_t ty = in->a < nconst ? f->insns[in->a].type : p->blocks.v[in->a - nconst].type;
			uint32_t n = ps_->column_end - ps_->column_start, st = ty == VIR_TYPE_I64 ? 5 : 3;
			uint32_t *w;

			if (ty != VIR_TYPE_I32 && ty != VIR_TYPE_I64)
				vir_parse_error_at(p, ps_->line, ps_->column, "a switch operand is i32 or i64");
			w = malloc(((size_t)n * st + 1) * 4);
			if (!w)
				abort();
			w[0] = n;
			for (uint32_t i = 0; i < n; i++) {
				const vir_parse_switch_case *c = &p->switch_cases.v[ps_->column_start + i];
				uint32_t *q = &w[1 + (size_t)i * st];

				if (ty == VIR_TYPE_I32) {
					if ((int64_t)c->low < INT32_MIN || (int64_t)c->high < INT32_MIN ||
					    (c->low > UINT32_MAX && (int64_t)c->low >= 0) || (c->high > UINT32_MAX && (int64_t)c->high >= 0)) {
						free(w);
						vir_parse_error_at(p, c->line, c->column, "case value out of range for i32");
					}
					q[0] = (uint32_t)c->low;
					q[1] = (uint32_t)c->high;
				} else {
					q[0] = (uint32_t)c->low;
					q[1] = (uint32_t)(c->low >> 32);
					q[2] = (uint32_t)c->high;
					q[3] = (uint32_t)(c->high >> 32);
				}
				q[st - 1] = p->block_records.entries.v[c->block].id;
			}
			p->blocks.v[k].c = vir_extra_add(f, w, n * st + 1);
			free(w);
		}
	}
	if (vir_is_failed() || nconst + p->blocks.n > VIR_MAX_INS)
		vir_parse_error_at(p, close->line, close->column, "function too large");
	if (p->blocks.n)
		memcpy(&f->insns[nconst], p->blocks.v, (size_t)p->blocks.n * sizeof(vir_insn));
	f->insn_count = nconst + p->blocks.n;
	f->constant_count = nconst;
	for (uint32_t i = 0; i < nb; i++) {
		vir_block_add(f, 0);
		f->blocks[i].first = nconst + p->parsed_block.v[i].first;
		f->blocks[i].insn_count = p->parsed_block.v[i].insn_count;
		f->blocks[i].parameter_count = (uint16_t)p->parsed_block.v[i].parameter_count;
		f->blocks[i].flags = (uint16_t)p->parsed_block.v[i].flags;
	}
	if (vir_is_failed())
		vir_parse_error_at(p, close->line, close->column, "function too large");
}

static uint32_t vir_parse_function_signature(vir_parse_state *p, const vir_parse_token *t)
{
	bool isnew;
	uint32_t e, si;

	if (t->kind != VIR_PARSE_TOKEN_SIG)
		VIR_PARSE_ERROR(p, t, "expected the function's signature (%%name)");
	e = vir_parse_name_get(&p->signature_name, t->string, t->len, &isnew);
	if (isnew || !p->signature_name.entries.v[e].state)
		VIR_PARSE_ERROR(p, t, "undeclared signature %%%.*s", (int)t->len, t->string);
	si = p->signature_name.entries.v[e].id;
	if (p->signature_offset.v[si] == ~0u)
		p->signature_offset.v[si] = vir_extra_add(&p->function, &p->unit->signature_words[p->unit->signatures[si].offset], p->unit->signatures[si].count);
	return si;
}

static void vir_parse_function(vir_parse_state *p)
{
	vir_parse_token *st, *gt;
	uint32_t fl = 0, sym, si;
	bool slots_open = true;

	vir_parse_token_next(p);
	st = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_SYM, "a symbol (@name)");
	vir_parse_symbol_define(p, st, VIR_UNIT_ITEM_FUNC);
	sym = vir_parse_symbol_reference(p, st);
	gt = vir_parse_token_current(p);
	if (gt->kind != VIR_PARSE_TOKEN_SIG)
		VIR_PARSE_ERROR(p, gt, "expected the function's signature (%%name)");
	p->position++;
	for (;;) {
		vir_parse_token *t = vir_parse_token_current(p);

		if (vir_parse_token_is_identifier(t, "nofp")) fl |= VIR_FUNCTION_FLAG_NOFP;
		else if (vir_parse_token_is_identifier(t, "fenv")) fl |= VIR_FUNCTION_FLAG_FENV;
		else if (vir_parse_token_is_identifier(t, "nullinvalid")) fl |= VIR_FUNCTION_FLAG_NULLINVALID;
		else if (vir_parse_token_is_identifier(t, "noreturn")) fl |= VIR_FUNCTION_FLAG_NORETURN;
		else if (vir_parse_token_is_identifier(t, "local")) fl |= VIR_FUNCTION_FLAG_LOCAL;
		else if (vir_parse_token_is_identifier(t, "exposure")) fl |= VIR_FUNCTION_FLAG_EXPOSURE;
		else break;
		p->position++;
	}
	vir_parse_punctuation_expect(p, '{');
	vir_parse_newline_expect(p);
	vir_function_begin(&p->function, p->unit->target, sym, fl);
	p->function.version = p->unit->version;
	vir_parse_function_reset(p);
	p->in_function = true;
	si = vir_parse_function_signature(p, gt);
	p->function.signature = p->signature_offset.v[si];
	if (VIR_SIGNATURE_IS_VARIADIC(p->unit->signature_words[p->unit->signatures[si].offset]))
		p->function.flags |= VIR_FUNCTION_FLAG_VARIADIC;
	for (;;) {
		vir_parse_token *t = vir_parse_token_current(p);

		if (t->kind == VIR_PARSE_TOKEN_NL) {
			p->position++;
			continue;
		}
		if (t->kind == VIR_PARSE_TOKEN_EOF)
			VIR_PARSE_ERROR(p, t, "the function is not closed (`}` expected)");
		if (vir_parse_token_is_punctuation(t, '}')) {
			p->position++;
			vir_parse_function_finish(p, t);
			vir_parse_newline_expect(p);
			break;
		}
		if (vir_parse_token_is_identifier(t, "slot")) {
			vir_parse_token *nt, *at;
			uint64_t size, al, sfl = 0;
			int64_t hint = INT64_MIN;
			bool isnew;
			uint32_t e;

			if (!slots_open)
				VIR_PARSE_ERROR(p, t, "slots are declared before the first block");
			p->position++;
			nt = vir_parse_token_kind_expect(p, VIR_PARSE_TOKEN_SLOT, "a slot ($N)");
			e = vir_parse_name_get(&p->slot_names, nt->string, nt->len, &isnew);
			if (!isnew)
				VIR_PARSE_ERROR(p, nt, "slot $%.*s declared twice", (int)nt->len, nt->string);
			vir_parse_keyword_expect(p, "size");
			size = vir_parse_integer_expect(p, false, NULL);
			vir_parse_keyword_expect(p, "align");
			at = vir_parse_token_current(p);
			al = vir_parse_integer_expect(p, false, NULL);
			if (al > UINT32_MAX)
				VIR_PARSE_ERROR(p, at, "alignment too large");
			for (;;) {
				if (vir_parse_token_is_identifier(vir_parse_token_current(p), "hint")) {
					bool ng;

					p->position++;
					hint = (int64_t)vir_parse_integer_expect(p, true, &ng);
				} else if (vir_parse_token_is_identifier(vir_parse_token_current(p), "param")) {
					p->position++;
					sfl |= VIR_SLOT_FLAG_PARAM;
				} else if (vir_parse_token_is_identifier(vir_parse_token_current(p), "exposed")) {
					p->position++;
					sfl |= VIR_SLOT_FLAG_EXPOSED;
				} else {
					break;
				}
			}
			vir_parse_newline_expect(p);
			p->slot_names.entries.v[e].state = 1;
			p->slot_names.entries.v[e].id = vir_slot_add(&p->function, size, (uint32_t)al, (uint32_t)sfl, hint);
			if (vir_is_failed())
				VIR_PARSE_ERROR(p, nt, "too many slots");
			continue;
		}
		{	/* a block header, or an instruction (with its result) */
			vir_parse_token *t1 = vir_parse_token_peek(p, 1), *t2 = vir_parse_token_peek(p, 2), *t3 = vir_parse_token_peek(p, 3);
			bool hdr = t->kind == VIR_PARSE_TOKEN_ID && (vir_parse_token_is_punctuation(t1, '(') || vir_parse_token_is_identifier(t1, "loop") || vir_parse_token_is_identifier(t1, "addr") || vir_parse_token_is_identifier(t1, "cold") ||
						    (vir_parse_token_is_punctuation(t1, ':') && (t2->kind == VIR_PARSE_TOKEN_NL || t2->kind == VIR_PARSE_TOKEN_EOF)));

			if (hdr && t->kind == VIR_PARSE_TOKEN_ID && !vir_parse_token_is_punctuation(t1, '(')) {
				/* `b1 loop:` or `b1:` */
			}
			slots_open = false;
			if (hdr) {
				vir_parse_block_header(p);
				continue;
			}
			if (t->kind == VIR_PARSE_TOKEN_ID && vir_parse_token_is_punctuation(t1, ':') && vir_parse_token_type(t2) && vir_parse_token_is_punctuation(t3, '=')) {	/* vN:T = op ... */
				uint32_t ty = vir_parse_token_type(t2);
				vir_parse_token *vt = t;
				uint32_t before = p->blocks.n, cb = p->constant_definitions.n;

				vir_parse_value_name_check(p, vt);
				p->position += 4;
				if (vir_parse_token_current(p)->kind != VIR_PARSE_TOKEN_ID)
					VIR_PARSE_ERROR(p, vir_parse_token_current(p), "expected an instruction");
				vir_parse_insn(p, ty);
				if (p->constant_definitions.n != cb) {	/* a named constant */
					p->constant_definitions.v[cb].insn.type = (uint8_t)ty;
					p->constant_definitions.v[cb].name = vt - p->tokens.v;
					vir_parse_value_define(p, vt, 2, cb);
				} else if (p->blocks.n != before) {
					vir_parse_value_define(p, vt, 1, before);
				}
				continue;
			}
			if (t->kind != VIR_PARSE_TOKEN_ID)
				VIR_PARSE_ERROR(p, t, "expected a block, an instruction or `}`");
			if (vir_parse_token_is_punctuation(t1, ':') && t2->kind == VIR_PARSE_TOKEN_ID && vir_parse_token_is_punctuation(t3, '=') && !vir_parse_token_type(t2))
				VIR_PARSE_ERROR(p, t2, "expected a type (i32, i64, f32, f64, ptr, multi)");
			if (!vir_opcode_find(t->string, (size_t)(memchr(t->string, '.', t->len) ? (const char *)memchr(t->string, '.', t->len) - t->string : t->len)) &&
			    t->string[0] == 'b' && (t1->kind == VIR_PARSE_TOKEN_NL || t1->kind == VIR_PARSE_TOKEN_EOF))
				VIR_PARSE_ERROR(p, t1, "expected `:` after the block name");
			vir_parse_insn(p, VIR_TYPE_VOID);
			if (p->constant_definitions.n && p->constant_definitions.v[p->constant_definitions.n - 1].name == ~0u)
				VIR_PARSE_ERROR(p, t, "a constant needs a name (vN:type = ...)");
		}
	}
	p->in_function = false;
	if (!p->in_function) {
		if (p->function.block_count >= 1)
			vir_function_save(p->unit, &p->function, sym);
	}
}

static void vir_parse_state_free(vir_parse_state *p)
{
	VIR_PARSE_VECTOR_FREE(p->tokens);
	free(p->string_buffer);
	vir_parse_names_free(&p->signature_name);
	vir_parse_names_free(&p->asm_name);
	vir_parse_names_free(&p->values);
	vir_parse_names_free(&p->block_records);
	vir_parse_names_free(&p->slot_names);
	VIR_PARSE_VECTOR_FREE(p->blocks);
	VIR_PARSE_VECTOR_FREE(p->block_parameter_count);
	VIR_PARSE_VECTOR_FREE(p->parsed_block);
	VIR_PARSE_VECTOR_FREE(p->operands);
	VIR_PARSE_VECTOR_FREE(p->fixups);
	VIR_PARSE_VECTOR_FREE(p->switch_cases);
	VIR_PARSE_VECTOR_FREE(p->constant_definitions);
	VIR_PARSE_VECTOR_FREE(p->signature_offset);
	VIR_PARSE_VECTOR_FREE(p->scratch);
}

bool vir_parse(vir_unit *u, const char *src, size_t n, vir_parse_error *err)
{
	vir_parse_state p;
	vir_parse_token *t;

	memset(&p, 0, sizeof(p));
	p.source = src;
	p.unit = u;
	p.error_record = err;
	memset(err, 0, sizeof(*err));
	if (setjmp(p.join_block)) {
		vir_parse_state_free(&p);
		return false;
	}
	vir_parse_tokens_lex(&p, src, n);
	while (vir_parse_token_current(&p)->kind == VIR_PARSE_TOKEN_NL)
		p.position++;
	vir_parse_keyword_expect(&p, "vir");
	u->version = 0;
	{
		vir_parse_token *vt = vir_parse_token_current(&p);
		uint64_t v = vir_parse_integer_expect(&p, false, NULL);

		if (v > 0xffff)
			VIR_PARSE_ERROR(&p, vt, "version out of range");
		u->version = (uint32_t)v;
	}
	vir_parse_newline_expect(&p);
	vir_parse_keyword_expect(&p, "target");
	{
		vir_parse_token *a = vir_parse_token_current(&p), *b = a;
		uint32_t tg = 0;

		while (b->kind != VIR_PARSE_TOKEN_NL && b->kind != VIR_PARSE_TOKEN_EOF)
			b++;
		if (b == a)
			VIR_PARSE_ERROR(&p, a, "expected a target");
		for (uint32_t i = 1; i < VIR_TARGET_COUNT; i++) {
			const char *nm_ = vir_target_name(i);
			size_t l = (size_t)((b - 1)->string + (b - 1)->len - a->string);

			if (strlen(nm_) == l && !memcmp(nm_, a->string, l))
				tg = i;
		}
		if (!tg)
			VIR_PARSE_ERROR(&p, a, "unknown target (x86_64-linux, arm64-linux, arm64-darwin)");
		u->target = tg;
		p.position = (uint32_t)(b - p.tokens.v);
	}
	vir_parse_newline_expect(&p);
	for (;;) {
		t = vir_parse_token_current(&p);
		if (t->kind == VIR_PARSE_TOKEN_NL) {
			p.position++;
			continue;
		}
		if (t->kind == VIR_PARSE_TOKEN_EOF)
			break;
		if (t->kind != VIR_PARSE_TOKEN_ID)
			VIR_PARSE_ERROR(&p, t, "expected a declaration (sig, asm, data, tdata, extern, func)");
		if (vir_parse_token_is_identifier(t, "sig")) {
			vir_parse_signature_declaration(&p);
		} else if (vir_parse_token_is_identifier(t, "asm")) {
			vir_parse_asm_declaration(&p);
		} else if (vir_parse_token_is_identifier(t, "data") || vir_parse_token_is_identifier(t, "tdata")) {
			vir_parse_data_declaration(&p);
		} else if (vir_parse_token_is_identifier(t, "extern")) {
			vir_parse_token *s;

			p.position++;
			s = vir_parse_token_kind_expect(&p, VIR_PARSE_TOKEN_SYM, "a symbol (@name)");
			vir_parse_symbol_define(&p, s, VIR_UNIT_ITEM_EXTERN);
			vir_parse_newline_expect(&p);
		} else if (vir_parse_token_is_identifier(t, "func")) {
			vir_parse_function(&p);
		} else {
			VIR_PARSE_ERROR(&p, t, "unknown declaration `%.*s`", (int)t->len, t->string);
		}
	}
	for (uint32_t i = 0; i < u->symbol_count; i++)
		if (u->symbols[i].kind == VIR_UNIT_ITEM_UNDECL)
			vir_parse_error_at(&p, u->symbols[i].line, u->symbols[i].column, "undeclared symbol @%s", u->symbols[i].name);
	vir_parse_state_free(&p);
	return true;
}
