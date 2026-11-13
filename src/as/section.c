/* section.c: .section and .pushsection (as_directive_section), the standard sections by name, switching
 * the current frag, and the parsing of section and symbol names (quoted or plain).  .text, .data, .bss,
 * .popsection and .previous are dispatched in as.c and use the helpers here.
 *
 * A section is a frag chain in the ElfObj; the context's cur is the frag appended to and prev the one
 * .previous returns to, with a bounded stack for .pushsection (AS_SECTION_STACK_MAX).  A frag that
 * .prefalign split goes on in its continuation, so a frag taken from cur or prev goes through
 * as_fragment_latest first.  .section flags and types are checked against ELF's
 * (SHF_MERGE needs an entry size, SHF_GROUP a group) and fail the assembly when wrong. */
#include "as-internal.h"

/* ---------------------------------------------------------------- sections */

static int as_text_has_prefix(as_text n, const char *pre)
{
	size_t k = strlen(pre);
	return n.length >= k && !memcmp(n.text, pre, k) && (n.length == k || n.text[k] == '.');
}

VPRIV uint32_t as_fragment_latest(as_context *a, uint32_t frag)	/* a frag split by .prefalign goes on in its continuation */
{
	ElfFrag *first = elf_frag(a->object, frag);
	if (!first->cont) return frag;
	/* Splits append to the active continuation. Resume at the previous
	 * endpoint when a section or saved fragment is entered again. */
	frag = first->last_cont ? first->last_cont : first->cont;
	while (elf_frag(a->object, frag)->cont)
		frag = elf_frag(a->object, frag)->cont;
	first->last_cont = frag;
	return frag;
}

VPRIV void as_fragment_switch(as_context *a, uint32_t frag)
{
	frag = as_fragment_latest(a, frag);
	if (frag != a->fragment_current) { a->fragment_previous = a->fragment_current; a->dwarf_row_is_pending = 0; }	/* MC drops a pending .loc */
	a->fragment_current = frag;
}

VPRIV uint32_t as_section_standard_find(as_context *a, const char *name)
{
	as_text n = { name, (uint32_t)strlen(name) };
	uint32_t type = SHT_PROGBITS;
	uint64_t flags = 0;
	if (as_text_equals(n, ".text")) flags = SHF_ALLOC | SHF_EXECINSTR;
	else if (as_text_equals(n, ".data")) flags = SHF_ALLOC | SHF_WRITE;
	else if (as_text_equals(n, ".bss")) { flags = SHF_ALLOC | SHF_WRITE; type = SHT_NOBITS; }
	else if (as_text_equals(n, ".rodata")) flags = SHF_ALLOC;
	return elf_section(a->object, n.text, n.length, type, flags, 0, 0);
}

VPRIV int as_name_parse(as_context *a, const char **pp, const char *e, as_text *out)
{
	const char *p = as_whitespace_skip(*pp, e);
	if (p < e && *p == '"') {
		const char *s = ++p;
		while (p < e && *p != '"') p++;
		if (p >= e) return as_error_report(a, "unterminated string");
		*out = (as_text){ s, (uint32_t)(p - s) };
		*pp = p + 1;
		return 0;
	}
	const char *s = p;
	while (p < e && (as_char_is_identifier((unsigned char)*p) || *p == '-')) p++;
	if (p == s) return as_error_report(a, "expected identifier");
	*out = (as_text){ s, (uint32_t)(p - s) };
	*pp = p;
	return 0;
}


