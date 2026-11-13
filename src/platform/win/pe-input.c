/*
 * Inputs: COFF x86-64 objects, ar archives (GNU or Microsoft symbol tables)
 * and short import records, and the global symbol table with lld's
 * resolution rules (SymbolTable::add*).  Every structural check that lld
 * makes fatal is an error here that stops the link before anything reads
 * past the input; nothing in a file is trusted.
 */

typedef struct { void *base; size_t size; } pe_mapping;
static pe_mapping *pe_mappings;
static uint32_t pe_mapping_count, pe_mapping_capacity;
static pe_archive **pe_archives;
static uint32_t pe_archive_count, pe_archive_capacity;

static void pe_input_reset(void)
{
	for (uint32_t i = 0; i < pe_mapping_count; i++)
		munmap(pe_mappings[i].base, pe_mappings[i].size);
	free(pe_mappings);
	pe_mappings = NULL;
	pe_mapping_count = pe_mapping_capacity = 0;
	for (uint32_t i = 0; i < pe_archive_count; i++) {
		free(pe_archives[i]->seen);
		free(pe_archives[i]->symbols);
	}
	free(pe_archives);
	pe_archives = NULL;
	pe_archive_count = pe_archive_capacity = 0;
	free(pe_inputs);
	pe_inputs = NULL;
	pe_input_count = pe_input_capacity = 0;
	free((void *)pe_search_dirs);
	pe_search_dirs = NULL;
	pe_search_dir_count = pe_search_dir_capacity = 0;
	pe_sysroot = NULL;
}

/* the whole file, mapped read-only; NULL (with an error) when it cannot be read */
static const uint8_t *pe_file_map(const char *path, size_t *size)
{
	int fd = openat(pe_dirfd, path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		pe_error("could not open '%s': %s", path, strerror(errno));
		return NULL;
	}
	struct stat st;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
		pe_error("cannot open %s: not a regular file", path);
		close(fd);
		return NULL;
	}
	*size = (size_t)st.st_size;
	if (!*size) {
		close(fd);
		static const uint8_t empty[1];
		return empty;
	}
	void *p = mmap(NULL, *size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (p == MAP_FAILED) {
		pe_error("cannot map %s: %s", path, strerror(errno));
		return NULL;
	}
	pe_mapping m = { p, *size };
	PE_PUSH(pe_mappings, pe_mapping_count, pe_mapping_capacity, m);
	return p;
}

/* ---------------- the global symbol table ---------------- */

static void pe_symbols_init(void)
{
	pe_hash_mask = (1u << 14) - 1;
	pe_hash = calloc(pe_hash_mask + 1, sizeof *pe_hash);
	if (!pe_hash)
		pe_oom();
}

static uint32_t pe_name_hash(const char *s, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; i++)
		h = (h ^ (uint8_t)s[i]) * 16777619u;
	return h;
}

static pe_symbol *pe_sym_find_n(const char *name, size_t n)
{
	for (pe_symbol *s = pe_hash[pe_name_hash(name, n) & pe_hash_mask]; s; s = s->hash_next)
		if (s->name_len == n && !memcmp(s->name, name, n))
			return s;
	return NULL;
}

static pe_symbol *pe_sym_find(const char *name) { return pe_sym_find_n(name, strlen(name)); }

static void pe_hash_grow(void)
{
	uint32_t nmask = pe_hash_mask * 2 + 1;
	pe_symbol **nh = calloc(nmask + 1, sizeof *nh);
	if (!nh)
		pe_oom();
	for (uint32_t i = 0; i < pe_global_count; i++) {
		pe_symbol *s = pe_globals[i];
		uint32_t b = pe_name_hash(s->name, s->name_len) & nmask;
		s->hash_next = nh[b];
		nh[b] = s;
	}
	free(pe_hash);
	pe_hash = nh;
	pe_hash_mask = nmask;
}

/* lld SymbolTable::insert: the symbol, created Undefined when new */
static pe_symbol *pe_sym_insert(const char *name, size_t n, bool *inserted)
{
	pe_symbol *s = pe_sym_find_n(name, n);
	if (s) {
		*inserted = false;
		return s;
	}
	if (pe_global_count >= pe_hash_mask)
		pe_hash_grow();
	s = pe_alloc(sizeof *s);
	s->name = pe_strndup(name, n);
	s->name_len = (uint32_t)n;
	s->kind = PE_SYM_UNDEFINED;
	s->external = true;
	uint32_t b = pe_name_hash(name, n) & pe_hash_mask;
	s->hash_next = pe_hash[b];
	pe_hash[b] = s;
	PE_PUSH(pe_globals, pe_global_count, pe_global_capacity, s);
	*inserted = true;
	return s;
}

static bool pe_sym_is_defined(const pe_symbol *s)
{
	return s->kind != PE_SYM_UNDEFINED && s->kind != PE_SYM_LAZY_ARCHIVE;
}

/* replaceSymbol: the record keeps its name, links and global flags; the rest is the new definition */
static void pe_sym_replace(pe_symbol *s, uint8_t kind, pe_file *f)
{
	bool used = s->used_in_regular, root = s->gc_root, pending = s->pending_load, written = s->written;
	pe_symbol *next = s->hash_next;
	const char *name = s->name;
	uint32_t n = s->name_len;
	bool ext = s->external;
	memset(s, 0, sizeof *s);
	s->name = name;
	s->name_len = n;
	s->external = ext;
	s->hash_next = next;
	s->used_in_regular = used;
	s->gc_root = root;
	s->pending_load = pending;
	s->written = written;
	s->kind = kind;
	s->file = f;
}

static void pe_queue_member(pe_archive *a, uint32_t offset, const char *why);

/* lld forceLazy */
static void pe_force_lazy(pe_symbol *s)
{
	s->pending_load = true;
	if (s->kind == PE_SYM_LAZY_ARCHIVE)
		pe_queue_member(s->archive, s->member_offset, s->name);
}

