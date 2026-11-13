/* dwline.c: the DWARF line table: .file N, .loc and the .debug_line section (MC's MCDwarfLineTable, version 5). */
#include "asimpl.h"

/* ---------------------------------------------------------------- DWARF lines */

static int dw_num(As *a, const char **pp, const char *e, uint64_t *v)
{
	const char *p = ws(*pp, e);
	if (p >= e || !c_digit((unsigned char)*p)) return err(a, "expected an integer");
	uint64_t x = 0;
	if (p + 1 < e && p[0] == '0' && (p[1] | 0x20) == 'x') {
		p += 2;
		while (p < e && c_xdigit((unsigned char)*p)) { x = x * 16 + (uint64_t)(c_digit((unsigned char)*p) ? *p - '0' : c_lower((unsigned char)*p) - 'a' + 10); p++; }
	} else while (p < e && c_digit((unsigned char)*p)) x = x * 10 + (uint64_t)(*p++ - '0');
	*pp = p; *v = x;
	return 0;
}

static uint32_t dw_dir_from(As *a, Sv d, uint32_t from)
{
	for (uint32_t i = from; i < a->ndwd; i++) if (a->dwd[i].n == d.n && !memcmp(a->dwd[i].s, d.s, d.n)) return i;
	if (a->ndwd == a->dwdcap) {
		uint32_t nc = a->dwdcap ? a->dwdcap * 2 : 16;
		Sv *t = rget(&a->defs, sizeof *t * nc);
		if (a->ndwd) memcpy(t, a->dwd, sizeof *t * a->ndwd);
		a->dwd = t; a->dwdcap = nc;
	}
	a->dwd[a->ndwd] = d;
	return a->ndwd++;
}

static uint32_t dw_dir(As *a, Sv d) { return dw_dir_from(a, d, 0); }

/* .file N ["dir"] "name" [md5 0x...] */
VPRIV int d_dwfile(As *a, const char *p, const char *e)
{
	uint64_t n;
	if (dw_num(a, &p, e, &n)) return -1;
	if (n > 100000) return err(a, "file number too large");
	char *b1 = rget(&a->defs, (size_t)(e - p) + 1), *b2 = rget(&a->defs, (size_t)(e - p) + 1);
	size_t l1, l2 = 0;
	if (parse_string(a, &p, e, b1, &l1)) return -1;
	b1[l1] = 0;
	p = ws(p, e);
	int two = p < e && *p == '"';
	if (two) { if (parse_string(a, &p, e, b2, &l2)) return -1; b2[l2] = 0; }
	Sv dir = two ? (Sv){ b1, (uint32_t)l1 } : (Sv){ "", 0 }, name = two ? (Sv){ b2, (uint32_t)l2 } : (Sv){ b1, (uint32_t)l1 };
	uint8_t md5[16]; int hasmd5 = 0;
	for (;;) {
		p = ws(p, e);
		if (p >= e) break;
		Sv w = first_word(p, e);
		if (w.n == 3 && !memcmp(w.s, "md5", 3)) {
			p = ws(w.s + 3, e);
			if (p + 2 > e || p[0] != '0' || (p[1] | 0x20) != 'x') return err(a, "MD5 checksum specified, but no file number");
			p += 2;
			const char *s = p;
			while (p < e && c_xdigit((unsigned char)*p)) p++;
			if (p - s != 32) return err(a, "invalid MD5 checksum specified");
			for (int i = 0; i < 16; i++) {
				int h = c_digit((unsigned char)s[2 * i]) ? s[2 * i] - '0' : c_lower((unsigned char)s[2 * i]) - 'a' + 10;
				int l = c_digit((unsigned char)s[2 * i + 1]) ? s[2 * i + 1] - '0' : c_lower((unsigned char)s[2 * i + 1]) - 'a' + 10;
				md5[i] = (uint8_t)(h << 4 | l);
			}
			hasmd5 = 1;
		} else return err(a, "unexpected token in '.file' directive");
	}
	/* MC: a file named as the root file (file 0) is the root: N stays unassigned */
	if (n && a->ndwf && a->dwf[0].set && !hasmd5 && !a->dwf[0].hasmd5 && name.n == a->dwf[0].name.n &&
	    !memcmp(name.s, a->dwf[0].name.s, name.n))
		return 0;
	if (n >= a->dwfcap) {
		uint32_t nc = a->dwfcap ? a->dwfcap : 16;
		while (nc <= n) nc *= 2;
		struct DwFile *t = rget(&a->defs, sizeof *t * nc);
		memset(t, 0, sizeof *t * nc);
		if (a->ndwf) memcpy(t, a->dwf, sizeof *t * a->ndwf);
		a->dwf = t; a->dwfcap = nc;
	}
	if (n >= a->ndwf) a->ndwf = (uint32_t)n + 1;
	struct DwFile *f = &a->dwf[n];
	int split = 0;
	if (!two && n) {	/* "sub/y.h": directory "sub", name "y.h" (MC's tryGetFile) */
		const char *sl = 0;
		for (uint32_t i = 0; i < name.n; i++) if (name.s[i] == '/') sl = name.s + i;
		if (sl && sl + 1 < name.s + name.n && sl > name.s) {
			dir = (Sv){ name.s, (uint32_t)(sl - name.s) };
			name = (Sv){ sl + 1, (uint32_t)(name.s + name.n - sl - 1) };
			split = 1;
		}
	}
	if (!a->ndwd) {	/* directory 0: the root file's, else the compilation directory */
		Sv d = dir;
		if (n) {
			char *cwd = rget(&a->defs, 4096);
			if (!getcwd(cwd, 4096)) cwd[0] = 0;
			d = (Sv){ cwd, (uint32_t)strlen(cwd) };
		}
		dw_dir(a, d);
	}
	if (n == 0) a->dwd[0] = dir;
	f->name = name; f->set = 1; f->hasmd5 = (uint8_t)hasmd5;
	if (hasmd5) memcpy(f->md5, md5, 16);
	/* (a directory split from the name goes in MC's list of the others: never entry 0) */
	f->dir = n == 0 || !dir.n ? 0 : split && a->o->machine == EM_X86_64 ? dw_dir_from(a, dir, 1) : dw_dir(a, dir);
	if (!two && n && f->dir == 0 && dir.n) f->dir = dw_dir(a, dir);
	a->dwtab = 1;
	return 0;
}

