/*
 * coff.c: the COFF relocatable object (x86-64, IMAGE_FILE_MACHINE_AMD64) from the model of obj/obj.h,
 * byte for byte as LLVM MC's WinCOFFObjectWriter writes it for x86_64-w64-mingw32.  See obj.h ("COFF")
 * for what sections and symbols mean here; unwind.c writes .xdata/.pdata into the model first.
 *
 * coff_finish writes .pdata (unwind.c) and creates .llvm_addrsig when asked, lays the model out, resolves
 * every fixup in layout order (in place, or a relocation: coff_resolve) and builds the tables; coff_emit
 * writes the file into a buffer of the returned size.  elf_finish/elf_emit call them for an OBJ_COFF
 * object, so callers write any object the same way.  Errors go to o->err and coff_finish returns 0.
 *
 * Output (as MC):
 *   file header (20) | section headers (40 each, in section-number order) |
 *   per section in creation order: its data (none for uninitialized data), its relocations (10 each;
 *   from 0xffff on, a first record holding the count, IMAGE_SCN_LNK_NRELOC_OVFL) |
 *   symbol table (18-byte records) | string table (a 4-byte size, then the names longer than 8)
 * Section numbers follow creation order, associative COMDAT sections last.  The symbol table: per
 * section its symbol with a section-definition record (size, relocation count, a JamCRC of the data,
 * the associated section, the COMDAT selection), then the section's COMDAT symbol; then every
 * registered symbol in registration order (temporaries only with .scl 3), each weak external followed
 * by its ".weak.NAME.default[.UNIQUE]" definition; then one .file symbol per .file name.  The header's
 * time stamp is 0.  Names longer than 8 are in the string table, tail-merged as StringTableBuilder does.
 */
#include "../../obj/objimpl.h"

enum {
	IMAGE_REL_AMD64_ADDR64 = 1, IMAGE_REL_AMD64_ADDR32 = 2, IMAGE_REL_AMD64_ADDR32NB = 3, IMAGE_REL_AMD64_REL32 = 4,
	IMAGE_REL_AMD64_SECTION = 10, IMAGE_REL_AMD64_SECREL = 11,
};
enum { COFF_CLASS_FILE = 103, COFF_CLASS_WEAK_EXTERNAL = 105 };
enum { COFF_ENTRY_SECTION, COFF_ENTRY_SYMBOL, COFF_ENTRY_WEAK_DEFAULT, COFF_ENTRY_FILE };
#define COFF_RELOC_SECTION 0x80000000u	/* CoffRelocation.target: a section's symbol */
#define COFF_SECTIONS_MAX 65279		/* beyond: /bigobj, not written */

typedef struct { uint32_t offset, target; uint16_t type; } CoffRelocation;

typedef struct {		/* one symbol-table record and its auxiliary records */
	uint8_t kind, aux_count, storage_class, has_definition;
	uint32_t ref;		/* the section, ElfSym or .file name */
	uint32_t link;		/* a weak external: the entry its auxiliary record names */
	uint32_t index;		/* in the symbol table */
	const char *name; uint32_t length, string_offset;
	uint32_t value; int32_t section_number; uint16_t type;
	uint32_t comdat_of;	/* the section whose COMDAT symbol this is */
} CoffEntry;

typedef struct {
	CoffRelocation *relocations; uint32_t relocation_count;
	uint32_t *relocation_first;	/* per section */
	CoffEntry *entries; uint32_t entry_count, entry_capacity;
	uint32_t *entry_of;		/* per ElfSym: entry + 1 */
	uint32_t *section_entry, *section_number, *section_order, *section_string;
	uint64_t *raw_offset, *relocation_offset;
	uint32_t symbol_count;		/* records, auxiliary ones too */
	uint8_t *strings; uint32_t string_size;
	uint64_t symbol_offset, string_offset, size;
	uint32_t *symidx; uint32_t symidx_count;	/* R_VCC_COFF_SYMIDX fixups */
	uint32_t addrsig;		/* .llvm_addrsig section */
} CoffOut;

/* ---- the per-object state (objimpl.h) ---- */