static pe_symbol *pe_add_undefined(const char *name, size_t n, pe_file *f, bool override_lazy)
{
	bool inserted;
	pe_symbol *s = pe_sym_insert(name, n, &inserted);
	s->used_in_regular = true;
	if (inserted || (s->kind == PE_SYM_LAZY_ARCHIVE && override_lazy)) {
		pe_sym_replace(s, PE_SYM_UNDEFINED, f);
		return s;
	}
	if (s->kind == PE_SYM_LAZY_ARCHIVE && !s->pending_load)
		pe_force_lazy(s);
	return s;
}

static void pe_report_duplicate(pe_symbol *existing, pe_file *nf)
{
	const char *old = existing->file ? pe_file_name(existing->file) : "<internal>";
	if (cfg.allow_multiple)
		pe_warn("duplicate symbol: %s\n>>> defined at %s\n>>> defined at %s", existing->name, old, pe_file_name(nf));
	else
		pe_error("duplicate symbol: %s\n>>> defined at %s\n>>> defined at %s", existing->name, old, pe_file_name(nf));
}

static void pe_sym_set_coff(pe_symbol *s, const uint8_t *cs)
{
	s->storage_class = cs[16];
	s->type = pe_rd16(cs + 14);
}

static pe_symbol *pe_add_regular(pe_file *f, const char *name, size_t n, const uint8_t *cs, pe_chunk *c, uint32_t value)
{
	bool inserted;
	pe_symbol *s = pe_sym_insert(name, n, &inserted);
	s->used_in_regular = true;
	if (inserted || s->kind != PE_SYM_REGULAR) {
		pe_sym_replace(s, PE_SYM_REGULAR, f);
		pe_sym_set_coff(s, cs);
		s->chunk = c;
		s->value = value;
	} else
		pe_report_duplicate(s, f);
	return s;
}

static pe_symbol *pe_add_common(pe_file *f, const char *name, size_t n, uint64_t size, const uint8_t *cs, pe_chunk *c)
{
	bool inserted;
	pe_symbol *s = pe_sym_insert(name, n, &inserted);
	s->used_in_regular = true;
	if (inserted || (s->kind != PE_SYM_REGULAR && s->kind != PE_SYM_COMMON) ||
	    (s->kind == PE_SYM_COMMON && size > s->value)) {
		pe_sym_replace(s, PE_SYM_COMMON, f);
		pe_sym_set_coff(s, cs);
		s->chunk = c;
		s->value = size;
		c->sym = s;
		c->live = true;		/* (and stays so when a later definition wins, as in lld) */
	}
	return s;
}

/* lld addAbsolute: from an object (cs) or the linker (cs NULL) */
static pe_symbol *pe_add_absolute(const char *name, size_t n, uint64_t va, const uint8_t *cs, pe_file *f)
{
	bool inserted;
	pe_symbol *s = pe_sym_insert(name, n, &inserted);
	if (cs)
		s->used_in_regular = true;
	if (inserted || s->kind == PE_SYM_UNDEFINED || s->kind == PE_SYM_LAZY_ARCHIVE) {
		pe_sym_replace(s, PE_SYM_ABSOLUTE, f);
		s->value = va;
		if (cs)
			pe_sym_set_coff(s, cs);
	} else if (s->kind == PE_SYM_ABSOLUTE) {
		if (s->value != va)
			pe_report_duplicate(s, f);
	} else if (s->kind != PE_SYM_REGULAR && s->kind != PE_SYM_COMMON)
		pe_report_duplicate(s, f);
	return s;
}

static pe_symbol *pe_add_synthetic(const char *name, pe_chunk *c)
{
	bool inserted;
	pe_symbol *s = pe_sym_insert(name, strlen(name), &inserted);
	if (inserted || s->kind == PE_SYM_UNDEFINED || s->kind == PE_SYM_LAZY_ARCHIVE) {
		pe_sym_replace(s, PE_SYM_SYNTHETIC, NULL);
		s->chunk = c;
	} else
		pe_report_duplicate(s, NULL);
	return s;
}

static pe_symbol *pe_add_lazy(pe_archive *a, const char *name, uint32_t offset)
{
	bool inserted;
	pe_symbol *s = pe_sym_insert(name, strlen(name), &inserted);
	if (inserted) {
		pe_sym_replace(s, PE_SYM_LAZY_ARCHIVE, NULL);
		s->archive = a;
		s->member_offset = offset;
		return s;
	}
	if (s->kind != PE_SYM_UNDEFINED || s->weak_alias || s->pending_load)
		return s;
	s->pending_load = true;
	pe_queue_member(a, offset, name);
	return s;
}

/* ---------------- COFF objects ---------------- */

static const char *pe_obj_symbol_name(pe_file *f, const uint8_t *cs, size_t *len)
{
	if (pe_rd32(cs) == 0) {
		uint32_t off = pe_rd32(cs + 4);
		if (off < 4 || off >= f->strtab_size) {
			pe_error("%s: invalid symbol name offset %u", pe_file_name(f), off);
			*len = 0;
			return "";
		}
		const char *s = f->strtab + off;
		const char *e = memchr(s, 0, f->strtab_size - off);
		if (!e) {
			pe_error("%s: unterminated symbol name", pe_file_name(f));
			*len = 0;
			return "";
		}
		*len = (size_t)(e - s);
		return s;
	}
	const char *s = (const char *)cs;
	size_t n = 0;
	while (n < 8 && s[n])
		n++;
	*len = n;
	return s;
}

static const uint8_t *pe_obj_sym(pe_file *f, uint32_t i) { return f->symtab + (size_t)i * COFF_SYMBOL_SIZE; }

static void pe_file_chunk_add(pe_file *f, pe_chunk *c)
{
	PE_PUSH(f->chunks, f->nchunks, f->chunk_capacity, c);
}