/* .loc file line [col] [basic_block|prologue_end|epilogue_begin|is_stmt N|isa N|discriminator N|view V] */
VPRIV int d_loc(As *a, const char *p, const char *e)
{
	uint64_t fi, ln, col = 0;
	if (dw_num(a, &p, e, &fi)) return -1;
	if (fi >= a->ndwf || !a->dwf[fi].set) return err(a, "unassigned file number in '.loc' directive");
	if (dw_num(a, &p, e, &ln)) return -1;
	p = ws(p, e);
	if (p < e && c_digit((unsigned char)*p) && dw_num(a, &p, e, &col)) return -1;
	if (a->dwlocp) loc_row(a);
	struct DwLoc *l = &a->dwcur;
	l->file = (uint32_t)fi; l->line = (uint32_t)ln; l->col = (uint32_t)col;
	l->flags &= 1; l->isa = 0; l->disc = 0;
	for (;;) {
		p = ws(p, e);
		if (p >= e) break;
		Sv w = first_word(p, e);
		p = w.s + w.n;
		uint64_t v;
#define LW(lit) (w.n == sizeof lit - 1 && !memcmp(w.s, lit, w.n))
		if (LW("basic_block")) l->flags |= 2;
		else if (LW("prologue_end")) l->flags |= 4;
		else if (LW("epilogue_begin")) l->flags |= 8;
		else if (LW("is_stmt")) { if (dw_num(a, &p, e, &v)) return -1; if (v > 1) return err(a, "is_stmt value not 0 or 1"); l->flags = (uint8_t)((l->flags & ~1) | v); }
		else if (LW("isa")) { if (dw_num(a, &p, e, &v)) return -1; l->isa = (uint32_t)v; }
		else if (LW("discriminator")) { if (dw_num(a, &p, e, &v)) return -1; l->disc = (uint32_t)v; }
		else if (LW("view")) { p = ws(p, e); while (p < e && *p != ' ' && *p != '\t') p++; }
		else return err(a, "unknown sub-directive in '.loc' directive");
#undef LW
	}
	a->dwlocp = 1;
	return 0;
}

/* the pending .loc becomes a row at the current position */
VPRIV void loc_row(As *a)
{
	a->dwlocp = 0;
	if (a->ndwl == a->dwlcap) {
		uint32_t nc = a->dwlcap ? a->dwlcap * 2 : 256;
		struct DwLoc *t = rget(&a->defs, sizeof *t * nc);
		if (a->ndwl) memcpy(t, a->dwl, sizeof *t * a->ndwl);
		a->dwl = t; a->dwlcap = nc;
	}
	struct DwLoc *l = &a->dwl[a->ndwl++];
	*l = a->dwcur;
	l->frag = a->cur; l->off = elf_frag(a->o, a->cur)->size;
}