VPRIV void *coff_grow(ElfObj *o, void *old, uint32_t count, uint32_t *capacity, size_t size)
{
	if (count < *capacity) return old;
	uint32_t n = *capacity ? *capacity * 2 : 16;
	void *p = rpush(&o->arena, size * n, 8);
	if (count) memcpy(p, old, size * count);
	*capacity = n;
	return p;
}

VPRIV CoffState *coff_state(ElfObj *o)
{
	if (!o->coff) {
		o->coff = rpush(&o->arena, sizeof *o->coff, 8);
		memset(o->coff, 0, sizeof *o->coff);
		o->coff->current = -1;
	}
	return o->coff;
}

VPRIV uint32_t coff_fragment_latest(ElfObj *o, uint32_t sec)
{
	uint32_t f = sec;
	while (elf_frag(o, f)->cont) f = elf_frag(o, f)->cont;
	return f;
}

VPRIV uint32_t coff_label_here(ElfObj *o, uint32_t frag)
{
	uint32_t y = elf_sym_anon(o, "", 0);
	elf_symp(o, y)->flags |= ESYM_TEMP;
	elf_define(o, y, frag, elf_frag(o, frag)->size);
	return y;
}

uint32_t coff_section(ElfObj *o, const char *name, uint32_t len, uint32_t characteristics, uint32_t comdat_sym, int selection)
{
	uint32_t s = elf_find_section(o, name, len, comdat_sym);
	if (s) return s;
	s = elf_section(o, name, len, characteristics & COFF_SCN_UNINIT_DATA ? SHT_NOBITS : SHT_PROGBITS, characteristics, 0, comdat_sym);
	elf_sec(o, s)->comdat = (uint8_t)selection;
	return s;
}

uint32_t coff_section_unique(ElfObj *o, const char *name, uint32_t len, uint32_t characteristics, uint32_t comdat_sym, int selection,
			     uint32_t unique_id)
{
	for (uint32_t i = 1; i < o->nsec; i++) {
		ElfSec *se = elf_sec(o, i);
		if (se->unique == unique_id + 1 && se->len == len && se->group == comdat_sym && !memcmp(se->name, name, len)) return i;
	}
	uint32_t s = elf_section_create(o, name, len, characteristics & COFF_SCN_UNINIT_DATA ? SHT_NOBITS : SHT_PROGBITS, characteristics, 0, comdat_sym);
	elf_sec(o, s)->comdat = (uint8_t)selection;
	elf_sec(o, s)->unique = unique_id + 1;
	return s;
}

void coff_file_name(ElfObj *o, const char *name, uint32_t len)
{
	CoffState *c = coff_state(o);
	char *p = rpush(&o->arena, len + 1, 1);
	memcpy(p, name, len); p[len] = 0;
	c->files = coff_grow(o, c->files, c->file_count, &c->file_capacity, sizeof *c->files);
	c->files[c->file_count].name = p; c->files[c->file_count].length = len;
	c->file_count++;
}

void coff_align_common(ElfObj *o, uint32_t sym, unsigned log2_align)
{
	uint32_t s = coff_section(o, ".drectve", 8, COFF_SCN_LNK_INFO | COFF_SCN_LNK_REMOVE, 0, 0);
	uint32_t frag = coff_fragment_latest(o, s);
	ElfSym *y = elf_symp(o, sym);
	char tail[16];
	int k = snprintf(tail, sizeof tail, "\",%u", log2_align);
	elf_data(o, frag);
	elf_bytes(o, frag, " -aligncomm:\"", 13);
	elf_bytes(o, frag, y->name, y->len);
	elf_bytes(o, frag, tail, (size_t)k);
}

/* ---- fixups ---- */

static int coff_is_temporary(const ElfSym *y)
{
	return (y->flags & ESYM_TEMP) || (y->len >= 2 && y->name[0] == '.' && y->name[1] == 'L');
}

/* in the symbol table: registered, not a section's, not a temporary (unless .scl 3 put it there) */
static int coff_in_table(const ElfSym *y)
{
	if (y->flags & (ESYM_SECTION | ESYM_NOSYMTAB)) return 0;
	return !coff_is_temporary(y) || y->coff_class == COFF_CLASS_STATIC;
}