static pe_chunk *pe_chunk_new(uint8_t kind)
{
	pe_chunk *c = pe_alloc(sizeof *c);
	c->kind = kind;
	c->live = true;
	c->has_data = true;
	c->align = 1;
	c->order = pe_chunk_order++;
	return c;
}

/* lld ObjFile::readSection: a chunk, or NULL when the section does not go to the output */
static pe_chunk *pe_section_read(pe_file *f, uint32_t i, bool comdat)
{
	coff_section_header *h = &f->sections[i];
	const char *name = f->secnames[i];
	if (!strcmp(name, ".drectve")) {
		if (h->size_of_raw_data) {
			f->directives = (const char *)f->data + h->pointer_to_raw_data;
			f->directives_len = h->size_of_raw_data;
		}
		return NULL;
	}
	if (!strcmp(name, ".llvm_addrsig") || !strcmp(name, ".llvm.call-graph-profile"))
		return NULL;
	if (cfg.debug < 2 && !strncmp(name, ".debug_", 7))
		return NULL;
	if (h->characteristics & IMAGE_SCN_LNK_REMOVE)
		return NULL;
	/* CodeView (.debug$S/T/P), the guard tables and .sxdata are not linked as data */
	if (!strcmp(name, ".debug") || !strncmp(name, ".debug$", 7) || !strcmp(name, ".gfids$y") ||
	    !strcmp(name, ".giats$y") || !strcmp(name, ".gljmp$y") || !strcmp(name, ".gehcont$y") || !strcmp(name, ".sxdata"))
		return NULL;
	if (!strcmp(name, ".rsrc") || !strncmp(name, ".rsrc$", 6)) {
		pe_error("%s: resource sections (.rsrc) are not supported by vlink", pe_file_name(f));
		return NULL;
	}
	pe_chunk *c = pe_chunk_new(PE_CHUNK_SECTION);
	c->file = f;
	c->name = name;
	c->section_index = i;
	c->header_chars = h->characteristics;
	c->chars = h->characteristics & (0xfe000000u | IMAGE_SCN_CNT_CODE | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_CNT_UNINITIALIZED_DATA);
	c->align = coff_section_alignment(h->characteristics);
	c->size = h->size_of_raw_data;
	c->is_comdat = comdat;
	c->is_dwarf = !strncmp(name, ".debug_", 7);
	c->has_data = !(h->characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA);
	c->data = c->has_data && c->size ? f->data + h->pointer_to_raw_data : NULL;
	/* COMDAT sections are dead until --gc-sections marks them; without it everything is live */
	c->live = !cfg.gc || !comdat;
	uint32_t nrel = h->number_of_relocations;
	const uint8_t *rel = f->data + h->pointer_to_relocations;
	if ((h->characteristics & IMAGE_SCN_LNK_NRELOC_OVFL) && nrel) {
		/* the first record holds the real count and is not itself a relocation */
		rel += COFF_RELOCATION_SIZE;
		nrel--;
	}
	c->relocs = nrel ? rel : NULL;
	c->nrelocs = nrel;
	pe_file_chunk_add(f, c);
	return c;
}

static bool pe_obj_validate(pe_file *f)
{
	const uint8_t *d = f->data;
	size_t size = f->size;
	const char *fn = pe_file_name(f);
	if (size < COFF_FILE_HEADER_SIZE) {
		pe_error("%s: file is too small to be a COFF object", fn);
		return false;
	}
	uint16_t machine = pe_rd16(d);
	if (pe_machine_name && machine == IMAGE_FILE_MACHINE_AMD64) {
		pe_error("%s: machine type x64 conflicts with %s", fn, pe_machine_name);
		return false;
	}
	if (machine != IMAGE_FILE_MACHINE_AMD64) {
		if (machine == 0 && pe_rd16(d + 2) == 0xffff)
			pe_error("%s: bigobj COFF objects are not supported by vlink", fn);
		else
			pe_error("%s: machine type %#x conflicts with x64", fn, machine);
		return false;
	}
	f->nsections = pe_rd16(d + 2);
	uint32_t symptr = pe_rd32(d + 8);
	f->nsyms = pe_rd32(d + 12);
	uint16_t opt = pe_rd16(d + 16);
	uint64_t shoff = COFF_FILE_HEADER_SIZE + (uint64_t)opt;
	if (shoff + (uint64_t)f->nsections * COFF_SECTION_HEADER_SIZE > size) {
		pe_error("%s: section table extends past the end of the file", fn);
		return false;
	}
	if (f->nsyms) {
		if ((uint64_t)symptr + (uint64_t)f->nsyms * COFF_SYMBOL_SIZE + 4 > size) {
			pe_error("%s: symbol table extends past the end of the file", fn);
			return false;
		}
		f->symtab = d + symptr;
		const uint8_t *st = f->symtab + (size_t)f->nsyms * COFF_SYMBOL_SIZE;
		f->strtab_size = pe_rd32(st);
		if (f->strtab_size < 4)
			f->strtab_size = 4;
		if ((uint64_t)(st - d) + f->strtab_size > size) {
			pe_error("%s: string table extends past the end of the file", fn);
			return false;
		}
		f->strtab = (const char *)st;
	} else {
		static const char empty[4];
		f->strtab = empty;
		f->strtab_size = 4;
	}
	f->sections = pe_alloc(((size_t)f->nsections + 1) * sizeof *f->sections);
	f->secnames = pe_alloc(((size_t)f->nsections + 1) * sizeof *f->secnames);
	for (uint32_t i = 1; i <= f->nsections; i++) {
		const uint8_t *s = d + shoff + (size_t)(i - 1) * COFF_SECTION_HEADER_SIZE;
		coff_section_header *h = &f->sections[i];
		memcpy(h->name, s, 8);
		h->name[8] = 0;
		h->virtual_size = pe_rd32(s + 8);
		h->virtual_address = pe_rd32(s + 12);
		h->size_of_raw_data = pe_rd32(s + 16);
		h->pointer_to_raw_data = pe_rd32(s + 20);
		h->pointer_to_relocations = pe_rd32(s + 24);
		h->pointer_to_linenumbers = pe_rd32(s + 28);
		h->number_of_relocations = pe_rd16(s + 32);
		h->number_of_linenumbers = pe_rd16(s + 34);
		h->characteristics = pe_rd32(s + 36);
		if (h->name[0] == '/') {
			uint64_t off = 0;
			bool ok = h->name[1] != 0;
			if (h->name[1] == '/') {
				/* //BASE64 */
				for (int k = 2; k < 8 && h->name[k]; k++) {
					char ch = h->name[k];
					int v = ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26 :
						ch >= '0' && ch <= '9' ? ch - '0' + 52 : ch == '+' ? 62 : ch == '/' ? 63 : -1;
					if (v < 0)
						ok = false;
					off = off * 64 + (uint64_t)(v < 0 ? 0 : v);
				}
			} else
				for (int k = 1; k < 8 && h->name[k]; k++) {
					if (h->name[k] < '0' || h->name[k] > '9')
						ok = false;
					off = off * 10 + (uint64_t)(h->name[k] - '0');
				}
			if (!ok || off < 4 || off >= f->strtab_size || !memchr(f->strtab + off, 0, f->strtab_size - off)) {
				pe_error("%s: invalid section name %.8s", fn, h->name);
				return false;
			}
			f->secnames[i] = f->strtab + off;
		} else
			f->secnames[i] = pe_strndup(h->name, strlen(h->name));
		if (h->characteristics & IMAGE_SCN_LNK_NRELOC_OVFL) {
			if (h->number_of_relocations == 0xffff &&
			    (uint64_t)h->pointer_to_relocations + COFF_RELOCATION_SIZE <= size)
				h->number_of_relocations = pe_rd32(d + h->pointer_to_relocations);
		}
		if (!(h->characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) && h->size_of_raw_data &&
		    (uint64_t)h->pointer_to_raw_data + h->size_of_raw_data > size) {
			pe_error("%s: section %u extends past the end of the file", fn, i);
			return false;
		}
		if (h->number_of_relocations &&
		    (uint64_t)h->pointer_to_relocations + (uint64_t)h->number_of_relocations * COFF_RELOCATION_SIZE > size) {
			pe_error("%s: relocations of section %u extend past the end of the file", fn, i);
			return false;
		}
	}
	return true;
}

