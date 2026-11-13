/* dwarf-line-table.c: the DWARF line table: .file N, .loc and the .debug_line section (MC's MCDwarfLineTable, version 5). */
#include "as-internal.h"

/* ---------------------------------------------------------------- DWARF lines */

static int as_dwarf_number_parse(as_context *a, const char **pp, const char *e, uint64_t *v)
{
	const char *p = as_whitespace_skip(*pp, e);
	if (p >= e || !as_char_is_digit((unsigned char)*p)) return as_error_report(a, "expected an integer");
	uint64_t x = 0;
	if (p + 1 < e && p[0] == '0' && (p[1] | 0x20) == 'x') {
		p += 2;
		while (p < e && as_char_is_hex_digit((unsigned char)*p)) { x = x * 16 + (uint64_t)(as_char_is_digit((unsigned char)*p) ? *p - '0' : as_char_to_lower((unsigned char)*p) - 'a' + 10); p++; }
	} else while (p < e && as_char_is_digit((unsigned char)*p)) x = x * 10 + (uint64_t)(*p++ - '0');
	*pp = p; *v = x;
	return 0;
}

static uint32_t as_dwarf_directory_find_from(as_context *a, as_text d, uint32_t from)
{
	for (uint32_t i = from; i < a->dwarf_directory_count; i++) if (a->dwarf_directories[i].n == d.n && !memcmp(a->dwarf_directories[i].s, d.s, d.n)) return i;
	if (a->dwarf_directory_count == a->dwarf_directory_capacity) {
		uint32_t nc = a->dwarf_directory_capacity ? a->dwarf_directory_capacity * 2 : 16;
		as_text *t = as_region_alloc(&a->definitions, sizeof *t * nc);
		if (a->dwarf_directory_count) memcpy(t, a->dwarf_directories, sizeof *t * a->dwarf_directory_count);
		a->dwarf_directories = t; a->dwarf_directory_capacity = nc;
	}
	a->dwarf_directories[a->dwarf_directory_count] = d;
	return a->dwarf_directory_count++;
}

static uint32_t as_dwarf_directory_find(as_context *a, as_text d) { return as_dwarf_directory_find_from(a, d, 0); }