static void coff_relocation(ElfObj *o, CoffOut *w, ElfFix *x, uint64_t P, uint16_t type, ElfSym *a, uint32_t aid, int64_t c)
{
	uint32_t sec = elf_frag(o, x->frag)->sec;
	uint32_t target = aid;
	if (coff_is_temporary(a) && a->coff_class != COFF_CLASS_STATIC) {
		/* MC: a temporary becomes its section's symbol and its offset */
		if (!insec(a)) { elf_error(o, "undefined temporary symbol '%s'", a->name); return; }
		c += (int64_t)symoff(o, a);
		target = COFF_RELOC_SECTION | symsec(o, a);
	}
	if (type == IMAGE_REL_AMD64_REL32) c += 4;	/* relative to the end of the field */
	x64_patch(o, x, c);
	w->relocations[w->relocation_count++] = (CoffRelocation){ (uint32_t)P, target, type };
	elf_sec(o, sec)->nfix_reloc++;
}

static void coff_resolve(ElfObj *o, CoffOut *w, ElfFix *x)
{
	ElfFrag *f = elf_frag(o, x->frag);
	uint32_t S = f->sec;
	uint64_t P = f->base + x->off;
	uint32_t aid = x->v.add;
	ElfSym *a = aid ? elf_symp(o, aid) : NULL;
	ElfSym *b = x->v.sub ? elf_symp(o, x->v.sub) : NULL;
	int64_t c = x->v.c;
	uint32_t t = x->type;
	if (t == R_VCC_ABS8) t = x->type = R_X86_64_8;
	if (a && !(a->flags & ESYM_DEFINED) && a->alias) { aid = a->alias; a = elf_symp(o, aid); }	/* x = undefined: x is that */
	if (a && (a->flags & ESYM_ABS)) { c += (int64_t)a->value; a = NULL; }
	if (b && (b->flags & ESYM_ABS)) { c -= (int64_t)b->value; b = NULL; }
	if (t == R_VCC_COFF_SYMIDX) {
		if (!a || b) { elf_error(o, "expected a symbol in '.symidx'"); return; }
		w->symidx[w->symidx_count++] = (uint32_t)(x - (ElfFix *)o->fixes.base);
		return;
	}
	if (b) {
		if (!insec(b)) { elf_error(o, "symbol '%s' can not be undefined in a subtraction expression", b->name); return; }
		if (!a) { elf_error(o, "expected relocatable expression"); return; }
		if (insec(a) && symsec(o, a) == symsec(o, b) && obj_pcrel_resolves_locally(o, a)) {
			x64_patch(o, x, (int64_t)symoff(o, a) - (int64_t)symoff(o, b) + c);
			return;
		}
		/* a - b as REL32 (MC: also for an 8-byte field); b is taken to be in the fixup's section */
		if (t != R_X86_64_32 && t != R_X86_64_32S && t != R_X86_64_64 && t != R_X86_64_PC32) {
			elf_error(o, "Cannot represent this expression");
			return;
		}
		coff_relocation(o, w, x, P, IMAGE_REL_AMD64_REL32, a, aid, (int64_t)P - (int64_t)symoff(o, b) + c);
		return;
	}
	int pcrel = x64_pcrel(t);
	if (!a) {
		x64_patch(o, x, pcrel ? c - (int64_t)P : c);
		return;
	}
	if (pcrel && !x->pad && insec(a) && symsec(o, a) == S && obj_pcrel_resolves_locally(o, a)) {
		x64_patch(o, x, (int64_t)symoff(o, a) + c - (int64_t)P);
		return;
	}
	uint16_t type;
	switch (t) {
	case R_X86_64_PC32: case R_X86_64_PLT32: case R_X86_64_GOTPCREL: case R_X86_64_GOTPCRELX: case R_X86_64_REX_GOTPCRELX:
	case R_X86_64_GOTTPOFF: case R_X86_64_TLSGD: case R_X86_64_TLSLD: case R_X86_64_GOTPC32:
		type = IMAGE_REL_AMD64_REL32; break;
	case R_X86_64_32: case R_X86_64_32S: case R_X86_64_GOT32: case R_X86_64_TPOFF32: case R_X86_64_DTPOFF32: case R_X86_64_SIZE32:
		type = IMAGE_REL_AMD64_ADDR32; break;
	case R_X86_64_64: case R_X86_64_TPOFF64: case R_X86_64_DTPOFF64: case R_X86_64_GOTOFF64: case R_X86_64_SIZE64:
		type = IMAGE_REL_AMD64_ADDR64; break;
	case R_VCC_COFF_IMGREL32: type = IMAGE_REL_AMD64_ADDR32NB; break;
	case R_VCC_COFF_SECREL32: type = IMAGE_REL_AMD64_SECREL; break;
	case R_VCC_COFF_SECTION: type = IMAGE_REL_AMD64_SECTION; break;
	default: elf_error(o, "unsupported relocation type"); return;
	}
	coff_relocation(o, w, x, P, type, a, aid, c);
}