static pe_symbol *pe_local_new(pe_file *f, const uint8_t *cs, uint8_t kind)
{
	pe_symbol *s = pe_alloc(sizeof *s);
	size_t n;
	const char *name = pe_obj_symbol_name(f, cs, &n);
	s->name = name;		/* (into the mapped file: not NUL-terminated for 8-byte short names) */
	s->name_len = (uint32_t)n;
	s->kind = kind;
	s->external = false;
	s->file = f;
	pe_sym_set_coff(s, cs);
	return s;
}

static bool pe_coff_is_section_definition(const uint8_t *cs)
{
	int16_t secnum = (int16_t)pe_rd16(cs + 12);
	uint8_t sc = cs[16], naux = cs[17];
	if (!naux)
		return false;
	bool appdomain = sc == IMAGE_SYM_CLASS_EXTERNAL && secnum == IMAGE_SYM_ABSOLUTE;
	if (!appdomain && sc != IMAGE_SYM_CLASS_STATIC)
		return false;
	return pe_rd32(cs + 8) == 0;
}

/* lld ObjFile::createRegular */
static pe_symbol *pe_create_regular(pe_file *f, const uint8_t *cs)
{
	int16_t secnum = (int16_t)pe_rd16(cs + 12);
	pe_chunk *sc = f->sparse[secnum];
	if (sc == PE_PENDING_COMDAT)
		sc = NULL;
	if (cs[16] == IMAGE_SYM_CLASS_EXTERNAL) {
		size_t n;
		const char *name = pe_obj_symbol_name(f, cs, &n);
		if (sc)
			return pe_add_regular(f, name, n, cs, sc, pe_rd32(cs + 8));
		if (n > 6 && !memcmp(name, ".weak.", 6))
			return NULL;
		return pe_add_undefined(name, n, f, false);
	}
	if (!sc)
		return NULL;
	pe_symbol *s = pe_local_new(f, cs, PE_SYM_REGULAR);
	s->chunk = sc;
	s->value = pe_rd32(cs + 8);
	return s;
}

/* lld readAssociativeDefinition: read the section if its parent prevails */
static void pe_read_associative(pe_file *f, uint32_t secnum, uint32_t parent)
{
	pe_chunk *p = f->sparse[parent];
	if (p == PE_PENDING_COMDAT)
		return;		/* (a parent read later, or without symbols: lld reports and keeps it pending) */
	if (!p) {
		f->sparse[secnum] = NULL;
		return;
	}
	pe_chunk *c = pe_section_read(f, secnum, true);
	f->sparse[secnum] = c;
	if (!c)
		return;
	c->selection = IMAGE_COMDAT_SELECT_ASSOCIATIVE;
	c->assoc_next = NULL;
	pe_chunk **tail = &p->assoc_first;
	while (*tail)
		tail = &(*tail)->assoc_next;
	*tail = c;
}

static void pe_comdat_duplicate(pe_symbol *leader, pe_file *f)
{
	pe_report_duplicate(leader, f);
}

