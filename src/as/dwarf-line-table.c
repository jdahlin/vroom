/* dwarf-line-table.c: the DWARF line table: .file N and .loc directives and the .debug_line (and
 * .debug_line_str) sections they produce, MC's MCDwarfLineTable, DWARF version 5 or 4 (a->dwarf_version:
 * MinGW's default is 4, Linux's 5; a .file 0 selects 5).  Version 4 has no file 0: the directories are MC's
 * list of the others (1-based) and the strings are in the table itself.
 *
 * A .loc does not emit anything: it sets a pending row, which the statement loop turns into a row
 * (as_dwarf_row_emit) at the position of the next instruction.  The section is encoded once, from
 * as_end (as_dwarf_line_finish) after relaxation, because row addresses are frag offsets.  File 0 is the root file; when the source has no
 * explicit .file 0 the first .file's entry stands in, as MC does.  The output must stay byte for byte
 * equal to clang's: bench/vcc-dwarf.py compares the rows.
 *
 * -g on an assembler source without .file N (MC's GenDwarfForAssembly, as_gen_dwarf_*): the main file
 * is file 1 (DWARF 5: the root, with the MD5 of the source), each instruction in one of its sections
 * gets a row (the line of the outermost statement, a cpp line marker's file and line when one was
 * seen), each non-temporary label there a DW_TAG_label, and the end adds .debug_info, .debug_abbrev,
 * .debug_aranges and (several sections, DWARF 3 and later) .debug_ranges or .debug_rnglists.  Its
 * sections are .text and, on ELF, every allocated executable one named by .section. */
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
	for (uint32_t i = from; i < a->dwarf_directory_count; i++) if (a->dwarf_directories[i].length == d.length && !memcmp(a->dwarf_directories[i].text, d.text, d.length)) return i;
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
	if (a->gen_dwarf) {	/* MC: the source's own debug information; its implicit file table goes (the rows stay) */
		a->gen_dwarf = 0;
		a->dwarf_file_count = a->dwarf_directory_count = 0;
	}
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
		if (w.length == 3 && !memcmp(w.text, "md5", 3)) {
			p = as_whitespace_skip(w.text + 3, e);
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
	if (n == 0 && a->dwarf_version < 5) a->dwarf_version = 5;	/* (MC: .file 0 is DWARF 5's) */
	int v5 = a->dwarf_version >= 5;
	/* MC: a file named as the root file (file 0) is the root: N stays unassigned */
	if (v5 && n && a->dwarf_file_count && a->dwarf_files[0].is_set && !hasmd5 && !a->dwarf_files[0].has_md5 && name.length == a->dwarf_files[0].name.length &&
	    !memcmp(name.text, a->dwarf_files[0].name.text, name.length))
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
	/* below version 5 with -g (cc1as -debug-info-kind): MC's line table knows the compilation
	   directory, and a file in it is named without it, then split */
	if (!v5 && n && two && a->debug_directory && dir.length == strlen(a->debug_directory) && !memcmp(dir.text, a->debug_directory, dir.length))
		two = 0, dir = (as_text){ "", 0 };
	if (!two && n) {	/* "sub/y.h": directory "sub", name "y.h" (MC's tryGetFile) */
		const char *sl = 0;
		for (uint32_t i = 0; i < name.length; i++) if (name.text[i] == '/') sl = name.text + i;
		if (sl && sl + 1 < name.text + name.length && (sl > name.text || !v5)) {
			dir = (as_text){ name.text, sl > name.text ? (uint32_t)(sl - name.text) : 1 };	/* ("/x.h": "/") */
			name = (as_text){ sl + 1, (uint32_t)(name.text + name.length - sl - 1) };
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
	/* (version 4: the compilation directory is no entry of MC's list, a directory equal to it gets one) */
	f->directory = n == 0 || !dir.length ? 0 : (split && a->object->machine == EM_X86_64) || !v5 ? as_dwarf_directory_find_from(a, dir, 1) : as_dwarf_directory_find(a, dir);
	if (!two && n && f->directory == 0 && dir.length) f->directory = as_dwarf_directory_find(a, dir);
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
	l->file = (uint32_t)fi; l->line = (uint32_t)ln; l->column = (uint32_t)col;
	l->flags &= 1; l->isa = 0; l->discriminator = 0;
	for (;;) {
		p = as_whitespace_skip(p, e);
		if (p >= e) break;
		as_text w = as_text_first_word(p, e);
		p = w.text + w.length;
		uint64_t v;
#define AS_DWARF_DIRECTIVE_IS(lit) (w.length == sizeof lit - 1 && !memcmp(w.text, lit, w.length))
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
static struct as_dwarf_row *as_dwarf_row_new(as_context *a)
{
	if (a->dwarf_row_count == a->dwarf_row_capacity) {
		uint32_t nc = a->dwarf_row_capacity ? a->dwarf_row_capacity * 2 : 256;
		struct as_dwarf_row *t = as_region_alloc(&a->definitions, sizeof *t * nc);
		if (a->dwarf_row_count) memcpy(t, a->dwarf_rows, sizeof *t * a->dwarf_row_count);
		a->dwarf_rows = t; a->dwarf_row_capacity = nc;
	}
	return &a->dwarf_rows[a->dwarf_row_count++];
}

VPRIV void as_dwarf_row_emit(as_context *a)
{
	a->dwarf_row_is_pending = 0;
	struct as_dwarf_row *l = as_dwarf_row_new(a);
	*l = a->dwarf_row_pending;
	l->fragment = a->fragment_current; l->offset = elf_frag(a->object, a->fragment_current)->size;
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
			if (k == s.length && !memcmp(c->data + i, s.text, s.length)) return off + i;
			i += (uint32_t)k + 1;
		}
		off += c->used;
	}
	off = (uint32_t)f->size;
	uint8_t *q = elf_grow(o, ls, s.length + 1);
	memcpy(q, s.text, s.length); q[s.length] = 0;
	return off;
}

VPRIV void as_dwarf_line_finish(as_context *a)
{
	ElfObj *o = a->object;
	int v5 = a->dwarf_version >= 5, coff = o->format == OBJ_COFF;
	if (a->dwarf_row_is_pending) a->dwarf_row_is_pending = 0;
	if (v5 && (!a->dwarf_file_count || !a->dwarf_files[0].is_set)) {
		if (a->dwarf_file_count > 1 && a->dwarf_files[1].is_set) {	/* no root file: file 1 is (MC) */
			if (!a->dwarf_file_count) a->dwarf_file_count = 1;
			a->dwarf_files[0] = a->dwarf_files[1];
			a->dwarf_files[0].directory = 0;
		} else { as_error_report(a, "no root file for the line table"); return; }
	}
	for (uint32_t i = 1; i < a->dwarf_file_count; i++)
		if (!a->dwarf_files[i].is_set) {
			char m[64];
			snprintf(m, sizeof m, "unassigned file number: %u for .file directives", i);
			as_error_report(a, "%s", m);
			return;
		}
	/* (COFF: MC's "dr" sections, 1-aligned) */
	enum { COFF_DEBUG = COFF_SCN_INIT_DATA | COFF_SCN_READ | COFF_SCN_DISCARDABLE };
	int declared = coff && elf_find_section(o, ".debug_line", 11, 0);
	uint32_t sec = coff ? coff_section(o, ".debug_line", 11, COFF_DEBUG, 0, 0) : elf_section(o, ".debug_line", 11, SHT_PROGBITS, 0, 0, 0);
	if (coff && !declared) coff_section_late(o, sec);	/* (MC creates it after .pdata and .llvm_addrsig) */
	elf_data(o, sec);
	uint32_t ls = 0;
	if (v5) {
		int str_declared = coff && elf_find_section(o, ".debug_line_str", 15, 0);
		ls = coff ? coff_section(o, ".debug_line_str", 15, COFF_DEBUG, 0, 0) : elf_section(o, ".debug_line_str", 15, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1, 0);
		if (coff && !str_declared) coff_section_late(o, ls);
		elf_data(o, ls);
	}
	int md5 = v5;
	for (uint32_t i = 0; i < a->dwarf_file_count; i++) if (a->dwarf_files[i].is_set && !a->dwarf_files[i].has_md5) md5 = 0;
	size_t cap = 256 + 32 * (size_t)(a->dwarf_file_count + a->dwarf_directory_count) + 48 * (size_t)a->dwarf_row_count;
	if (!v5) {	/* the strings are in the table */
		for (uint32_t i = 1; i < a->dwarf_directory_count; i++) cap += a->dwarf_directories[i].length;
		for (uint32_t i = 1; i < a->dwarf_file_count; i++) cap += a->dwarf_files[i].name.length;
	}
	as_byte_span b = { as_region_alloc(&a->definitions, cap), 0 };
	uint32_t *sfix = as_region_alloc(&a->definitions, sizeof(uint32_t) * 2 * (a->dwarf_file_count + a->dwarf_directory_count + 1));
	uint32_t nsfix = 0;
	b.n = 4;
	b.p[b.n++] = (uint8_t)a->dwarf_version; b.p[b.n++] = 0;
	if (v5) { b.p[b.n++] = 8; b.p[b.n++] = 0; }	/* address and segment selector sizes */
	size_t hl = b.n; b.n += 4;
	static const uint8_t std[] = { 1, 1, 1, 0xfb, 14, 13, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };
	memcpy(b.p + b.n, std, sizeof std); b.n += sizeof std;
	if (a->dwarf_version < 4) { memmove(b.p + b.n - sizeof std + 1, b.p + b.n - sizeof std + 2, sizeof std - 2); b.n--; }	/* (no maximum_operations_per_instruction) */
	if (!v5) {	/* MC's emitV2FileDirTables: the directories but the compilation one, the files from 1 */
		for (uint32_t i = 1; i < a->dwarf_directory_count; i++) {
			memcpy(b.p + b.n, a->dwarf_directories[i].text, a->dwarf_directories[i].length);
			b.n += a->dwarf_directories[i].length; b.p[b.n++] = 0;
		}
		b.p[b.n++] = 0;
		for (uint32_t i = 1; i < a->dwarf_file_count; i++) {
			struct as_dwarf_file *f = &a->dwarf_files[i];
			memcpy(b.p + b.n, f->name.text, f->name.length); b.n += f->name.length; b.p[b.n++] = 0;
			as_leb128_unsigned_append(&b, f->directory);
			b.p[b.n++] = 0; b.p[b.n++] = 0;	/* modification time, length */
		}
		b.p[b.n++] = 0;
		goto program;
	}
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
		as_leb128_unsigned_append(&b, f->directory);
		if (md5) { memcpy(b.p + b.n, f->md5, 16); b.n += 16; }
	}
program:;
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
		uint32_t sc = elf_frag(o, a->dwarf_rows[s].fragment)->sec;
		uint32_t file = 1, line = 1, col = 0, disc = 0, isa = 0, flags = 1;
		uint64_t last = 0; int first = 1;
		for (uint32_t i = s; i < a->dwarf_row_count; i++) {
			struct as_dwarf_row *l = &a->dwarf_rows[i];
			if (elf_frag(o, l->fragment)->sec != sc) continue;
			uint64_t at = elf_frag(o, l->fragment)->base + l->offset;
			done[i] = 1;
			if (b.n + 64 > cap) { as_error_report(a, "line table overflow"); return; }
			int64_t ld = (int64_t)l->line - line;
			if (file != l->file) { file = l->file; b.p[b.n++] = 4; as_leb128_unsigned_append(&b, file); }
			if (col != l->column) { col = l->column; b.p[b.n++] = 5; as_leb128_unsigned_append(&b, col); }
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
	uint32_t lsym = v5 ? elf_sec(o, elf_frag(o, ls)->sec)->sym : 0;
	for (uint32_t i = 0; i < nsfix; i++)	/* (COFF: section-relative, MC's emitCOFFSecRel32) */
		elf_fixup(o, sec, at + sfix[2 * i], q + sfix[2 * i], coff ? R_VCC_COFF_SECREL32 : o->machine == EM_X86_64 ? R_X86_64_32 : R_AARCH64_ABS32,
			  (as_value){ lsym, 0, (int64_t)sfix[2 * i + 1] });
	for (uint32_t i = 0; i < nafix; i++) {
		struct as_dwarf_row *l = &a->dwarf_rows[afix[2 * i + 1]];
		uint32_t y = elf_sym_anon(o, ".Ltmp", 5);
		elf_symp(o, y)->flags |= ESYM_TEMP;
		elf_define(o, y, l->fragment, l->offset);
		elf_fixup(o, sec, at + afix[2 * i], q + afix[2 * i], o->machine == EM_X86_64 ? R_X86_64_64 : R_AARCH64_ABS64, (as_value){ y, 0, 0 });
	}
}

/* ---------------------------------------------------------------- -g on assembler sources */

/* RFC 1321's MD5: the root file's checksum in a DWARF 5 line table (cc1as hashes its input) */
static void as_md5(const uint8_t *msg, size_t len, uint8_t out[16])
{
	static const uint32_t K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
		0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
		0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
		0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
		0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
		0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
	static const uint8_t R[16] = { 7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21 };
	uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
	for (size_t off = 0;; off += 64) {
		uint8_t blk[64];
		uint32_t w[16];
		int last = off + 64 >= len + 9;
		for (int i = 0; i < 64; i++) {
			size_t k = off + (size_t)i;
			blk[i] = k < len ? msg[k] : k == len ? 0x80 : 0;
		}
		if (last)	/* the last block: the length in bits */
			for (int i = 0; i < 8; i++) blk[56 + i] = (uint8_t)(((uint64_t)len * 8) >> (8 * i));
		for (int i = 0; i < 16; i++) w[i] = (uint32_t)blk[4 * i] | (uint32_t)blk[4 * i + 1] << 8 | (uint32_t)blk[4 * i + 2] << 16 | (uint32_t)blk[4 * i + 3] << 24;
		uint32_t A = h[0], B = h[1], C = h[2], D = h[3];
		for (int i = 0; i < 64; i++) {
			uint32_t F, g;
			if (i < 16) F = (B & C) | (~B & D), g = (uint32_t)i;
			else if (i < 32) F = (D & B) | (~D & C), g = (uint32_t)(5 * i + 1) & 15;
			else if (i < 48) F = B ^ C ^ D, g = (uint32_t)(3 * i + 5) & 15;
			else F = C ^ (B | ~D), g = (uint32_t)(7 * i) & 15;
			F += A + K[i] + w[g];
			A = D; D = C; C = B;
			uint8_t r = R[(i >> 4) * 4 + (i & 3)];
			B += F << r | F >> (32 - r);
		}
		h[0] += A; h[1] += B; h[2] += C; h[3] += D;
		if (last) break;
	}
	for (int i = 0; i < 16; i++) out[i] = (uint8_t)(h[i / 4] >> (8 * (i % 4)));
}

/* `.file N ["dir"] "name" [md5 0x...]` as the source would have written it, for the generated table */
static void as_gen_file_register(as_context *a, uint32_t n, const char *dir, const char *name, const uint8_t *md5)
{
	size_t dl = dir ? strlen(dir) : 0, nl = strlen(name);
	char *t = as_region_alloc(&a->definitions, 2 * (dl + nl) + 96), *q = t;
	q += sprintf(q, "%u ", n);
	for (int k = 0; k < 2; k++) {
		const char *x = k ? name : dir;
		if (!x) continue;
		*q++ = '"';
		for (; *x; x++) { if (*x == '"' || *x == '\\') *q++ = '\\'; *q++ = *x; }
		*q++ = '"'; *q++ = ' ';
	}
	if (md5) { q += sprintf(q, "md5 0x"); for (int i = 0; i < 16; i++) q += sprintf(q, "%02x", md5[i]); }
	int g = a->gen_dwarf;
	a->gen_dwarf = 0;
	as_directive_file(a, t, q);
	a->gen_dwarf = g;
}

static void as_gen_dwarf_section_push(as_context *a, uint32_t sec)
{
	for (uint32_t i = 0; i < a->gen_section_count; i++) if (a->gen_sections[i] == sec) return;
	if (a->gen_section_count == a->gen_section_capacity) {
		uint32_t nc = a->gen_section_capacity ? 2 * a->gen_section_capacity : 8;
		uint32_t *t = as_region_alloc(&a->definitions, sizeof *t * nc);
		uint8_t *c = as_region_alloc(&a->definitions, nc);
		if (a->gen_section_count) memcpy(t, a->gen_sections, sizeof *t * a->gen_section_count), memcpy(c, a->gen_section_code, a->gen_section_count);
		a->gen_sections = t; a->gen_section_code = c; a->gen_section_capacity = nc;
	}
	a->gen_section_code[a->gen_section_count] = 0;
	a->gen_sections[a->gen_section_count++] = sec;
}

/* the first snippet: the main file (DWARF 5: the root, with its MD5), .text the first section */
VPRIV void as_gen_dwarf_start(as_context *a, const char *text, size_t len)
{
	a->gen_dwarf = 1;
	const char *name = a->file_name && *a->file_name && strcmp(a->file_name, "-") ? a->file_name : "<stdin>";
	size_t dl = strlen(a->debug_directory);
	if (dl && !strncmp(name, a->debug_directory, dl)) {	/* MC's setGenDwarfRootFile: the compilation directory off the front */
		name += dl;
		if (*name == '/') name++;
	}
	a->gen_names = as_region_alloc(&a->definitions, sizeof *a->gen_names * 16);
	a->gen_name_capacity = 16;
	if (a->dwarf_version >= 5) {
		uint8_t md5[16];
		as_md5((const uint8_t *)text, len, md5);
		as_gen_file_register(a, 0, a->debug_directory, name, md5);
		a->gen_file = 0;
	} else {	/* (file 1 at the first instruction or label: MC's enabledGenDwarfForAssembly) */
		a->gen_names[0] = (struct as_gen_name){ { name, (uint32_t)strlen(name) }, 1 };
		a->gen_name_count = 1;
		a->gen_file = 1;
		a->dwarf_table_is_due = 1;
	}
	as_gen_dwarf_section_push(a, elf_frag(a->object, a->fragment_current)->sec);
}

/* MC's enabledGenDwarfForAssembly, at each instruction and label: below DWARF 5 the main file
   becomes file 1 the first time */
static int as_gen_dwarf_enabled(as_context *a)
{
	if (a->gen_dwarf != 1) return 0;
	if (a->dwarf_version < 5 && !a->dwarf_file_count) as_gen_file_register(a, 1, NULL, a->gen_names[0].name.text, NULL);
	return 1;
}

static int as_gen_dwarf_in_section(as_context *a)
{
	uint32_t sec = elf_frag(a->object, a->fragment_current)->sec;
	for (uint32_t i = 0; i < a->gen_section_count; i++) if (a->gen_sections[i] == sec) return (int)i + 1;
	return 0;
}

VPRIV void as_gen_dwarf_section(as_context *a, uint32_t sec)
{
	if (a->gen_dwarf == 1 && a->object->format != OBJ_COFF) as_gen_dwarf_section_push(a, sec);
}

/* MC's line of an instruction: the outermost macro or .rept instantiation's line in the file, else its own */
static uint32_t as_gen_dwarf_line(as_context *a)
{
	int k = a->frame_count - 1;
	while (k >= 0 && a->frames[k].kind != AS_FRAME_FILE) k--;
	if (k < 0) return a->end_line;
	return k + 1 < a->frame_count ? a->frames[k + 1].caller_line : a->frames[k].statement_line;
}

/* an instruction: a row at the outermost statement's line (a cpp marker's file: its own number) */
VPRIV int as_gen_dwarf_instruction(as_context *a)
{
	int k = as_gen_dwarf_enabled(a) ? as_gen_dwarf_in_section(a) : 0;
	if (!k) return 0;
	a->gen_section_code[k - 1] = 1;
	if (a->cpp_file) {	/* MC's emitDwarfFileDirective(0, "", name): the number of that name, else the next */
		uint32_t i = 0, n = (uint32_t)strlen(a->cpp_file);
		while (i < a->gen_name_count && !(a->gen_names[i].name.length == n && !memcmp(a->gen_names[i].name.text, a->cpp_file, n))) i++;
		if (i == a->gen_name_count) {
			if (a->gen_name_count == a->gen_name_capacity) {
				uint32_t nc = 2 * a->gen_name_capacity;
				struct as_gen_name *t = as_region_alloc(&a->definitions, sizeof *t * nc);
				memcpy(t, a->gen_names, sizeof *t * a->gen_name_count);
				a->gen_names = t; a->gen_name_capacity = nc;
			}
			uint32_t f = a->dwarf_file_count > 1 ? a->dwarf_file_count : 1;
			as_gen_file_register(a, f, NULL, a->cpp_file, NULL);
			a->gen_names[i] = (struct as_gen_name){ { a->cpp_file, n }, f };
			a->gen_name_count++;
		}
		a->gen_file = a->gen_names[i].file;
	}
	if (a->dwarf_row_is_pending) as_dwarf_row_emit(a);
	struct as_dwarf_row *l = &a->dwarf_row_pending;
	l->file = a->gen_file; l->line = as_gen_dwarf_line(a); l->column = 0; l->flags = 1; l->isa = 0; l->discriminator = 0;
	a->dwarf_row_is_pending = 1;
	a->dwarf_table_is_due = 1;
	return 1;
}

/* x86: MC parses a one-byte prefix before an instruction on its line (rex64 data16 addr32 xacquire
   xrelease, a segment) as an instruction of its own, so the rest has a row of its own one byte on */
VPRIV void as_gen_dwarf_prefix(as_context *a, as_text w, const char *e)
{
	static const char *const split[] = { "rex64", "data16", "addr32", "xacquire", "xrelease", "cs", "ds", "es", "fs", "gs", "ss" };
	int k = 0;
	for (size_t i = 0; i < sizeof split / sizeof *split && !k; i++) k = as_text_equals(w, split[i]);
	if (!k || as_whitespace_skip(w.text + w.length, e) >= e || !a->dwarf_row_count) return;
	struct as_dwarf_row *l = as_dwarf_row_new(a);
	*l = l[-1];
	l->offset++;
}

/* a label: its DW_TAG_label (MC drops a leading '_' of the name), at the source's own line */
VPRIV void as_gen_dwarf_label(as_context *a, const char *s, size_t n)
{
	if (!as_gen_dwarf_enabled(a) || (n >= 2 && s[0] == '.' && s[1] == 'L') || (n && as_char_is_digit((unsigned char)s[0])) ||
	    !as_gen_dwarf_in_section(a)) return;
	if (a->gen_label_count == a->gen_label_capacity) {
		uint32_t nc = a->gen_label_capacity ? 2 * a->gen_label_capacity : 64;
		struct as_gen_label *t = as_region_alloc(&a->definitions, sizeof *t * nc);
		if (a->gen_label_count) memcpy(t, a->gen_labels, sizeof *t * a->gen_label_count);
		a->gen_labels = t; a->gen_label_capacity = nc;
	}
	if (n && s[0] == '_') s++, n--;
	ElfObj *o = a->object;
	uint32_t y = elf_sym_anon(o, ".Ltmp", 5);
	elf_symp(o, y)->flags |= ESYM_TEMP;
	elf_define(o, y, a->fragment_current, elf_frag(o, a->fragment_current)->size);
	/* (the label's own line: in a macro's body, that body's; in the file, before any cpp marker's renumbering) */
	as_frame *top = &a->frames[a->frame_count - 1];
	uint32_t line = top->kind == AS_FRAME_FILE ? (uint32_t)((int64_t)top->statement_line + a->cpp_line_delta) : top->statement_line;
	a->gen_labels[a->gen_label_count++] = (struct as_gen_label){ as_text_duplicate(a, s, n), a->gen_file, line, y };
}

/* a temporary symbol at the end of section sec (its size after relaxation) */
static uint32_t as_gen_dwarf_section_end(ElfObj *o, uint32_t sec)
{
	uint32_t f = sec;
	while (elf_frag(o, f)->next) f = elf_frag(o, f)->next;
	uint32_t y = elf_sym_anon(o, ".Ltmp", 5);
	elf_symp(o, y)->flags |= ESYM_TEMP;
	elf_define(o, y, f, elf_frag(o, f)->size);
	return y;
}

static uint64_t as_gen_dwarf_section_size(ElfObj *o, uint32_t sec)
{
	uint64_t n = 0;
	for (uint32_t f = sec; f; f = elf_frag(o, f)->next) n += elf_frag(o, f)->size;
	return n;
}

static uint32_t as_gen_dwarf_section_new(ElfObj *o, const char *name)
{
	uint32_t len = (uint32_t)strlen(name), s;
	if (o->format == OBJ_COFF) {
		int declared = elf_find_section(o, name, len, 0) != 0;
		s = coff_section(o, name, len, COFF_SCN_INIT_DATA | COFF_SCN_READ | COFF_SCN_DISCARDABLE, 0, 0);
		if (!declared) coff_section_late(o, s);	/* (MC: after .pdata and .llvm_addrsig) */
	} else s = elf_section(o, name, len, SHT_PROGBITS, 0, 0, 0);
	return s;
}

/* the bytes of b into sec with their fixups (offset in b, type, value) */
struct as_gen_fixup { uint32_t offset, type; as_value value; };
static void as_gen_dwarf_put(ElfObj *o, uint32_t sec, const as_byte_span *b, const struct as_gen_fixup *fx, uint32_t nfx)
{
	elf_data(o, sec);	/* (arm64: its $d, in MC's order of the data) */
	uint64_t at = elf_frag(o, sec)->size;
	uint8_t *q = elf_grow(o, sec, b->n);
	memcpy(q, b->p, b->n);
	for (uint32_t i = 0; i < nfx; i++) elf_fixup(o, sec, at + fx[i].offset, q + fx[i].offset, fx[i].type, fx[i].value);
}

static void as_gen_put(as_byte_span *b, uint64_t v, int n) { for (int i = 0; i < n; i++) b->p[b->n++] = (uint8_t)(v >> (8 * i)); }
static void as_gen_string(as_byte_span *b, const char *s, size_t n) { memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n++] = 0; }

/* MCGenDwarfInfo::Emit: .debug_info, .debug_abbrev, .debug_aranges and the ranges, in MC's order */
VPRIV void as_gen_dwarf_finish(as_context *a)
{
	ElfObj *o = a->object;
	int coff = o->format == OBJ_COFF, x86 = o->machine == EM_X86_64, v = a->dwarf_version;
	uint32_t off32 = coff ? R_VCC_COFF_SECREL32 : x86 ? R_X86_64_32 : R_AARCH64_ABS32, addr64 = x86 ? R_X86_64_64 : R_AARCH64_ABS64;
	if (a->gen_dwarf != 1 || !a->dwarf_row_count) return;	/* (2: no input at all; MC: no line entries, no unit) */
	uint32_t kept = 0;	/* MC's finalizeDwarfSections: only the sections with instructions */
	for (uint32_t i = 0; i < a->gen_section_count; i++) if (a->gen_section_code[i]) a->gen_sections[kept++] = a->gen_sections[i];
	a->gen_section_count = kept;
	uint32_t info = as_gen_dwarf_section_new(o, ".debug_info"), abbrev = as_gen_dwarf_section_new(o, ".debug_abbrev");
	int ranges = a->gen_section_count > 1 && v >= 3;
	uint32_t aranges = as_gen_dwarf_section_new(o, ".debug_aranges");
	uint32_t rng = 0;
	uint32_t ns = a->gen_section_count;
	size_t name_len = 0;
	const char *producer = a->dwarf_producer ? a->dwarf_producer : "vcc", *cdir = a->debug_directory;
	/* the unit's name as MC rebuilds it: its first other directory, "/", file 1 (else the root) */
	struct as_dwarf_file *root = a->dwarf_file_count > 1 && a->dwarf_files[1].is_set ? &a->dwarf_files[1] : &a->dwarf_files[0];
	for (uint32_t i = 0; i < a->gen_label_count; i++) name_len += a->gen_labels[i].name.length;
	size_t dl = a->dwarf_directory_count > 1 ? a->dwarf_directories[1].length : 0;
	size_t cap = 256 + 64 * (size_t)ns + 32 * (size_t)a->gen_label_count + name_len + strlen(producer) + strlen(cdir) + root->name.length + dl;
	as_byte_span b = { as_region_alloc(&a->definitions, cap), 0 };
	struct as_gen_fixup *fx = as_region_alloc(&a->definitions, sizeof *fx * (8 + 2 * (size_t)ns + a->gen_label_count));
	uint32_t nfx = 0;
	/* .debug_aranges: version 2, the unit's offset, address size 8, padded to 16; (start, length) per section */
	as_gen_put(&b, 0, 4); as_gen_put(&b, 2, 2);
	fx[nfx++] = (struct as_gen_fixup){ (uint32_t)b.n, off32, { elf_sec(o, info)->sym, 0, 0 } };
	as_gen_put(&b, 0, 4); as_gen_put(&b, 8, 1); as_gen_put(&b, 0, 1); as_gen_put(&b, 0, 4);
	for (uint32_t i = 0; i < ns; i++) {
		fx[nfx++] = (struct as_gen_fixup){ (uint32_t)b.n, addr64, { elf_sec(o, a->gen_sections[i])->sym, 0, 0 } };
		as_gen_put(&b, 0, 8); as_gen_put(&b, as_gen_dwarf_section_size(o, a->gen_sections[i]), 8);
	}
	as_gen_put(&b, 0, 16);
	{ uint32_t l = (uint32_t)b.n - 4; memcpy(b.p, &l, 4); }
	as_gen_dwarf_put(o, aranges, &b, fx, nfx);
	/* .debug_ranges (a base address selection, then 0 .. length) or DWARF 5's DW_RLE_start_length */
	if (ranges) {	/* (created once .debug_aranges has its data, as in MC) */
		rng = as_gen_dwarf_section_new(o, v >= 5 ? ".debug_rnglists" : ".debug_ranges");
		b.n = 0; nfx = 0;
		if (v >= 5) { as_gen_put(&b, 0, 4); as_gen_put(&b, 5, 2); as_gen_put(&b, 8, 1); as_gen_put(&b, 0, 1); as_gen_put(&b, 0, 4); }
		for (uint32_t i = 0; i < ns; i++) {
			uint64_t size = as_gen_dwarf_section_size(o, a->gen_sections[i]);
			if (v >= 5) b.p[b.n++] = 7;
			else as_gen_put(&b, UINT64_MAX, 8);
			fx[nfx++] = (struct as_gen_fixup){ (uint32_t)b.n, addr64, { elf_sec(o, a->gen_sections[i])->sym, 0, 0 } };
			as_gen_put(&b, 0, 8);
			if (v >= 5) as_leb128_unsigned_append(&b, size);
			else { as_gen_put(&b, 0, 8); as_gen_put(&b, size, 8); }
		}
		if (v >= 5) { b.p[b.n++] = 0; uint32_t l = (uint32_t)b.n - 4; memcpy(b.p, &l, 4); }
		else as_gen_put(&b, 0, 16);
		as_gen_dwarf_put(o, rng, &b, fx, nfx);
	}
	/* .debug_abbrev: the unit (line table, pc range or ranges, name, directory, producer, language), labels */
	b.n = 0; nfx = 0;
	uint8_t sec_offset = v >= 4 ? 0x17 : 0x06;
	static const uint8_t tail[] = { 0x03, 0x08, 0x1b, 0x08, 0x25, 0x08, 0x13, 0x05, 0, 0,
					2, 0x0a, 0, 0x03, 0x08, 0x3a, 0x06, 0x3b, 0x06, 0x11, 0x01, 0, 0, 0 };
	b.p[b.n++] = 1; b.p[b.n++] = 0x11; b.p[b.n++] = 1; b.p[b.n++] = 0x10; b.p[b.n++] = sec_offset;
	if (ranges) { b.p[b.n++] = 0x55; b.p[b.n++] = sec_offset; }
	else { b.p[b.n++] = 0x11; b.p[b.n++] = 0x01; b.p[b.n++] = 0x12; b.p[b.n++] = 0x01; }
	memcpy(b.p + b.n, tail, sizeof tail); b.n += sizeof tail;
	as_gen_dwarf_put(o, abbrev, &b, NULL, 0);
	/* .debug_info */
	b.n = 0;
	as_gen_put(&b, 0, 4); as_gen_put(&b, (uint64_t)v, 2);
	if (v >= 5) { as_gen_put(&b, 1, 1); as_gen_put(&b, 8, 1); }
	fx[nfx++] = (struct as_gen_fixup){ (uint32_t)b.n, off32, { elf_sec(o, abbrev)->sym, 0, 0 } };
	as_gen_put(&b, 0, 4);
	if (v < 5) as_gen_put(&b, 8, 1);
	b.p[b.n++] = 1;
	uint32_t stmt = o->nfix + nfx;	/* (its symbol once .debug_line exists: MC creates it after these) */
	fx[nfx++] = (struct as_gen_fixup){ (uint32_t)b.n, off32, { elf_sec(o, abbrev)->sym, 0, 0 } };
	as_gen_put(&b, 0, 4);
	if (ranges) {
		fx[nfx++] = (struct as_gen_fixup){ (uint32_t)b.n, off32, { elf_sec(o, rng)->sym, 0, v >= 5 ? 12 : 0 } };
		as_gen_put(&b, 0, 4);
	} else {
		fx[nfx++] = (struct as_gen_fixup){ (uint32_t)b.n, addr64, { elf_sec(o, a->gen_sections[0])->sym, 0, 0 } };
		as_gen_put(&b, 0, 8);
		fx[nfx++] = (struct as_gen_fixup){ (uint32_t)b.n, addr64, { as_gen_dwarf_section_end(o, a->gen_sections[0]), 0, 0 } };
		as_gen_put(&b, 0, 8);
	}
	if (dl) {
		memcpy(b.p + b.n, a->dwarf_directories[1].text, dl);
		b.n += dl; b.p[b.n++] = '/';
	}
	as_gen_string(&b, root->name.text, root->name.length);
	as_gen_string(&b, cdir, strlen(cdir));
	as_gen_string(&b, producer, strlen(producer));
	as_gen_put(&b, 0x8001, 2);	/* DW_LANG_Mips_Assembler */
	for (uint32_t i = 0; i < a->gen_label_count; i++) {
		struct as_gen_label *l = &a->gen_labels[i];
		b.p[b.n++] = 2;
		as_gen_string(&b, l->name.text, l->name.length);
		as_gen_put(&b, l->file, 4); as_gen_put(&b, l->line, 4);
		fx[nfx++] = (struct as_gen_fixup){ (uint32_t)b.n, addr64, { l->symbol, 0, 0 } };
		as_gen_put(&b, 0, 8);
	}
	b.p[b.n++] = 0;
	{ uint32_t l = (uint32_t)b.n - 4; memcpy(b.p, &l, 4); }
	as_gen_dwarf_put(o, info, &b, fx, nfx);
	uint32_t line = as_gen_dwarf_section_new(o, ".debug_line");
	((ElfFix *)o->fixes.base)[stmt].v.add = elf_sec(o, line)->sym;
}