/* ---- symbols and strings ---- */

static uint32_t coff_entry_add(ElfObj *o, CoffOut *w, int kind, uint32_t ref)
{
	w->entries = coff_grow(o, w->entries, w->entry_count, &w->entry_capacity, sizeof *w->entries);
	CoffEntry *e = &w->entries[w->entry_count];
	memset(e, 0, sizeof *e);
	e->kind = (uint8_t)kind; e->ref = ref;
	return w->entry_count++;
}

static uint32_t coff_entry_of(ElfObj *o, CoffOut *w, uint32_t sym)
{
	if (!w->entry_of[sym]) w->entry_of[sym] = coff_entry_add(o, w, COFF_ENTRY_SYMBOL, sym) + 1;
	return w->entry_of[sym] - 1;
}

static int coff_is_external(const ElfSym *y)
{
	if (y->bind != STB_LOCAL || y->common == 1 || y->coff_weak) return 1;
	return !(y->flags & ESYM_DEFINED) && !y->alias;	/* undefined and not a variable */
}

typedef struct { const char *s; uint32_t n; uint32_t *out; } CoffString;

static int coff_string_compare(const void *pa, const void *pb)
{
	const CoffString *a = pa, *b = pb;
	for (uint32_t i = 0;; i++) {
		int ca = i < a->n ? (unsigned char)a->s[a->n - 1 - i] : -1;
		int cb = i < b->n ? (unsigned char)b->s[b->n - 1 - i] : -1;
		if (ca != cb) return ca > cb ? -1 : 1;
		if (ca < 0) return 0;
	}
}

/* the string table (StringTableBuilder, WinCOFF: offsets from 4, a suffix of the previous shares it) */
static void coff_strings(ElfObj *o, CoffOut *w, CoffString *v, uint32_t n)
{
	qsort(v, n, sizeof *v, coff_string_compare);
	uint32_t size = 4, prevoff = 0, prevn = 0;
	const char *prev = NULL;
	for (uint32_t i = 0; i < n; i++) {
		if (prev && prevn >= v[i].n && !memcmp(prev + prevn - v[i].n, v[i].s, v[i].n)) {
			*v[i].out = prevoff + prevn - v[i].n;
			continue;
		}
		*v[i].out = size; prevoff = size; prev = v[i].s; prevn = v[i].n;
		size += v[i].n + 1;
	}
	uint8_t *t = rpush(&o->arena, size, 8);
	put(t, size, 4);
	for (uint32_t i = 0; i < n; i++) { memcpy(t + *v[i].out, v[i].s, v[i].n); t[*v[i].out + v[i].n] = 0; }
	w->strings = t; w->string_size = size;
}