/* lld ObjFile::initializeChunks + initializeSymbols; false on a malformed file */
static bool pe_obj_parse(pe_file *f)
{
	if (!pe_obj_validate(f))
		return false;
	f->sparse = pe_alloc(((size_t)f->nsections + 1) * sizeof *f->sparse);
	for (uint32_t i = 1; i <= f->nsections; i++) {
		if (f->sections[i].characteristics & IMAGE_SCN_LNK_COMDAT)
			f->sparse[i] = PE_PENDING_COMDAT;
		else {
			f->sparse[i] = pe_section_read(f, i, false);
		}
	}
	f->syms = pe_alloc(((size_t)f->nsyms + 1) * sizeof *f->syms);
	const uint8_t **comdat_defs = pe_alloc(((size_t)f->nsections + 1) * sizeof *comdat_defs);
	uint32_t *pending = pe_alloc(((size_t)f->nsyms + 1) * sizeof *pending);
	uint32_t npending = 0;
	struct { pe_symbol *sym; uint32_t tag; uint32_t characteristics; } *weak = pe_alloc(((size_t)f->nsyms + 1) * sizeof *weak);
	uint32_t nweak = 0;
	const char *fn = pe_file_name(f);
	struct { const char *s; size_t n; uint32_t sec; } *prevailing_names = pe_alloc(((size_t)f->nsections + 1) * sizeof *prevailing_names);
	uint32_t nprevailing = 0;

	for (uint32_t i = 0; i < f->nsyms; i++) {
		const uint8_t *cs = pe_obj_sym(f, i);
		int16_t secnum = (int16_t)pe_rd16(cs + 12);
		uint8_t sc = cs[16], naux = cs[17];
		uint32_t value = pe_rd32(cs + 8);
		if ((uint64_t)i + naux >= f->nsyms) {
			pe_error("%s: symbol %u: auxiliary records past the end of the symbol table", fn, i);
			return false;
		}
		size_t nlen;
		const char *name = pe_obj_symbol_name(f, cs, &nlen);
		if (pe_error_count)
			return false;
		if (secnum == IMAGE_SYM_UNDEFINED && sc != IMAGE_SYM_CLASS_WEAK_EXTERNAL &&
		    !(sc == IMAGE_SYM_CLASS_EXTERNAL && value != 0)) {
			/* undefined */
			f->syms[i] = pe_add_undefined(name, nlen, f, false);
		} else if (sc == IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
			if (!naux) {
				pe_error("%s: weak external %.*s without auxiliary record", fn, (int)nlen, name);
				return false;
			}
			const uint8_t *aux = pe_obj_sym(f, i + 1);
			f->syms[i] = pe_add_undefined(name, nlen, f, true);
			weak[nweak].sym = f->syms[i];
			weak[nweak].tag = pe_rd32(aux);
			weak[nweak].characteristics = pe_rd32(aux + 4);
			nweak++;
		} else if (secnum == IMAGE_SYM_UNDEFINED) {
			/* COMMON: external, section 0, value = size */
			pe_chunk *c = pe_chunk_new(PE_CHUNK_COMMON);
			c->file = f;
			c->name = ".bss";
			c->chars = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
			c->has_data = false;
			c->size = value;
			uint32_t a = 1;
			while (a < value && a < 32)
				a <<= 1;
			c->align = a;
			c->live = false;	/* emitted once its symbol has won at some point */
			pe_file_chunk_add(f, c);
			f->syms[i] = pe_add_common(f, name, nlen, value, cs, c);
		} else if (secnum == IMAGE_SYM_ABSOLUTE) {
			if (nlen == 8 && !memcmp(name, "@feat.00", 8))
				f->feat00 = value;
			if ((nlen == 8 && !memcmp(name, "@feat.00", 8)) || (nlen == 8 && !memcmp(name, "@comp.id", 8)))
				f->syms[i] = NULL;
			else if (sc == IMAGE_SYM_CLASS_EXTERNAL)
				f->syms[i] = pe_add_absolute(name, nlen, value, cs, f);
			else {
				pe_symbol *s = pe_local_new(f, cs, PE_SYM_ABSOLUTE);
				s->value = value;
				s->storage_class = 0;	/* lld's DefinedAbsolute: written as an external without type */
				f->syms[i] = s;
			}
		} else if (secnum == IMAGE_SYM_DEBUG) {
			f->syms[i] = NULL;
		} else if (secnum < 0 || (uint32_t)secnum > f->nsections) {
			pe_error("%s: %.*s should not refer to non-existent section %d", fn, (int)nlen, name, secnum);
			return false;
		} else if (comdat_defs[secnum]) {
			/* the COMDAT leader */
			const uint8_t *def = comdat_defs[secnum];
			comdat_defs[secnum] = NULL;
			uint8_t selection = def[14];
			pe_symbol *leader;
			bool prevailing;
			if (sc == IMAGE_SYM_CLASS_EXTERNAL) {
				bool inserted;
				leader = pe_sym_insert(name, nlen, &inserted);
				leader->used_in_regular = true;
				if (inserted || leader->kind != PE_SYM_REGULAR) {
					pe_sym_replace(leader, PE_SYM_REGULAR, f);
					pe_sym_set_coff(leader, cs);
					leader->is_comdat = true;
					leader->value = value;
					prevailing = true;
				} else {
					prevailing = false;
					if (!leader->is_comdat)
						pe_report_duplicate(leader, f);
				}
			} else {
				leader = pe_local_new(f, cs, PE_SYM_REGULAR);
				leader->value = value;
				prevailing = true;
			}
			if (!prevailing && leader->is_comdat && leader->chunk) {
				/* lld handleComdatSelection */
				pe_chunk *lc = leader->chunk;
				uint8_t lsel = lc->selection;
				uint32_t nsize = f->sections[secnum].size_of_raw_data;
				if ((selection == IMAGE_COMDAT_SELECT_ANY && lsel == IMAGE_COMDAT_SELECT_LARGEST) ||
				    (selection == IMAGE_COMDAT_SELECT_LARGEST && lsel == IMAGE_COMDAT_SELECT_ANY))
					lsel = selection = IMAGE_COMDAT_SELECT_LARGEST;
				if ((selection == IMAGE_COMDAT_SELECT_ANY && lsel == IMAGE_COMDAT_SELECT_SAME_SIZE) ||
				    (selection == IMAGE_COMDAT_SELECT_SAME_SIZE && lsel == IMAGE_COMDAT_SELECT_ANY))
					lsel = selection = IMAGE_COMDAT_SELECT_SAME_SIZE;
				if (selection != lsel)
					pe_comdat_duplicate(leader, f);
				else
					switch (selection) {
					case IMAGE_COMDAT_SELECT_NODUPLICATES:
						pe_comdat_duplicate(leader, f);
						break;
					case IMAGE_COMDAT_SELECT_SAME_SIZE:
						if (lc->size != nsize)
							pe_comdat_duplicate(leader, f);
						break;
					case IMAGE_COMDAT_SELECT_EXACT_MATCH:
						if (lc->size != nsize || (lc->has_data && f->sections[secnum].size_of_raw_data &&
						    !(f->sections[secnum].characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) &&
						    memcmp(lc->data, f->data + f->sections[secnum].pointer_to_raw_data, nsize)))
							pe_comdat_duplicate(leader, f);
						break;
					case IMAGE_COMDAT_SELECT_LARGEST:
						if (lc->size < nsize) {
							pe_sym_replace(leader, PE_SYM_REGULAR, f);
							pe_sym_set_coff(leader, cs);
							leader->is_comdat = true;
							leader->value = value;
							prevailing = true;
						}
						break;
					default:
						break;
					}
			}
			if (prevailing) {
				pe_chunk *c = pe_section_read(f, (uint32_t)secnum, true);
				f->sparse[secnum] = c;
				if (c) {
					c->leader = leader;
					c->selection = selection;
					leader->chunk = c;
				} else if (leader->external) {
					/* (a COMDAT .drectve or debug section: no chunk) */
					leader->chunk = NULL;
				}
				if (!c && !leader->external) {
					f->syms[i] = NULL;
					i += naux;
					continue;
				}
			} else
				f->sparse[secnum] = NULL;
			f->syms[i] = leader;
			/* lld recordPrevailingSymbolForMingw */
			pe_chunk *pc = f->sparse[secnum];
			if (prevailing && pc && (pc->chars & IMAGE_SCN_MEM_EXECUTE)) {
				const char *dollar = strchr(pc->name, '$');
				const char *suffix = dollar ? dollar + 1 : "";
				uint32_t k2 = 0;
				while (k2 < nprevailing && !(prevailing_names[k2].n == strlen(suffix) && !memcmp(prevailing_names[k2].s, suffix, strlen(suffix))))
					k2++;
				if (k2 == nprevailing)
					nprevailing++;
				prevailing_names[k2].s = suffix;
				prevailing_names[k2].n = strlen(suffix);
				prevailing_names[k2].sec = (uint32_t)secnum;
			}
		} else if (f->sparse[secnum] == PE_PENDING_COMDAT) {
			if (naux && pe_coff_is_section_definition(cs) && sc == IMAGE_SYM_CLASS_STATIC) {
				const uint8_t *def = pe_obj_sym(f, i + 1);
				if (def[14] != IMAGE_COMDAT_SELECT_ASSOCIATIVE)
					comdat_defs[secnum] = def;
				if (def[14] == IMAGE_COMDAT_SELECT_NEWEST || def[14] > IMAGE_COMDAT_SELECT_LARGEST) {
					pe_error("%s: unknown comdat type %u for %.*s", fn, def[14], (int)nlen, name);
					return false;
				}
			}
			pending[npending++] = i;
		} else {
			f->syms[i] = pe_create_regular(f, cs);
			if (f->syms[i] && !f->syms[i]->external && pe_coff_is_section_definition(cs))
				f->syms[i]->storage_class = IMAGE_SYM_CLASS_SECTION;	/* (marks "not written") */
		}
		i += naux;
	}

	for (uint32_t k = 0; k < npending; k++) {
		uint32_t i = pending[k];
		const uint8_t *cs = pe_obj_sym(f, i);
		int16_t secnum = (int16_t)pe_rd16(cs + 12);
		if (cs[17] && pe_coff_is_section_definition(cs) && cs[16] == IMAGE_SYM_CLASS_STATIC) {
			const uint8_t *def = pe_obj_sym(f, i + 1);
			if (def[14] == IMAGE_COMDAT_SELECT_ASSOCIATIVE) {
				uint32_t parent = pe_rd16(def + 12);
				if (parent == 0 || parent > f->nsections || (int32_t)parent == secnum) {
					pe_error("%s: associative comdat %s has invalid reference to section %u", fn,
						 f->secnames[secnum], parent);
					return false;
				}
				pe_read_associative(f, (uint32_t)secnum, parent);
			} else {
				/* MinGW: .pdata$F, .xdata$F, .eh_frame$F belong to the prevailing .text$F (lld maybeAssociateSEHForMingw) */
				size_t nlen;
				const char *nm = pe_obj_symbol_name(f, cs, &nlen);
				size_t pre = nlen > 7 && (!memcmp(nm, ".pdata$", 7) || !memcmp(nm, ".xdata$", 7)) ? 7 :
					     nlen > 10 && !memcmp(nm, ".eh_frame$", 10) ? 10 : 0;
				if (pre)
					for (uint32_t k2 = 0; k2 < nprevailing; k2++)
						if (prevailing_names[k2].n == nlen - pre && !memcmp(prevailing_names[k2].s, nm + pre, nlen - pre)) {
							pe_read_associative(f, (uint32_t)secnum, prevailing_names[k2].sec);
							break;
						}
			}
		}
		if (f->sparse[secnum] == PE_PENDING_COMDAT) {
			pe_log("comdat section %s without leader and unassociated, discarding", f->secnames[secnum]);
			continue;
		}
		f->syms[i] = pe_create_regular(f, cs);
		if (f->syms[i] && !f->syms[i]->external && pe_coff_is_section_definition(cs))
			f->syms[i]->storage_class = IMAGE_SYM_CLASS_SECTION;
	}

	for (uint32_t k = 0; k < nweak; k++) {
		if (weak[k].tag >= f->nsyms) {
			pe_error("%s: weak external %s refers to symbol %u past the end", fn, weak[k].sym->name, weak[k].tag);
			return false;
		}
		pe_symbol *target = f->syms[weak[k].tag];
		pe_symbol *u = weak[k].sym;
		bool anti = weak[k].characteristics == IMAGE_WEAK_EXTERN_ANTI_DEPENDENCY;
		if (u->kind != PE_SYM_UNDEFINED)
			continue;
		if (u->weak_alias && u->weak_alias != target) {
			if (anti)
				continue;
			if (!u->weak_anti_dependency) {
				if (cfg.allow_duplicate_weak)
					continue;
				pe_report_duplicate(u, f);
			}
		}
		u->weak_alias = target;
		u->weak_anti_dependency = anti;
	}
	return true;
}