/* .section name [, "flags" [, @type [, entsize]]] ; push = .pushsection */
VPRIV int as_directive_section(as_context *a, const char *p, const char *e, int push)
{
	as_text name;
	if (as_name_parse(a, &p, e, &name)) return -1;
	uint32_t type = 0; uint64_t flags = 0; uint32_t entsize = 0, group = 0, linksec = 0, linksym = 0;
	int have_flags = 0, comdat = 0;
	int64_t sub = 0;
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') {
		p = as_whitespace_skip(p + 1, e);
		if (push && p < e && *p != '"') {	/* .pushsection name, subsection */
			if (as_expression_absolute_parse(a, &p, e, &sub)) return -1;
			p = as_whitespace_skip(p, e);
			if (p < e && *p == ',') p = as_whitespace_skip(p + 1, e);
		}
		if (p < e && *p == '"') {
			have_flags = 1;
			for (p++; p < e && *p != '"'; p++) {
				switch (*p) {
				case 'a': flags |= SHF_ALLOC; break;
				case 'w': flags |= SHF_WRITE; break;
				case 'x': flags |= SHF_EXECINSTR; break;
				case 'M': flags |= SHF_MERGE; break;
				case 'S': flags |= SHF_STRINGS; break;
				case 'T': flags |= SHF_TLS; break;
				case 'o': flags |= SHF_LINK_ORDER; break;
				case 'e': flags |= SHF_EXCLUDE; break;
				case 'R': flags |= 0x200000; break;	/* SHF_GNU_RETAIN */
				case 'G': flags |= SHF_GROUP; break;
				default: return as_error_report(a, "unknown flag '%c' in .section", *p);
				}
			}
			p++;
			p = as_whitespace_skip(p, e);
			if (p < e && *p == ',') {
				p = as_whitespace_skip(p + 1, e);
				if (p < e && (*p == '@' || *p == '%')) {
					p++;
					const char *s = p;
					while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
					as_text t = { s, (uint32_t)(p - s) };
					if (as_text_equals(t, "progbits")) type = SHT_PROGBITS;
					else if (as_text_equals(t, "nobits")) type = SHT_NOBITS;
					else if (as_text_equals(t, "note")) type = SHT_NOTE;
					else if (as_text_equals(t, "init_array")) type = SHT_INIT_ARRAY;
					else if (as_text_equals(t, "fini_array")) type = SHT_FINI_ARRAY;
					else if (as_text_equals(t, "preinit_array")) type = SHT_PREINIT_ARRAY;
					else return as_error_report(a, "unknown section type");
					p = as_whitespace_skip(p, e);
					if ((flags & SHF_MERGE) && p < e && *p == ',') {
						p++;
						int64_t es;
						if (as_expression_absolute_parse(a, &p, e, &es)) return -1;
						entsize = (uint32_t)es;
						p = as_whitespace_skip(p, e);
					}
					if ((flags & SHF_LINK_ORDER) && p < e && *p == ',') {
						/* ,sym: the section it follows is sym's */
						p++;
						as_text ls;
						if (as_name_parse(a, &p, e, &ls)) return -1;
						uint32_t ly = elf_sym_lookup(a->object, ls.text, (uint32_t)ls.length);
						if (ly && (elf_symp(a->object, ly)->flags & ESYM_DEFINED) && !(elf_symp(a->object, ly)->flags & ESYM_ABS))
							linksec = elf_frag(a->object, elf_symp(a->object, ly)->frag)->sec, linksym = ly;
						p = as_whitespace_skip(p, e);
					}
					if ((flags & SHF_GROUP) && p < e && *p == ',') {
						p++;
						as_text g;
						if (as_name_parse(a, &p, e, &g)) return -1;
						group = as_symbol_intern(a, g.text, g.length);
						p = as_whitespace_skip(p, e);
						if (p < e && *p == ',') {
							as_text c = as_text_trim(p + 1, e);
							if (!as_text_equals(c, "comdat")) return as_error_report(a, "expected 'comdat'");
							comdat = 1;
						}
					}
				}
			}
		}
	}
	if ((flags & SHF_MERGE) && !entsize) return as_error_report(a, "entry size not specified");
	if (!type) {
		if (name.length >= 5 && !memcmp(name.text, ".note", 5)) type = SHT_NOTE;
		else if (as_text_has_prefix(name, ".init_array")) type = SHT_INIT_ARRAY;
		else if (as_text_has_prefix(name, ".bss") || as_text_has_prefix(name, ".tbss")) type = SHT_NOBITS;
		else if (as_text_has_prefix(name, ".fini_array")) type = SHT_FINI_ARRAY;
		else if (as_text_has_prefix(name, ".preinit_array")) type = SHT_PREINIT_ARRAY;
		else type = SHT_PROGBITS;
	}
	if (!have_flags) {
		if (as_text_has_prefix(name, ".rodata") || as_text_equals(name, ".rodata1")) flags = SHF_ALLOC;
		else if (as_text_equals(name, ".fini") || as_text_equals(name, ".init") || as_text_has_prefix(name, ".text")) flags = SHF_ALLOC | SHF_EXECINSTR;
		else if (as_text_has_prefix(name, ".data") || as_text_equals(name, ".data1") || as_text_has_prefix(name, ".bss") ||
			 as_text_has_prefix(name, ".init_array") || as_text_has_prefix(name, ".fini_array") || as_text_has_prefix(name, ".preinit_array"))
			flags = SHF_ALLOC | SHF_WRITE;
		else if (as_text_has_prefix(name, ".tdata") || as_text_has_prefix(name, ".tbss")) flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
	}
	if (push) {
		if (a->section_stack_count >= AS_SECTION_STACK_MAX) return as_error_report(a, "section stack overflow");
		a->section_stack[a->section_stack_count].cur = a->fragment_current; a->section_stack[a->section_stack_count].prev = a->fragment_previous; a->section_stack_count++;
	}
	if ((flags & SHF_GROUP) && !group) return as_error_report(a, "expected group name");
	uint32_t s = linksec ? elf_section_lo(a->object, name.text, name.length, type, flags, entsize, group, linksec, linksym)
			     : elf_section(a->object, name.text, name.length, type, flags, entsize, group);
	if (group) { elf_reg(a->object, group); elf_sec(a->object, s)->comdat = (uint8_t)comdat; }
	{	/* MC: a code section is at least 4-aligned once switched to */
		ElfSec *se = elf_sec(a->object, s);
		if ((se->flags & SHF_EXECINSTR) && se->align < 2 && a->object->machine != EM_X86_64) se->align = 2;
	}
	as_fragment_switch(a, elf_subsection(a->object, s, (int32_t)sub));
	return 0;
}