static void coff_symbols(ElfObj *o, CoffOut *w)
{
	uint32_t ns = o->nsec;
	CoffState *cs = o->coff;
	w->entry_of = rpush(&o->arena, sizeof(uint32_t) * o->nsym, 8);
	memset(w->entry_of, 0, sizeof(uint32_t) * o->nsym);
	/* sections, each with its COMDAT symbol */
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		uint32_t e = coff_entry_add(o, w, COFF_ENTRY_SECTION, s);
		w->section_entry[s] = e;
		if (se->group && se->comdat != COFF_SELECT_ASSOCIATIVE) {
			uint32_t k = coff_entry_of(o, w, se->group);
			if (w->entries[k].comdat_of) { elf_error(o, "two sections have the same comdat"); return; }
			w->entries[k].comdat_of = s;
		}
	}
	/* the registered symbols, in order */
	uint32_t *regs = (uint32_t *)o->regs.base, weak_defaults = 0;
	for (uint32_t r = 0; r < o->nreg; r++) {
		uint32_t id = regs[r];
		ElfSym *y = elf_symp(o, id);
		if (!coff_in_table(y)) continue;
		uint32_t e = coff_entry_of(o, w, id);
		w->entries[e].has_definition = 1;
		if (!y->coff_weak) continue;
		ElfSym *t = y->alias ? elf_symp(o, y->alias) : NULL;
		if (t && (!(t->flags & ESYM_DEFINED) || t->bind != STB_LOCAL) && coff_in_table(t)) {
			uint32_t k = coff_entry_of(o, w, y->alias);	/* an alias of an external: that symbol */
			w->entries[e].link = k + 1;
		} else {
			uint32_t k = coff_entry_add(o, w, COFF_ENTRY_WEAK_DEFAULT, id);
			w->entries[e].link = k + 1;
			weak_defaults++;
		}
	}
	/* their records */
	for (uint32_t i = 0; i < w->entry_count; i++) {
		CoffEntry *e = &w->entries[i];
		if (e->kind == COFF_ENTRY_SECTION) {
			ElfSec *se = elf_sec(o, e->ref);
			e->name = se->name; e->length = se->len;
			e->section_number = (int32_t)w->section_number[e->ref];
			e->storage_class = COFF_CLASS_STATIC; e->aux_count = 1;
			continue;
		}
		ElfSym *y = elf_symp(o, e->ref);
		int defined = insec(y), absolute = (y->flags & ESYM_ABS) != 0;
		uint32_t value = y->common == 1 && coff_is_external(y) ? (uint32_t)y->size : defined ? (uint32_t)symoff(o, y) :
				 absolute ? (uint32_t)y->value : 0;
		int32_t number = defined ? (int32_t)w->section_number[symsec(o, y)] : absolute ? -1 : 0;
		uint8_t cls = y->coff_class ? y->coff_class : coff_is_external(y) ? COFF_CLASS_EXTERNAL : COFF_CLASS_STATIC;
		if (e->kind == COFF_ENTRY_WEAK_DEFAULT) {
			char *n = rpush(&o->arena, y->len + 16, 1);
			memcpy(n, ".weak.", 6); memcpy(n + 6, y->name, y->len); memcpy(n + 6 + y->len, ".default", 9);
			e->name = n; e->length = y->len + 14;
			e->value = value; e->section_number = number ? number : -1;
			e->type = y->coff_type; e->storage_class = cls;
			continue;
		}
		e->name = y->name; e->length = y->len;
		if (y->coff_weak) {
			e->storage_class = COFF_CLASS_WEAK_EXTERNAL; e->aux_count = 1;
			continue;
		}
		if (!e->has_definition) {	/* a COMDAT symbol never registered: its name and section (MC) */
			e->section_number = (int32_t)w->section_number[e->comdat_of];
			continue;
		}
		e->value = value; e->section_number = number; e->type = y->coff_type; e->storage_class = cls;
	}
	if (weak_defaults) {	/* MC's setWeakDefaultNames: ".NAME" of the first external definition (not COMDAT, if one is) */
		CoffEntry *unique = NULL;
		for (int comdat = 0; comdat < 2 && !unique; comdat++)
			for (uint32_t i = 0; i < w->entry_count && !unique; i++) {
				CoffEntry *e = &w->entries[i];
				if (e->kind != COFF_ENTRY_SYMBOL || e->storage_class != COFF_CLASS_EXTERNAL) continue;
				if (e->section_number <= 0 && e->section_number != -1) continue;
				if (!comdat && e->section_number > 0 && (elf_sec(o, w->section_order[e->section_number])->flags & COFF_SCN_LNK_COMDAT)) continue;
				unique = e;
			}
		if (unique)
			for (uint32_t i = 0; i < w->entry_count; i++) {
				CoffEntry *e = &w->entries[i];
				if (e->kind != COFF_ENTRY_WEAK_DEFAULT) continue;
				char *n = rpush(&o->arena, e->length + unique->length + 2, 1);
				memcpy(n, e->name, e->length); n[e->length] = '.';
				memcpy(n + e->length + 1, unique->name, unique->length);
				e->name = n; e->length += unique->length + 1;
			}
	}
	for (uint32_t i = 0; cs && i < cs->file_count; i++) {
		uint32_t k = coff_entry_add(o, w, COFF_ENTRY_FILE, i);
		CoffEntry *e = &w->entries[k];
		e->name = ".file"; e->length = 5; e->section_number = -2; e->storage_class = COFF_CLASS_FILE;
		e->aux_count = (uint8_t)((cs->files[i].length + 17) / 18);
	}
	uint32_t index = 0;
	for (uint32_t i = 0; i < w->entry_count; i++) { w->entries[i].index = index; index += 1 + w->entries[i].aux_count; }
	w->symbol_count = index;
	/* the string table: long section names, then long symbol names */
	CoffString *sv = rpush(&o->arena, sizeof(CoffString) * (ns + w->entry_count + 1), 8);
	uint32_t n = 0;
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		w->section_string[s] = 0;
		if (se->len > 8) sv[n++] = (CoffString){ se->name, se->len, &w->section_string[s] };
	}
	for (uint32_t i = 0; i < w->entry_count; i++) {
		CoffEntry *e = &w->entries[i];
		if (e->length > 8) sv[n++] = (CoffString){ e->name, e->length, &e->string_offset };
	}
	coff_strings(o, w, sv, n);
}