/* ---------------- short import records ---------------- */

static pe_symbol *pe_add_import_data(const char *name, pe_file *f)
{
	bool inserted;
	pe_symbol *s = pe_sym_insert(name, strlen(name), &inserted);
	s->used_in_regular = true;
	if (inserted || s->kind == PE_SYM_UNDEFINED || s->kind == PE_SYM_LAZY_ARCHIVE) {
		pe_sym_replace(s, PE_SYM_IMPORT_DATA, f);
		return s;
	}
	pe_report_duplicate(s, f);
	return NULL;
}

static bool pe_import_parse(pe_file *f)
{
	const uint8_t *d = f->data;
	if (f->size < COFF_IMPORT_HEADER_SIZE || f->size != COFF_IMPORT_HEADER_SIZE + (size_t)pe_rd32(d + 12)) {
		pe_error("%s: broken import library", pe_file_name(f));
		return false;
	}
	uint16_t machine = pe_rd16(d + 6);
	if (machine != IMAGE_FILE_MACHINE_AMD64) {
		pe_error("%s: machine type %#x conflicts with x64", pe_file_name(f), machine);
		return false;
	}
	const char *p = (const char *)d + COFF_IMPORT_HEADER_SIZE, *end = (const char *)d + f->size;
	const char *z1 = memchr(p, 0, (size_t)(end - p));
	if (!z1) {
		pe_error("%s: broken import library", pe_file_name(f));
		return false;
	}
	const char *dll = z1 + 1;
	const char *z2 = dll < end ? memchr(dll, 0, (size_t)(end - dll)) : NULL;
	if (!z2) {
		pe_error("%s: broken import library", pe_file_name(f));
		return false;
	}
	const char *name = pe_strndup(p, (size_t)(z1 - p));
	f->dll_name = pe_strndup(dll, (size_t)(z2 - dll));
	f->hint = pe_rd16(d + 16);
	uint16_t t = pe_rd16(d + 18);
	f->import_type = t & 3;
	f->name_type = (t >> 2) & 7;
	switch (f->name_type) {
	case IMPORT_ORDINAL:
		f->external_name = "";
		break;
	case IMPORT_NAME:
		f->external_name = name;
		break;
	case IMPORT_NAME_NOPREFIX:
	case IMPORT_NAME_UNDECORATE: {
		const char *e = name;
		if (*e == '?' || *e == '@' || *e == '_')
			e++;
		if (f->name_type == IMPORT_NAME_UNDECORATE) {
			const char *at = strchr(e, '@');
			f->external_name = at ? pe_strndup(e, (size_t)(at - e)) : e;
		} else
			f->external_name = e;
		break;
	}
	case IMPORT_NAME_EXPORTAS: {
		const char *x = z2 + 1;
		const char *z3 = x < end ? memchr(x, 0, (size_t)(end - x)) : NULL;
		f->external_name = z3 ? pe_strndup(x, (size_t)(z3 - x)) : "";
		break;
	}
	default:
		pe_error("%s: unknown import name type %u", pe_file_name(f), f->name_type);
		return false;
	}
	f->live = !cfg.gc;
	f->thunk_live = f->live;
	f->imp_sym = pe_add_import_data(pe_format("__imp_%s", name), f);
	if (!f->imp_sym)
		return true;
	if (f->import_type == IMPORT_CONST)
		(void)pe_add_import_data(name, f);
	if (f->import_type == IMPORT_CODE) {
		bool inserted;
		pe_symbol *s = pe_sym_insert(name, strlen(name), &inserted);
		s->used_in_regular = true;
		if (inserted || s->kind == PE_SYM_UNDEFINED || s->kind == PE_SYM_LAZY_ARCHIVE) {
			pe_sym_replace(s, PE_SYM_IMPORT_THUNK, f);
			s->wrapped = f->imp_sym;
			pe_chunk *c = pe_chunk_new(PE_CHUNK_IMPORT_THUNK);
			c->name = ".text";
			c->chars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
			c->size = 6;
			c->align = 16;
			c->sym = f->imp_sym;
			c->file = f;
			s->chunk = c;
			f->thunk_sym = s;
		} else
			pe_report_duplicate(s, f);
	}
	return true;
}