/* .file N ["dir"] "name" [md5 0x...] */
VPRIV int as_directive_file(as_context *a, const char *p, const char *e)
{
	uint64_t n;
	if (as_dwarf_number_parse(a, &p, e, &n)) return -1;
	if (n > 100000) return as_error_report(a, "file number too large");
	char *b1 = as_region_alloc(&a->definitions, (size_t)(e - p) + 1), *b2 = as_region_alloc(&a->definitions, (size_t)(e - p) + 1);
	size_t l1, l2 = 0;
	if (as_string_parse(a, &p, e, b1, &l1)) return -1;
	b1[l1] = 0;
	p = as_whitespace_skip(p, e);
	int two = p < e && *p == '"';
	if (two) { if (as_string_parse(a, &p, e, b2, &l2)) return -1; b2[l2] = 0; }
	as_text dir = two ? (as_text){ b1, (uint32_t)l1 } : (as_text){ "", 0 }, name = two ? (as_text){ b2, (uint32_t)l2 } : (as_text){ b1, (uint32_t)l1 };
	uint8_t md5[16]; int hasmd5 = 0;
	for (;;) {
		p = as_whitespace_skip(p, e);
		if (p >= e) break;
		as_text w = as_text_first_word(p, e);
		if (w.n == 3 && !memcmp(w.s, "md5", 3)) {
			p = as_whitespace_skip(w.s + 3, e);
			if (p + 2 > e || p[0] != '0' || (p[1] | 0x20) != 'x') return as_error_report(a, "MD5 checksum specified, but no file number");
			p += 2;
			const char *s = p;
			while (p < e && as_char_is_hex_digit((unsigned char)*p)) p++;
			if (p - s != 32) return as_error_report(a, "invalid MD5 checksum specified");
			for (int i = 0; i < 16; i++) {
				int h = as_char_is_digit((unsigned char)s[2 * i]) ? s[2 * i] - '0' : as_char_to_lower((unsigned char)s[2 * i]) - 'a' + 10;
				int l = as_char_is_digit((unsigned char)s[2 * i + 1]) ? s[2 * i + 1] - '0' : as_char_to_lower((unsigned char)s[2 * i + 1]) - 'a' + 10;
				md5[i] = (uint8_t)(h << 4 | l);
			}
			hasmd5 = 1;
		} else return as_error_report(a, "unexpected token in '.file' directive");
	}
	/* MC: a file named as the root file (file 0) is the root: N stays unassigned */
	if (n && a->dwarf_file_count && a->dwarf_files[0].is_set && !hasmd5 && !a->dwarf_files[0].has_md5 && name.n == a->dwarf_files[0].name.n &&
	    !memcmp(name.s, a->dwarf_files[0].name.s, name.n))
		return 0;
	if (n >= a->dwarf_file_capacity) {
		uint32_t nc = a->dwarf_file_capacity ? a->dwarf_file_capacity : 16;
		while (nc <= n) nc *= 2;
		struct as_dwarf_file *t = as_region_alloc(&a->definitions, sizeof *t * nc);
		memset(t, 0, sizeof *t * nc);
		if (a->dwarf_file_count) memcpy(t, a->dwarf_files, sizeof *t * a->dwarf_file_count);
		a->dwarf_files = t; a->dwarf_file_capacity = nc;
	}
	if (n >= a->dwarf_file_count) a->dwarf_file_count = (uint32_t)n + 1;
	struct as_dwarf_file *f = &a->dwarf_files[n];
	int split = 0;
	if (!two && n) {	/* "sub/y.h": directory "sub", name "y.h" (MC's tryGetFile) */
		const char *sl = 0;
		for (uint32_t i = 0; i < name.n; i++) if (name.s[i] == '/') sl = name.s + i;
		if (sl && sl + 1 < name.s + name.n && sl > name.s) {
			dir = (as_text){ name.s, (uint32_t)(sl - name.s) };
			name = (as_text){ sl + 1, (uint32_t)(name.s + name.n - sl - 1) };
			split = 1;
		}
	}
	if (!a->dwarf_directory_count) {	/* directory 0: the root file's, else the compilation directory */
		as_text d = dir;
		if (n) {
			char *cwd = as_region_alloc(&a->definitions, 4096);
			if (!getcwd(cwd, 4096)) cwd[0] = 0;
			d = (as_text){ cwd, (uint32_t)strlen(cwd) };
		}
		as_dwarf_directory_find(a, d);
	}
	if (n == 0) a->dwarf_directories[0] = dir;
	f->name = name; f->is_set = 1; f->has_md5 = (uint8_t)hasmd5;
	if (hasmd5) memcpy(f->md5, md5, 16);
	/* (a directory split from the name goes in MC's list of the others: never entry 0) */
	f->dir = n == 0 || !dir.n ? 0 : split && a->o->machine == EM_X86_64 ? as_dwarf_directory_find_from(a, dir, 1) : as_dwarf_directory_find(a, dir);
	if (!two && n && f->dir == 0 && dir.n) f->dir = as_dwarf_directory_find(a, dir);
	a->dwarf_table_is_due = 1;
	return 0;
}