/* ---- finish ---- */

static uint32_t coff_symbol_index(ElfObj *o, CoffOut *w, uint32_t target)
{
	if (target & COFF_RELOC_SECTION) return w->entries[w->section_entry[target & ~COFF_RELOC_SECTION]].index;
	if (!w->entry_of[target]) { elf_error(o, "relocation against '%s', which is not in the symbol table", elf_symp(o, target)->name); return 0; }
	return w->entries[w->entry_of[target] - 1].index;
}

static void coff_uleb(uint8_t *p, uint32_t *n, uint32_t v)
{
	do { uint8_t c = v & 0x7f; v >>= 7; p[(*n)++] = (uint8_t)(c | (v ? 0x80 : 0)); } while (v);
}

size_t coff_finish(ElfObj *o)
{
	CoffOut *w = rpush(&o->arena, sizeof *w, 8);
	memset(w, 0, sizeof *w);
	o->fin = w;
	if (o->machine != EM_X86_64) { elf_error(o, "COFF objects are written for x86-64 only"); return 0; }
	if (o->coff && o->coff->frame_count) coff_unwind_finish(o);
	if (o->has_err) return 0;
	if (o->has_addrsig) w->addrsig = coff_section(o, ".llvm_addrsig", 13, COFF_SCN_LNK_REMOVE, 0, 0);
	uint32_t ns = o->nsec;
	if (ns - 1 > COFF_SECTIONS_MAX) { elf_error(o, "too many sections for a COFF object (%u; /bigobj is not written)", ns - 1); return 0; }
	elf_layout(o);
	/* section numbers: associative COMDAT sections after the others (MC, for link.exe) */
	w->section_number = rpush(&o->arena, sizeof(uint32_t) * ns * 4, 8);
	w->section_order = w->section_number + ns;
	w->section_entry = w->section_number + 2 * ns;
	w->section_string = w->section_number + 3 * ns;
	uint32_t number = 1;
	for (int assoc = 0; assoc < 2; assoc++)
		for (uint32_t s = 1; s < ns; s++)
			if ((elf_sec(o, s)->comdat == COFF_SELECT_ASSOCIATIVE) == assoc) { w->section_order[number] = s; w->section_number[s] = number++; }
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		if (se->align > 13) { elf_error(o, "section '%s': alignment %llu is more than COFF's 8192", se->name, 1ull << se->align); return 0; }
		if (se->comdat == COFF_SELECT_ASSOCIATIVE && (!se->group || !insec(elf_symp(o, se->group)))) {
			elf_error(o, "cannot make section %s associative with sectionless symbol %s", se->name, se->group ? elf_symp(o, se->group)->name : "");
			return 0;
		}
		se->nfix_reloc = 0;
	}
	/* fixups, in layout order */
	w->relocations = rpush(&o->arena, sizeof(CoffRelocation) * (o->nfix + 1), 8);
	w->symidx = rpush(&o->arena, sizeof(uint32_t) * (o->nfix + 1), 8);
	ElfFix *fx = (ElfFix *)o->fixes.base;
	for (uint32_t s = 1; s < ns; s++)
		for (uint32_t f = s; f; f = elf_frag(o, f)->next)
			for (uint32_t i = elf_frag(o, f)->fix_head; i; i = fx[i].next)
				coff_resolve(o, w, &fx[i]);
	if (o->has_err) return 0;
	w->relocation_first = rpush(&o->arena, sizeof(uint32_t) * (ns + 1), 8);
	for (uint32_t s = 1, k = 0; s <= ns; s++) { w->relocation_first[s] = k; if (s < ns) k += elf_sec(o, s)->nfix_reloc; }
	coff_symbols(o, w);
	if (o->has_err) return 0;
	for (uint32_t i = 0; i < w->symidx_count; i++) {
		ElfFix *x = &fx[w->symidx[i]];
		ElfSym *a = elf_symp(o, x->v.add);
		uint32_t index = w->entry_of[x->v.add] ? w->entries[w->entry_of[x->v.add] - 1].index :
				 insec(a) ? w->entries[w->section_entry[symsec(o, a)]].index : 0;
		put(x->p, index, 4);
	}
	if (w->addrsig) {	/* the symbols' indices (a temporary: its section's), registered ones only */
		uint8_t *b = rpush(&o->arena, 5 * (size_t)o->naddrsig + 1, 1);
		uint32_t n = 0;
		for (uint32_t i = 0; i < o->naddrsig; i++) {
			ElfSym *y = elf_symp(o, o->addrsig[i]);
			if (!y->reg) continue;
			if (!coff_is_temporary(y) && w->entry_of[o->addrsig[i]]) coff_uleb(b, &n, w->entries[w->entry_of[o->addrsig[i]] - 1].index);
			else if (insec(y)) coff_uleb(b, &n, w->entries[w->section_entry[symsec(o, y)]].index);
		}
		elf_bytes(o, w->addrsig, b, n);
		elf_sec(o, w->addrsig)->size = n;
	}
	for (uint32_t i = 0; i < w->relocation_count; i++)
		w->relocations[i].target = coff_symbol_index(o, w, w->relocations[i].target);
	if (o->has_err) return 0;
	/* file offsets: each section's data, then its relocations */
	w->raw_offset = rpush(&o->arena, sizeof(uint64_t) * ns * 2, 8);
	w->relocation_offset = w->raw_offset + ns;
	uint64_t off = 20 + 40 * (uint64_t)(ns - 1);
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		w->raw_offset[s] = 0; w->relocation_offset[s] = 0;
		if (!(se->flags & COFF_SCN_UNINIT_DATA)) { w->raw_offset[s] = off; off += se->size; }
		if (se->nfix_reloc) { w->relocation_offset[s] = off; off += 10 * ((uint64_t)se->nfix_reloc + (se->nfix_reloc >= 0xffff)); }
	}
	w->symbol_offset = off; off += 18 * (uint64_t)w->symbol_count;
	w->string_offset = off; off += w->string_size;
	if (off > 0xffffffffu) { elf_error(o, "COFF object larger than 4 GB"); return 0; }
	w->size = off;
	return off;
}