/* ---------------- archives ---------------- */

static uint32_t pe_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* a member header at OFF: its body and name; false if malformed */
static bool pe_archive_member(pe_archive *a, uint32_t off, const uint8_t **body, size_t *size, const char **name)
{
	if ((uint64_t)off + 60 > a->size) {
		pe_error("%s: truncated archive member header", a->path);
		return false;
	}
	const uint8_t *h = a->data + off;
	if (h[58] != 0x60 || h[59] != 0x0a) {
		pe_error("%s: malformed archive member header at %u", a->path, off);
		return false;
	}
	char sz[11];
	memcpy(sz, h + 48, 10);
	sz[10] = 0;
	char *e;
	unsigned long long n = strtoull(sz, &e, 10);
	while (*e == ' ')
		e++;
	if (*e || (uint64_t)off + 60 + n > a->size) {
		pe_error("%s: archive member at %u extends past the end of the file", a->path, off);
		return false;
	}
	*body = h + 60;
	*size = (size_t)n;
	if (name) {
		const char *nm = (const char *)h;
		if (nm[0] == '/' && nm[1] >= '0' && nm[1] <= '9') {
			unsigned long lo = strtoul(nm + 1, NULL, 10);
			if (!a->longnames || lo >= a->longnames_size) {
				pe_error("%s: invalid long member name", a->path);
				return false;
			}
			const char *s = a->longnames + lo;
			size_t k = 0;
			while (lo + k < a->longnames_size && s[k] != '\n' && s[k] != 0)
				k++;
			if (k && s[k - 1] == '/')
				k--;
			*name = pe_strndup(s, k);
		} else {
			size_t k = 0;
			while (k < 16 && nm[k] != '/' && nm[k] != ' ')
				k++;
			*name = pe_strndup(nm, k);
		}
	}
	return true;
}

