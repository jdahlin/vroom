/* section.c: .section, .text, .data and the standard sections, and the parsing of section and symbol names. */
#include "asimpl.h"

/* ---------------------------------------------------------------- sections */

static int hasprefix(Sv n, const char *pre)
{
	size_t k = strlen(pre);
	return n.n >= k && !memcmp(n.s, pre, k) && (n.n == k || n.s[k] == '.');
}

VPRIV uint32_t latest(As *a, uint32_t frag)	/* a frag split by .prefalign goes on in its continuation */
{
	while (elf_frag(a->o, frag)->cont) frag = elf_frag(a->o, frag)->cont;
	return frag;
}

VPRIV void switch_to(As *a, uint32_t frag)
{
	frag = latest(a, frag);
	if (frag != a->cur) { a->prev = a->cur; a->dwlocp = 0; }	/* MC drops a pending .loc */
	a->cur = frag;
}

VPRIV uint32_t std_section(As *a, const char *name)
{
	Sv n = { name, (uint32_t)strlen(name) };
	uint32_t type = SHT_PROGBITS;
	uint64_t flags = 0;
	if (sveq(n, ".text")) flags = SHF_ALLOC | SHF_EXECINSTR;
	else if (sveq(n, ".data")) flags = SHF_ALLOC | SHF_WRITE;
	else if (sveq(n, ".bss")) { flags = SHF_ALLOC | SHF_WRITE; type = SHT_NOBITS; }
	else if (sveq(n, ".rodata")) flags = SHF_ALLOC;
	return elf_section(a->o, n.s, n.n, type, flags, 0, 0);
}

VPRIV int parse_name(As *a, const char **pp, const char *e, Sv *out)
{
	const char *p = ws(*pp, e);
	if (p < e && *p == '"') {
		const char *s = ++p;
		while (p < e && *p != '"') p++;
		if (p >= e) return err(a, "unterminated string");
		*out = (Sv){ s, (uint32_t)(p - s) };
		*pp = p + 1;
		return 0;
	}
	const char *s = p;
	while (p < e && (isidc((unsigned char)*p) || *p == '-')) p++;
	if (p == s) return err(a, "expected identifier");
	*out = (Sv){ s, (uint32_t)(p - s) };
	*pp = p;
	return 0;
}


/* .section name [, "flags" [, @type [, entsize]]] ; push = .pushsection */
VPRIV int d_section(As *a, const char *p, const char *e, int push)
{
	Sv name;
	if (parse_name(a, &p, e, &name)) return -1;
	uint32_t type = 0; uint64_t flags = 0; uint32_t entsize = 0, group = 0, linksec = 0, linksym = 0;
	int have_flags = 0, comdat = 0;
	int64_t sub = 0;
	p = ws(p, e);
	if (p < e && *p == ',') {
		p = ws(p + 1, e);
		if (push && p < e && *p != '"') {	/* .pushsection name, subsection */
			if (abs_expr(a, &p, e, &sub)) return -1;
			p = ws(p, e);
			if (p < e && *p == ',') p = ws(p + 1, e);
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
				default: return err(a, "unknown flag '%c' in .section", *p);
				}
			}
			p++;
			p = ws(p, e);
			if (p < e && *p == ',') {
				p = ws(p + 1, e);
				if (p < e && (*p == '@' || *p == '%')) {
					p++;
					const char *s = p;
					while (p < e && isidc((unsigned char)*p)) p++;
					Sv t = { s, (uint32_t)(p - s) };
					if (sveq(t, "progbits")) type = SHT_PROGBITS;
					else if (sveq(t, "nobits")) type = SHT_NOBITS;
					else if (sveq(t, "note")) type = SHT_NOTE;
					else if (sveq(t, "init_array")) type = SHT_INIT_ARRAY;
					else if (sveq(t, "fini_array")) type = SHT_FINI_ARRAY;
					else if (sveq(t, "preinit_array")) type = SHT_PREINIT_ARRAY;
					else return err(a, "unknown section type");
					p = ws(p, e);
					if ((flags & SHF_MERGE) && p < e && *p == ',') {
						p++;
						int64_t es;
						if (abs_expr(a, &p, e, &es)) return -1;
						entsize = (uint32_t)es;
						p = ws(p, e);
					}
					if ((flags & SHF_LINK_ORDER) && p < e && *p == ',') {
						/* ,sym: the section it follows is sym's */
						p++;
						Sv ls;
						if (parse_name(a, &p, e, &ls)) return -1;
						uint32_t ly = elf_sym_lookup(a->o, ls.s, (uint32_t)ls.n);
						if (ly && (elf_symp(a->o, ly)->flags & ESYM_DEFINED) && !(elf_symp(a->o, ly)->flags & ESYM_ABS))
							linksec = elf_frag(a->o, elf_symp(a->o, ly)->frag)->sec, linksym = ly;
						p = ws(p, e);
					}
					if ((flags & SHF_GROUP) && p < e && *p == ',') {
						p++;
						Sv g;
						if (parse_name(a, &p, e, &g)) return -1;
						group = sym(a, g.s, g.n);
						p = ws(p, e);
						if (p < e && *p == ',') {
							Sv c = trim(p + 1, e);
							if (!sveq(c, "comdat")) return err(a, "expected 'comdat'");
							comdat = 1;
						}
					}
				}
			}
		}
	}
	if ((flags & SHF_MERGE) && !entsize) return err(a, "entry size not specified");
	if (!type) {
		if (name.n >= 5 && !memcmp(name.s, ".note", 5)) type = SHT_NOTE;
		else if (hasprefix(name, ".init_array")) type = SHT_INIT_ARRAY;
		else if (hasprefix(name, ".bss") || hasprefix(name, ".tbss")) type = SHT_NOBITS;
		else if (hasprefix(name, ".fini_array")) type = SHT_FINI_ARRAY;
		else if (hasprefix(name, ".preinit_array")) type = SHT_PREINIT_ARRAY;
		else type = SHT_PROGBITS;
	}
	if (!have_flags) {
		if (hasprefix(name, ".rodata") || sveq(name, ".rodata1")) flags = SHF_ALLOC;
		else if (sveq(name, ".fini") || sveq(name, ".init") || hasprefix(name, ".text")) flags = SHF_ALLOC | SHF_EXECINSTR;
		else if (hasprefix(name, ".data") || sveq(name, ".data1") || hasprefix(name, ".bss") ||
			 hasprefix(name, ".init_array") || hasprefix(name, ".fini_array") || hasprefix(name, ".preinit_array"))
			flags = SHF_ALLOC | SHF_WRITE;
		else if (hasprefix(name, ".tdata") || hasprefix(name, ".tbss")) flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
	}
	if (push) {
		if (a->nss >= MAXSECSTK) return err(a, "section stack overflow");
		a->ss[a->nss].cur = a->cur; a->ss[a->nss].prev = a->prev; a->nss++;
	}
	if ((flags & SHF_GROUP) && !group) return err(a, "expected group name");
	uint32_t s = linksec ? elf_section_lo(a->o, name.s, name.n, type, flags, entsize, group, linksec, linksym)
			     : elf_section(a->o, name.s, name.n, type, flags, entsize, group);
	if (group) { elf_reg(a->o, group); elf_sec(a->o, s)->comdat = (uint8_t)comdat; }
	{	/* MC: a code section is at least 4-aligned once switched to */
		ElfSec *se = elf_sec(a->o, s);
		if ((se->flags & SHF_EXECINSTR) && se->align < 2 && a->o->machine != EM_X86_64) se->align = 2;
	}
	switch_to(a, elf_subsection(a->o, s, (int32_t)sub));
	return 0;
}