/* ---- emit ---- */

static void coff_section_name(uint8_t *p, const ElfSec *se, uint32_t string_offset)
{
	if (se->len <= 8) { memcpy(p, se->name, se->len); return; }
	if (string_offset <= 9999999) { char t[9]; int k = snprintf(t, sizeof t, "/%u", string_offset); memcpy(p, t, (size_t)k); return; }
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	p[0] = p[1] = '/';
	for (int i = 0; i < 6; i++) p[7 - i] = (uint8_t)b64[(string_offset >> (6 * i)) & 63];
}

void coff_emit(ElfObj *o, uint8_t *b)
{
	CoffOut *w = o->fin;
	CoffState *cs = o->coff;
	uint32_t ns = o->nsec, crc_table[256];
	memset(b, 0, w->size);
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
		crc_table[i] = c;
	}
	put(b, 0x8664, 2); put(b + 2, ns - 1, 2); put(b + 8, w->symbol_offset, 4); put(b + 12, w->symbol_count, 4);
	uint32_t *crc = rpush(&o->arena, sizeof(uint32_t) * ns, 8);
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		uint32_t n = se->nfix_reloc, c = 0;
		crc[s] = 0;
		if (w->raw_offset[s]) {
			uint8_t *d = b + w->raw_offset[s];
			for (uint32_t f = s; f; f = elf_frag(o, f)->next) {
				ElfFrag *fr = elf_frag(o, f);
				uint8_t *q = d + fr->base;
				for (ElfChunk *ch = fr->head; ch; ch = ch->next) { memcpy(q, ch->data, ch->used); q += ch->used; }
			}
			for (uint64_t i = 0; i < se->size; i++) c = crc_table[(c ^ d[i]) & 0xff] ^ (c >> 8);	/* JamCRC from 0 (MC) */
			crc[s] = c;
		}
		uint8_t *r = b + w->relocation_offset[s];
		if (n >= 0xffff) { put(r, (uint64_t)n + 1, 4); r += 10; }
		for (uint32_t i = w->relocation_first[s]; i < w->relocation_first[s + 1]; i++, r += 10) {
			put(r, w->relocations[i].offset, 4); put(r + 4, w->relocations[i].target, 4); put(r + 8, w->relocations[i].type, 2);
		}
	}
	for (uint32_t k = 1; k < ns; k++) {	/* headers, in section-number order */
		uint32_t s = w->section_order[k], n = elf_sec(o, s)->nfix_reloc;
		ElfSec *se = elf_sec(o, s);
		uint8_t *h = b + 20 + 40 * (uint64_t)(k - 1);
		coff_section_name(h, se, w->section_string[s]);
		put(h + 16, se->size, 4); put(h + 20, w->raw_offset[s], 4); put(h + 24, w->relocation_offset[s], 4);
		put(h + 32, n >= 0xffff ? 0xffff : n, 2);
		put(h + 36, (uint32_t)se->flags | (uint32_t)(se->align + 1) << 20 | (n >= 0xffff ? 0x01000000u : 0), 4);
	}
	uint8_t *y = b + w->symbol_offset;
	for (uint32_t i = 0; i < w->entry_count; i++) {
		CoffEntry *e = &w->entries[i];
		if (e->length <= 8) memcpy(y, e->name, e->length);
		else put(y + 4, e->string_offset, 4);
		put(y + 8, e->value, 4); put(y + 12, (uint16_t)e->section_number, 2); put(y + 14, e->type, 2);
		y[16] = e->storage_class; y[17] = e->aux_count;
		y += 18;
		if (e->kind == COFF_ENTRY_SECTION) {
			ElfSec *se = elf_sec(o, e->ref);
			uint32_t n = se->nfix_reloc, number = w->section_number[e->ref];
			if (se->comdat == COFF_SELECT_ASSOCIATIVE) {
				ElfSym *k = elf_symp(o, se->group);
				number = insec(k) ? w->section_number[symsec(o, k)] : 0;
			}
			put(y, se->size, 4); put(y + 4, n >= 0xffff ? 0xffff : n, 2); put(y + 8, crc[e->ref], 4);
			put(y + 12, number, 2); y[14] = se->comdat;
		} else if (e->kind == COFF_ENTRY_SYMBOL && e->aux_count) {	/* a weak external */
			put(y, w->entries[e->link - 1].index, 4); put(y + 4, elf_symp(o, e->ref)->coff_weak, 4);
		} else if (e->kind == COFF_ENTRY_FILE) {
			memcpy(y, cs->files[e->ref].name, cs->files[e->ref].length);
		}
		y += 18 * (uint64_t)e->aux_count;
	}
	memcpy(b + w->string_offset, w->strings, w->string_size);
}