static pe_archive *pe_archive_open(const char *path, const uint8_t *data, size_t size)
{
	pe_archive *a = pe_alloc(sizeof *a);
	a->path = path;
	a->data = data;
	a->size = size;
	PE_PUSH(pe_archives, pe_archive_count, pe_archive_capacity, a);
	if (!memcmp(data, "!<thin>\n", 8)) {
		pe_error("%s: thin archives are not supported by vlink", path);
		return NULL;
	}
	uint32_t off = 8;
	const uint8_t *first = NULL, *second = NULL;
	size_t first_size = 0, second_size = 0;
	/* the special members: "/" (once or twice) and "//" */
	for (int k = 0; k < 3 && off + 60 <= size; k++) {
		const uint8_t *h = data + off;
		const uint8_t *body;
		size_t n;
		if (!pe_archive_member(a, off, &body, &n, NULL))
			return NULL;
		if (h[0] == '/' && h[1] == ' ') {
			if (!first) { first = body; first_size = n; }
			else { second = body; second_size = n; }
		} else if (h[0] == '/' && h[1] == '/' && h[2] == ' ') {
			a->longnames = (const char *)body;
			a->longnames_size = n;
		} else
			break;
		off += 60 + (uint32_t)n;
		off += off & 1;
	}
	if (second) {
		/* Microsoft's sorted table: members, offsets, symbols, 1-based indices, names */
		if (second_size < 4)
			goto bad;
		uint32_t m = pe_rd32(second);
		if ((uint64_t)4 + (uint64_t)m * 4 + 4 > second_size)
			goto bad;
		uint32_t n = pe_rd32(second + 4 + (size_t)m * 4);
		const uint8_t *idx = second + 8 + (size_t)m * 4;
		if ((uint64_t)8 + (uint64_t)m * 4 + (uint64_t)n * 2 > second_size)
			goto bad;
		const char *names = (const char *)idx + (size_t)n * 2, *nend = (const char *)second + second_size;
		a->symbols = calloc(n ? n : 1, sizeof *a->symbols);
		if (!a->symbols)
			pe_oom();
		for (uint32_t i = 0; i < n; i++) {
			const char *z = names < nend ? memchr(names, 0, (size_t)(nend - names)) : NULL;
			uint16_t ix = pe_rd16(idx + (size_t)i * 2);
			if (!z || ix == 0 || ix > m)
				goto bad;
			a->symbols[i].name = names;
			a->symbols[i].offset = pe_rd32(second + 4 + (size_t)(ix - 1) * 4);
			names = z + 1;
		}
		a->nsymbols = n;
	} else if (first) {
		if (first_size < 4)
			goto bad;
		uint32_t n = pe_be32(first);
		if ((uint64_t)4 + (uint64_t)n * 4 > first_size)
			goto bad;
		const char *names = (const char *)first + 4 + (size_t)n * 4, *nend = (const char *)first + first_size;
		a->symbols = calloc(n ? n : 1, sizeof *a->symbols);
		if (!a->symbols)
			pe_oom();
		for (uint32_t i = 0; i < n; i++) {
			const char *z = names < nend ? memchr(names, 0, (size_t)(nend - names)) : NULL;
			if (!z)
				goto bad;
			a->symbols[i].name = names;
			a->symbols[i].offset = pe_be32(first + 4 + (size_t)i * 4);
			names = z + 1;
		}
		a->nsymbols = n;
	}
	return a;
bad:
	pe_error("%s: malformed archive symbol table", path);
	return NULL;
}

/* lld ArchiveFile::addMember's "seen" set: true the first time */
static bool pe_archive_seen_insert(pe_archive *a, uint32_t off)
{
	uint32_t key = off + 1;
	if (a->seen_count * 2 >= a->seen_capacity) {
		uint32_t ncap = a->seen_capacity ? a->seen_capacity * 2 : 64;
		uint32_t *ns = calloc(ncap, sizeof *ns);
		if (!ns)
			pe_oom();
		for (uint32_t i = 0; i < a->seen_capacity; i++)
			if (a->seen[i]) {
				uint32_t h = (a->seen[i] * 2654435761u) & (ncap - 1);
				while (ns[h])
					h = (h + 1) & (ncap - 1);
				ns[h] = a->seen[i];
			}
		free(a->seen);
		a->seen = ns;
		a->seen_capacity = ncap;
	}
	uint32_t h = (key * 2654435761u) & (a->seen_capacity - 1);
	while (a->seen[h]) {
		if (a->seen[h] == key)
			return false;
		h = (h + 1) & (a->seen_capacity - 1);
	}
	a->seen[h] = key;
	a->seen_count++;
	return true;
}