/* MCDwarfLineAddr::encode with DWARF v5's line_base -5, line_range 14, opcode_base 13 */
static void dw_adv(Buf8 *b, int64_t ld, uint64_t ad, int end)
{
	enum { BASE = -5, RANGE = 14, OPB = 13, MAXSP = (255 - OPB) / RANGE };
	if (end) {
		if (ad == MAXSP) b->p[b->n++] = 8;
		else if (ad) { b->p[b->n++] = 2; uleb(b, ad); }
		b->p[b->n++] = 0; b->p[b->n++] = 1; b->p[b->n++] = 1;
		return;
	}
	int copy = 0;
	uint64_t t = (uint64_t)(ld - BASE);
	if (t >= RANGE) { b->p[b->n++] = 3; sleb(b, ld); ld = 0; t = (uint64_t)-BASE; copy = 1; }
	if (!ld && !ad) { b->p[b->n++] = 1; return; }
	t += OPB;
	if (ad < 256 + MAXSP) {
		uint64_t op = t + ad * RANGE;
		if (op <= 255) { b->p[b->n++] = (uint8_t)op; return; }
		op = t + (ad - MAXSP) * RANGE;
		if (op <= 255) { b->p[b->n++] = 8; b->p[b->n++] = (uint8_t)op; return; }
	}
	b->p[b->n++] = 2; uleb(b, ad);
	if (copy) b->p[b->n++] = 1;
	else b->p[b->n++] = (uint8_t)t;
}

static uint32_t dw_str(ElfObj *o, uint32_t ls, Sv s)
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