/* .loc file line [col] [basic_block|prologue_end|epilogue_begin|is_stmt N|isa N|discriminator N|view V] */
VPRIV int as_directive_loc(as_context *a, const char *p, const char *e)
{
	uint64_t fi, ln, col = 0;
	if (as_dwarf_number_parse(a, &p, e, &fi)) return -1;
	if (fi >= a->dwarf_file_count || !a->dwarf_files[fi].is_set) return as_error_report(a, "unassigned file number in '.loc' directive");
	if (as_dwarf_number_parse(a, &p, e, &ln)) return -1;
	p = as_whitespace_skip(p, e);
	if (p < e && as_char_is_digit((unsigned char)*p) && as_dwarf_number_parse(a, &p, e, &col)) return -1;
	if (a->dwarf_row_is_pending) as_dwarf_row_emit(a);
	struct as_dwarf_row *l = &a->dwarf_row_pending;
	l->file = (uint32_t)fi; l->line = (uint32_t)ln; l->col = (uint32_t)col;
	l->flags &= 1; l->isa = 0; l->discriminator = 0;
	for (;;) {
		p = as_whitespace_skip(p, e);
		if (p >= e) break;
		as_text w = as_text_first_word(p, e);
		p = w.s + w.n;
		uint64_t v;
#define AS_DWARF_DIRECTIVE_IS(lit) (w.n == sizeof lit - 1 && !memcmp(w.s, lit, w.n))
		if (AS_DWARF_DIRECTIVE_IS("basic_block")) l->flags |= 2;
		else if (AS_DWARF_DIRECTIVE_IS("prologue_end")) l->flags |= 4;
		else if (AS_DWARF_DIRECTIVE_IS("epilogue_begin")) l->flags |= 8;
		else if (AS_DWARF_DIRECTIVE_IS("is_stmt")) { if (as_dwarf_number_parse(a, &p, e, &v)) return -1; if (v > 1) return as_error_report(a, "is_stmt value not 0 or 1"); l->flags = (uint8_t)((l->flags & ~1) | v); }
		else if (AS_DWARF_DIRECTIVE_IS("isa")) { if (as_dwarf_number_parse(a, &p, e, &v)) return -1; l->isa = (uint32_t)v; }
		else if (AS_DWARF_DIRECTIVE_IS("discriminator")) { if (as_dwarf_number_parse(a, &p, e, &v)) return -1; l->discriminator = (uint32_t)v; }
		else if (AS_DWARF_DIRECTIVE_IS("view")) { p = as_whitespace_skip(p, e); while (p < e && *p != ' ' && *p != '\t') p++; }
		else return as_error_report(a, "unknown sub-directive in '.loc' directive");
#undef LW
	}
	a->dwarf_row_is_pending = 1;
	return 0;
}

/* the pending .loc becomes a row at the current position */
VPRIV void as_dwarf_row_emit(as_context *a)
{
	a->dwarf_row_is_pending = 0;
	if (a->dwarf_row_count == a->dwarf_row_capacity) {
		uint32_t nc = a->dwarf_row_capacity ? a->dwarf_row_capacity * 2 : 256;
		struct as_dwarf_row *t = as_region_alloc(&a->definitions, sizeof *t * nc);
		if (a->dwarf_row_count) memcpy(t, a->dwarf_rows, sizeof *t * a->dwarf_row_count);
		a->dwarf_rows = t; a->dwarf_row_capacity = nc;
	}
	struct as_dwarf_row *l = &a->dwarf_rows[a->dwarf_row_count++];
	*l = a->dwarf_row_pending;
	l->frag = a->cur; l->off = elf_frag(a->o, a->cur)->size;
}

/* MCDwarfLineAddr::encode with DWARF v5's line_base -5, line_range 14, opcode_base 13 */
static void as_dwarf_advance_encode(as_byte_span *b, int64_t ld, uint64_t ad, int end)
{
	enum { BASE = -5, RANGE = 14, OPB = 13, MAXSP = (255 - OPB) / RANGE };
	if (end) {
		if (ad == MAXSP) b->p[b->n++] = 8;
		else if (ad) { b->p[b->n++] = 2; as_leb128_unsigned_append(b, ad); }
		b->p[b->n++] = 0; b->p[b->n++] = 1; b->p[b->n++] = 1;
		return;
	}
	int copy = 0;
	uint64_t t = (uint64_t)(ld - BASE);
	if (t >= RANGE) { b->p[b->n++] = 3; as_leb128_signed_append(b, ld); ld = 0; t = (uint64_t)-BASE; copy = 1; }
	if (!ld && !ad) { b->p[b->n++] = 1; return; }
	t += OPB;
	if (ad < 256 + MAXSP) {
		uint64_t op = t + ad * RANGE;
		if (op <= 255) { b->p[b->n++] = (uint8_t)op; return; }
		op = t + (ad - MAXSP) * RANGE;
		if (op <= 255) { b->p[b->n++] = 8; b->p[b->n++] = (uint8_t)op; return; }
	}
	b->p[b->n++] = 2; as_leb128_unsigned_append(b, ad);
	if (copy) b->p[b->n++] = 1;
	else b->p[b->n++] = (uint8_t)t;
}