VPRIV void dwline_finish(As *a)
{
	ElfObj *o = a->o;
	if (a->dwlocp) a->dwlocp = 0;
	if (!a->ndwf || !a->dwf[0].set) {
		if (a->ndwf > 1 && a->dwf[1].set) {	/* no root file: file 1 is (MC) */
			if (!a->ndwf) a->ndwf = 1;
			a->dwf[0] = a->dwf[1];
			a->dwf[0].dir = 0;
		} else { err(a, "no root file for the line table"); return; }
	}
	for (uint32_t i = 1; i < a->ndwf; i++)
		if (!a->dwf[i].set) {
			char m[64];
			snprintf(m, sizeof m, "unassigned file number: %u for .file directives", i);
			err(a, "%s", m);
			return;
		}
	uint32_t sec = elf_section(o, ".debug_line", 11, SHT_PROGBITS, 0, 0, 0);
	elf_data(o, sec);
	uint32_t ls = elf_section(o, ".debug_line_str", 15, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1, 0);
	elf_data(o, ls);
	int md5 = 1;
	for (uint32_t i = 0; i < a->ndwf; i++) if (a->dwf[i].set && !a->dwf[i].hasmd5) md5 = 0;
	size_t cap = 256 + 32 * (size_t)(a->ndwf + a->ndwd) + 48 * (size_t)a->ndwl;
	Buf8 b = { rget(&a->defs, cap), 0 };
	uint32_t *sfix = rget(&a->defs, sizeof(uint32_t) * 2 * (a->ndwf + a->ndwd + 1));
	uint32_t nsfix = 0;
	b.n = 4;
	b.p[b.n++] = 5; b.p[b.n++] = 0; b.p[b.n++] = 8; b.p[b.n++] = 0;
	size_t hl = b.n; b.n += 4;
	static const uint8_t std[] = { 1, 1, 1, 0xfb, 14, 13, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };
	memcpy(b.p + b.n, std, sizeof std); b.n += sizeof std;
	b.p[b.n++] = 1; b.p[b.n++] = 1; b.p[b.n++] = 0x1f;
	uleb(&b, a->ndwd);
	for (uint32_t i = 0; i < a->ndwd; i++) {
		sfix[2 * nsfix] = (uint32_t)b.n; sfix[2 * nsfix + 1] = dw_str(o, ls, a->dwd[i]); nsfix++;
		memset(b.p + b.n, 0, 4); b.n += 4;
	}
	b.p[b.n++] = (uint8_t)(2 + md5);
	b.p[b.n++] = 1; b.p[b.n++] = 0x1f; b.p[b.n++] = 2; b.p[b.n++] = 0x0f;
	if (md5) { b.p[b.n++] = 5; b.p[b.n++] = 0x1e; }
	uleb(&b, a->ndwf);
	for (uint32_t i = 0; i < a->ndwf; i++) {
		struct DwFile *f = &a->dwf[i];
		sfix[2 * nsfix] = (uint32_t)b.n; sfix[2 * nsfix + 1] = dw_str(o, ls, f->name); nsfix++;
		memset(b.p + b.n, 0, 4); b.n += 4;
		uleb(&b, f->dir);
		if (md5) { memcpy(b.p + b.n, f->md5, 16); b.n += 16; }
	}
	uint32_t hlen = (uint32_t)(b.n - hl - 4);
	memcpy(b.p + hl, &hlen, 4);
	/* sequences: one per section, in order of first row */
	frag_bases(o);
	uint32_t *afix = rget(&a->defs, sizeof(uint32_t) * 2 * (a->ndwl + 1));
	uint32_t nafix = 0;
	uint8_t *done = rget(&a->defs, a->ndwl + 1);
	memset(done, 0, a->ndwl + 1);
	for (uint32_t s = 0; s < a->ndwl; s++) {
		if (done[s]) continue;
		uint32_t sc = elf_frag(o, a->dwl[s].frag)->sec;
		uint32_t file = 1, line = 1, col = 0, disc = 0, isa = 0, flags = 1;
		uint64_t last = 0; int first = 1;
		for (uint32_t i = s; i < a->ndwl; i++) {
			struct DwLoc *l = &a->dwl[i];
			if (elf_frag(o, l->frag)->sec != sc) continue;
			uint64_t at = elf_frag(o, l->frag)->base + l->off;
			done[i] = 1;
			if (b.n + 64 > cap) { err(a, "line table overflow"); return; }
			int64_t ld = (int64_t)l->line - line;
			if (file != l->file) { file = l->file; b.p[b.n++] = 4; uleb(&b, file); }
			if (col != l->col) { col = l->col; b.p[b.n++] = 5; uleb(&b, col); }
			if (disc != l->disc) {
				disc = l->disc;
				Buf8 t = { b.p + b.n + 2, 0 }; uleb(&t, disc);
				b.p[b.n++] = 0; b.p[b.n++] = (uint8_t)(t.n + 1);
				memmove(b.p + b.n + 1, b.p + b.n, t.n); b.p[b.n] = 4; b.n += t.n + 1;
			}
			if (isa != l->isa) { isa = l->isa; b.p[b.n++] = 12; uleb(&b, isa); }
			if ((l->flags ^ flags) & 1) { flags = l->flags; b.p[b.n++] = 6; }
			if (l->flags & 2) b.p[b.n++] = 7;
			if (l->flags & 4) b.p[b.n++] = 10;
			if (l->flags & 8) b.p[b.n++] = 11;
			if (first) {
				b.p[b.n++] = 0; b.p[b.n++] = 9; b.p[b.n++] = 2;
				afix[2 * nafix] = (uint32_t)b.n; afix[2 * nafix + 1] = i; nafix++;
				memset(b.p + b.n, 0, 8); b.n += 8;
				dw_adv(&b, ld, 0, 0);
				first = 0;
			} else dw_adv(&b, ld, at - last, 0);
			disc = 0;
			line = l->line; last = at;
		}
		uint64_t send = 0;
		for (uint32_t f = sc; f; f = elf_frag(o, f)->next) send += elf_frag(o, f)->size;
		dw_adv(&b, 0, send - last, 1);
	}
	uint32_t len = (uint32_t)(b.n - 4);
	memcpy(b.p, &len, 4);
	uint64_t at = elf_frag(o, sec)->size;
	uint8_t *q = elf_grow(o, sec, b.n);
	memcpy(q, b.p, b.n);
	uint32_t lsym = elf_sec(o, elf_frag(o, ls)->sec)->sym;
	for (uint32_t i = 0; i < nsfix; i++)
		elf_fixup(o, sec, at + sfix[2 * i], q + sfix[2 * i], o->machine == EM_X86_64 ? R_X86_64_32 : R_AARCH64_ABS32,
			  (Val){ lsym, 0, (int64_t)sfix[2 * i + 1] });
	for (uint32_t i = 0; i < nafix; i++) {
		struct DwLoc *l = &a->dwl[afix[2 * i + 1]];
		uint32_t y = elf_sym_anon(o, ".Ltmp", 5);
		elf_symp(o, y)->flags |= ESYM_TEMP;
		elf_define(o, y, l->frag, l->off);
		elf_fixup(o, sec, at + afix[2 * i], q + afix[2 * i], o->machine == EM_X86_64 ? R_X86_64_64 : R_AARCH64_ABS64, (Val){ y, 0, 0 });
	}
}