static uint32_t as_dwarf_string_add(ElfObj *o, uint32_t ls, as_text s)
{
	/* .debug_line_str: deduplicated, in order of first use */
	ElfFrag *f = elf_frag(o, ls);
	uint32_t off = 0;
	for (ElfChunk *c = f->head; c; c = c->next) {
		for (uint32_t i = 0; i < c->used;) {
			size_t k = strlen((const char *)c->data + i);
			if (k == s.n && !memcmp(c->data + i, s.s, s.n)) return off + i;
			i += (uint32_t)k + 1;
		}
		off += c->used;
	}
	off = (uint32_t)f->size;
	uint8_t *q = elf_grow(o, ls, s.n + 1);
	memcpy(q, s.s, s.n); q[s.n] = 0;
	return off;
}

VPRIV void as_dwarf_line_finish(as_context *a)
{
	ElfObj *o = a->o;
	if (a->dwarf_row_is_pending) a->dwarf_row_is_pending = 0;
	if (!a->dwarf_file_count || !a->dwarf_files[0].is_set) {
		if (a->dwarf_file_count > 1 && a->dwarf_files[1].is_set) {	/* no root file: file 1 is (MC) */
			if (!a->dwarf_file_count) a->dwarf_file_count = 1;
			a->dwarf_files[0] = a->dwarf_files[1];
			a->dwarf_files[0].dir = 0;
		} else { as_error_report(a, "no root file for the line table"); return; }
	}
	for (uint32_t i = 1; i < a->dwarf_file_count; i++)
		if (!a->dwarf_files[i].is_set) {
			char m[64];
			snprintf(m, sizeof m, "unassigned file number: %u for .file directives", i);
			as_error_report(a, "%s", m);
			return;
		}
	uint32_t sec = elf_section(o, ".debug_line", 11, SHT_PROGBITS, 0, 0, 0);
	elf_data(o, sec);
	uint32_t ls = elf_section(o, ".debug_line_str", 15, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1, 0);
	elf_data(o, ls);
	int md5 = 1;
	for (uint32_t i = 0; i < a->dwarf_file_count; i++) if (a->dwarf_files[i].is_set && !a->dwarf_files[i].has_md5) md5 = 0;
	size_t cap = 256 + 32 * (size_t)(a->dwarf_file_count + a->dwarf_directory_count) + 48 * (size_t)a->dwarf_row_count;
	as_byte_span b = { as_region_alloc(&a->definitions, cap), 0 };
	uint32_t *sfix = as_region_alloc(&a->definitions, sizeof(uint32_t) * 2 * (a->dwarf_file_count + a->dwarf_directory_count + 1));
	uint32_t nsfix = 0;
	b.n = 4;
	b.p[b.n++] = 5; b.p[b.n++] = 0; b.p[b.n++] = 8; b.p[b.n++] = 0;
	size_t hl = b.n; b.n += 4;
	static const uint8_t std[] = { 1, 1, 1, 0xfb, 14, 13, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };
	memcpy(b.p + b.n, std, sizeof std); b.n += sizeof std;
	b.p[b.n++] = 1; b.p[b.n++] = 1; b.p[b.n++] = 0x1f;
	as_leb128_unsigned_append(&b, a->dwarf_directory_count);
	for (uint32_t i = 0; i < a->dwarf_directory_count; i++) {
		sfix[2 * nsfix] = (uint32_t)b.n; sfix[2 * nsfix + 1] = as_dwarf_string_add(o, ls, a->dwarf_directories[i]); nsfix++;
		memset(b.p + b.n, 0, 4); b.n += 4;
	}
	b.p[b.n++] = (uint8_t)(2 + md5);
	b.p[b.n++] = 1; b.p[b.n++] = 0x1f; b.p[b.n++] = 2; b.p[b.n++] = 0x0f;
	if (md5) { b.p[b.n++] = 5; b.p[b.n++] = 0x1e; }
	as_leb128_unsigned_append(&b, a->dwarf_file_count);
	for (uint32_t i = 0; i < a->dwarf_file_count; i++) {
		struct as_dwarf_file *f = &a->dwarf_files[i];
		sfix[2 * nsfix] = (uint32_t)b.n; sfix[2 * nsfix + 1] = as_dwarf_string_add(o, ls, f->name); nsfix++;
		memset(b.p + b.n, 0, 4); b.n += 4;
		as_leb128_unsigned_append(&b, f->dir);
		if (md5) { memcpy(b.p + b.n, f->md5, 16); b.n += 16; }
	}
	uint32_t hlen = (uint32_t)(b.n - hl - 4);
	memcpy(b.p + hl, &hlen, 4);
	/* sequences: one per section, in order of first row */
	as_fragment_offsets_cache(o);
	uint32_t *afix = as_region_alloc(&a->definitions, sizeof(uint32_t) * 2 * (a->dwarf_row_count + 1));
	uint32_t nafix = 0;
	uint8_t *done = as_region_alloc(&a->definitions, a->dwarf_row_count + 1);
	memset(done, 0, a->dwarf_row_count + 1);
	for (uint32_t s = 0; s < a->dwarf_row_count; s++) {
		if (done[s]) continue;
		uint32_t sc = elf_frag(o, a->dwarf_rows[s].frag)->sec;
		uint32_t file = 1, line = 1, col = 0, disc = 0, isa = 0, flags = 1;
		uint64_t last = 0; int first = 1;
		for (uint32_t i = s; i < a->dwarf_row_count; i++) {
			struct as_dwarf_row *l = &a->dwarf_rows[i];
			if (elf_frag(o, l->frag)->sec != sc) continue;
			uint64_t at = elf_frag(o, l->frag)->base + l->off;
			done[i] = 1;
			if (b.n + 64 > cap) { as_error_report(a, "line table overflow"); return; }
			int64_t ld = (int64_t)l->line - line;
			if (file != l->file) { file = l->file; b.p[b.n++] = 4; as_leb128_unsigned_append(&b, file); }
			if (col != l->col) { col = l->col; b.p[b.n++] = 5; as_leb128_unsigned_append(&b, col); }
			if (disc != l->discriminator) {
				disc = l->discriminator;
				as_byte_span t = { b.p + b.n + 2, 0 }; as_leb128_unsigned_append(&t, disc);
				b.p[b.n++] = 0; b.p[b.n++] = (uint8_t)(t.n + 1);
				memmove(b.p + b.n + 1, b.p + b.n, t.n); b.p[b.n] = 4; b.n += t.n + 1;
			}
			if (isa != l->isa) { isa = l->isa; b.p[b.n++] = 12; as_leb128_unsigned_append(&b, isa); }
			if ((l->flags ^ flags) & 1) { flags = l->flags; b.p[b.n++] = 6; }
			if (l->flags & 2) b.p[b.n++] = 7;
			if (l->flags & 4) b.p[b.n++] = 10;
			if (l->flags & 8) b.p[b.n++] = 11;
			if (first) {
				b.p[b.n++] = 0; b.p[b.n++] = 9; b.p[b.n++] = 2;
				afix[2 * nafix] = (uint32_t)b.n; afix[2 * nafix + 1] = i; nafix++;
				memset(b.p + b.n, 0, 8); b.n += 8;
				as_dwarf_advance_encode(&b, ld, 0, 0);
				first = 0;
			} else as_dwarf_advance_encode(&b, ld, at - last, 0);
			disc = 0;
			line = l->line; last = at;
		}
		uint64_t send = 0;
		for (uint32_t f = sc; f; f = elf_frag(o, f)->next) send += elf_frag(o, f)->size;
		as_dwarf_advance_encode(&b, 0, send - last, 1);
	}
	uint32_t len = (uint32_t)(b.n - 4);
	memcpy(b.p, &len, 4);
	uint64_t at = elf_frag(o, sec)->size;
	uint8_t *q = elf_grow(o, sec, b.n);
	memcpy(q, b.p, b.n);
	uint32_t lsym = elf_sec(o, elf_frag(o, ls)->sec)->sym;
	for (uint32_t i = 0; i < nsfix; i++)
		elf_fixup(o, sec, at + sfix[2 * i], q + sfix[2 * i], o->machine == EM_X86_64 ? R_X86_64_32 : R_AARCH64_ABS32,
			  (as_value){ lsym, 0, (int64_t)sfix[2 * i + 1] });
	for (uint32_t i = 0; i < nafix; i++) {
		struct as_dwarf_row *l = &a->dwarf_rows[afix[2 * i + 1]];
		uint32_t y = elf_sym_anon(o, ".Ltmp", 5);
		elf_symp(o, y)->flags |= ESYM_TEMP;
		elf_define(o, y, l->frag, l->off);
		elf_fixup(o, sec, at + afix[2 * i], q + afix[2 * i], o->machine == EM_X86_64 ? R_X86_64_64 : R_AARCH64_ABS64, (as_value){ y, 0, 0 });
	}
}
