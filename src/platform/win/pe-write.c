/*
 * The file: section contents with relocations applied (x86-64 COFF types),
 * the synthetic chunks, DOS stub, PE32+ headers and data directories, the
 * section table, the COFF symbol table and string table lld writes for
 * MinGW (debug sections' long names, defined symbols per object in symbol
 * order), sorted .pdata, the TLS directory's alignment, and the build id:
 * XXH3-64 of the file with the time stamps still zero, then the time stamp
 * (SOURCE_DATE_EPOCH, the time, or 0 with --no-insert-timestamp).
 */

typedef struct { uint8_t raw[COFF_SYMBOL_SIZE]; int32_t str; } pe_out_symbol;
static pe_out_symbol *pe_out_symbols;
static uint32_t pe_out_symbol_count, pe_out_symbol_capacity;
/* the string table: lld builds it with StringTableBuilder(WinCOFF), so the strings are
   deduplicated, sorted by their reversed text (descending) and tails are shared */
typedef struct { const char *s; uint32_t n, offset; } pe_string;
static pe_string *pe_strings;
static uint32_t pe_string_count, pe_string_capacity;
static char *pe_strtab;
static size_t pe_strtab_size;
static uint64_t pe_symtab_offset;
static uint32_t pe_section_string_count;

static void pe_write_reset(void)
{
	free(pe_out_symbols);
	pe_out_symbols = NULL;
	pe_out_symbol_count = pe_out_symbol_capacity = 0;
	free(pe_strings);
	pe_strings = NULL;
	pe_string_count = pe_string_capacity = 0;
	free(pe_strtab);
	pe_strtab = NULL;
	pe_strtab_size = 0;
	pe_symtab_offset = 0;
	pe_section_string_count = 0;
}

static int32_t pe_strtab_add(const char *s, size_t n)
{
	pe_string x = { s, (uint32_t)n, 0 };
	PE_PUSH(pe_strings, pe_string_count, pe_string_capacity, x);
	return (int32_t)pe_string_count - 1;
}

/* two groups, each sorted and tail-merged on its own (common/string-table.h): section names first, then symbol names */
static size_t pe_strtab_group(uint32_t from, uint32_t to, size_t size)
{
	if (from >= to)
		return size;
	vstring_ref *v = malloc((to - from) * sizeof *v);
	if (!v)
		pe_oom();
	for (uint32_t i = from; i < to; i++)
		v[i - from] = (vstring_ref){ pe_strings[i].s, pe_strings[i].n, &pe_strings[i].offset };
	size = vstring_table_layout(v, to - from, (uint32_t)size);
	vstring_table_fill((uint8_t *)pe_strtab, v, to - from, 4);
	free(v);
	return size;
}

static void pe_strtab_finalize(void)
{
	if (!pe_string_count)
		return;
	size_t total = 0;
	for (uint32_t i = 0; i < pe_string_count; i++)
		total += pe_strings[i].n + 1;
	pe_strtab = malloc(total + 1);
	if (!pe_strtab)
		pe_oom();
	size_t size = pe_strtab_group(0, pe_section_string_count, 4);
	size = pe_strtab_group(pe_section_string_count, pe_string_count, size);
	pe_strtab_size = size - 4;
}

/* the chunk a symbol is in: an __imp_ symbol (also a copy made by auto-import) is its file's IAT slot */
static pe_chunk *pe_sym_chunk(const pe_symbol *s)
{
	if (s->kind == PE_SYM_IMPORT_DATA)
		return s->file ? s->file->location : NULL;
	return s->chunk;
}

/* the symbol's RVA (lld Defined::getRVA) */
static uint64_t pe_symbol_rva(const pe_symbol *s)
{
	pe_chunk *c = pe_sym_chunk(s);
	switch (s->kind) {
	case PE_SYM_REGULAR:
		return c ? (uint64_t)c->rva + s->value : 0;
	case PE_SYM_COMMON:
	case PE_SYM_IMPORT_DATA:
	case PE_SYM_IMPORT_THUNK:
	case PE_SYM_LOCAL_IMPORT:
		return c ? c->rva : 0;
	case PE_SYM_SYNTHETIC:
		return c ? (uint64_t)c->rva + s->value : 0;
	case PE_SYM_ABSOLUTE:
		return s->value - cfg.image_base;
	default:
		return 0;
	}
}

static pe_output *pe_symbol_output(const pe_symbol *s)
{
	if (s->kind == PE_SYM_ABSOLUTE || s->kind == PE_SYM_UNDEFINED || s->kind == PE_SYM_LAZY_ARCHIVE)
		return NULL;
	pe_chunk *c = pe_sym_chunk(s);
	if (!c || !c->os || c->os->removed)
		return NULL;
	if ((c->kind == PE_CHUNK_SECTION || c->kind == PE_CHUNK_COMMON) && !c->live)
		return NULL;
	return c->os;
}

/* ---------------- symbol table (lld createSymbolAndStringTable) ---------------- */

static void pe_symbol_emit(pe_symbol *d)
{
	pe_out_symbol o;
	memset(&o, 0, sizeof o);
	o.str = -1;
	uint8_t *p = o.raw;
	if (d->kind == PE_SYM_ABSOLUTE) {
		pe_wr32(p + 8, (uint32_t)d->value);
		pe_wr16(p + 12, (uint16_t)IMAGE_SYM_ABSOLUTE);
	} else {
		pe_output *os = pe_symbol_output(d);
		if (!os)
			return;
		pe_wr32(p + 8, (uint32_t)(pe_symbol_rva(d) - os->rva));
		pe_wr16(p + 12, (uint16_t)os->index);
	}
	if (d->runtime_pseudo_reloc)
		return;
	if (d->name_len > 8)
		o.str = pe_strtab_add(d->name, d->name_len);
	else
		memcpy(p, d->name, d->name_len);
	bool coff = d->storage_class && (d->kind == PE_SYM_REGULAR || d->kind == PE_SYM_COMMON);
	if (coff) {
		pe_wr16(p + 14, d->type);
		p[16] = d->storage_class;
	} else if (d->kind == PE_SYM_IMPORT_THUNK) {
		pe_wr16(p + 14, IMAGE_SYM_DTYPE_FUNCTION << 4);
		p[16] = IMAGE_SYM_CLASS_EXTERNAL;
	} else
		p[16] = IMAGE_SYM_CLASS_EXTERNAL;
	PE_PUSH(pe_out_symbols, pe_out_symbol_count, pe_out_symbol_capacity, o);
}

static void pe_symbol_table(void)
{
	for (uint32_t i = 0; i < pe_output_count; i++) {
		pe_output *o = pe_outputs[i];
		o->strtab_offset = 0;
		o->str = -1;
		if (strlen(o->name) > 8 && (o->chars & IMAGE_SCN_MEM_DISCARDABLE))
			o->str = pe_strtab_add(o->name, strlen(o->name));
	}
	pe_section_string_count = pe_string_count;
	if (cfg.debug) {
		for (uint32_t fi = 0; fi < pe_object_count; fi++) {
			pe_file *f = pe_objects[fi];
			for (uint32_t k = 0; k < f->nsyms; k++) {
				pe_symbol *d = f->syms[k];
				if (!d || !pe_sym_is_defined(d) || d->written)
					continue;
				d->written = true;
				if (d->kind == PE_SYM_REGULAR || d->kind == PE_SYM_COMMON) {
					const uint8_t *cs = NULL;
					/* section definitions and labels of the defining object are not written */
					if (d->storage_class == IMAGE_SYM_CLASS_SECTION || d->storage_class == IMAGE_SYM_CLASS_LABEL)
						continue;
					(void)cs;
				}
				pe_symbol_emit(d);
				if (d->kind == PE_SYM_IMPORT_THUNK && d->wrapped && !d->wrapped->written) {
					d->wrapped->written = true;
					pe_symbol_emit(d->wrapped);
				}
			}
		}
	}
	pe_strtab_finalize();
	for (uint32_t i = 0; i < pe_output_count; i++)
		if (pe_outputs[i]->str >= 0)
			pe_outputs[i]->strtab_offset = pe_strings[pe_outputs[i]->str].offset;
	for (uint32_t i = 0; i < pe_out_symbol_count; i++)
		if (pe_out_symbols[i].str >= 0)
			pe_wr32(pe_out_symbols[i].raw + 4, pe_strings[pe_out_symbols[i].str].offset);
	if (!pe_out_symbol_count && !pe_strtab_size)
		return;
	pe_symtab_offset = pe_file_size;
	uint64_t end = pe_file_size + (uint64_t)pe_out_symbol_count * COFF_SYMBOL_SIZE + 4 + pe_strtab_size;
	pe_file_size = pe_align_up(end, cfg.file_align);
}

/* ---------------- relocations ---------------- */

static void pe_add16(uint8_t *p, uint16_t v) { pe_wr16(p, (uint16_t)(pe_rd16(p) + v)); }
static void pe_add32(uint8_t *p, uint32_t v) { pe_wr32(p, pe_rd32(p) + v); }
static void pe_add64(uint8_t *p, uint64_t v) { pe_wr64(p, pe_rd64(p) + v); }

static void pe_section_chunk_write(pe_chunk *c, uint8_t *buf)
{
	if (!c->has_data)
		return;
	if (c->size)
		memcpy(buf, c->data, c->size);
	pe_file *f = c->file;
	for (uint32_t r = 0; r < c->nrelocs; r++) {
		const uint8_t *rel = c->relocs + (size_t)r * COFF_RELOCATION_SIZE;
		uint32_t va = pe_rd32(rel), ix = pe_rd32(rel + 4);
		uint16_t type = pe_rd16(rel + 8);
		if (va >= c->size) {
			pe_error("relocation points beyond the end of its parent section");
			continue;
		}
		uint8_t *off = buf + va;
		pe_symbol *sym = ix < f->nsyms ? f->syms[ix] : NULL;
		if (ix >= f->nsyms) {
			pe_error("%s: relocation refers to symbol %u past the end of the symbol table", pe_file_name(f), ix);
			continue;
		}
		pe_output *os = sym && pe_sym_is_defined(sym) ? pe_symbol_output(sym) : NULL;
		if (!sym || !pe_sym_is_defined(sym) || (!os && sym->kind != PE_SYM_ABSOLUTE && sym->kind != PE_SYM_SYNTHETIC)) {
			/* a reference into a discarded section: harmless from debug info */
			if (!c->is_dwarf && sym && pe_sym_is_defined(sym) && !(sym->name_len > 6 && !memcmp(sym->name, ".weak.", 6)))
				pe_error("relocation against symbol in discarded section: %.*s", (int)sym->name_len, sym->name);
			continue;
		}
		/* bytes the relocation writes must lie in the section */
		uint32_t width = type == IMAGE_REL_AMD64_ADDR64 ? 8 : type == IMAGE_REL_AMD64_SECTION ? 2 :
				 type == IMAGE_REL_AMD64_ABSOLUTE ? 0 : 4;
		if ((uint64_t)va + width > c->size) {
			pe_error("%s: relocation at %#x does not fit in section %s", pe_file_name(f), va, c->name);
			continue;
		}
		uint64_t s = pe_symbol_rva(sym);
		uint64_t p = (uint64_t)c->rva + va;
		switch (type) {
		case IMAGE_REL_AMD64_ABSOLUTE:
			break;
		case IMAGE_REL_AMD64_ADDR32:
			pe_add32(off, (uint32_t)(s + cfg.image_base));
			break;
		case IMAGE_REL_AMD64_ADDR64:
			pe_add64(off, s + cfg.image_base);
			break;
		case IMAGE_REL_AMD64_ADDR32NB:
			pe_add32(off, (uint32_t)s);
			break;
		case IMAGE_REL_AMD64_REL32:
		case IMAGE_REL_AMD64_REL32_1:
		case IMAGE_REL_AMD64_REL32_2:
		case IMAGE_REL_AMD64_REL32_3:
		case IMAGE_REL_AMD64_REL32_4:
		case IMAGE_REL_AMD64_REL32_5:
			pe_add32(off, (uint32_t)(s - p - 4 - (type - IMAGE_REL_AMD64_REL32)));
			break;
		case IMAGE_REL_AMD64_SECTION:
			pe_add16(off, (uint16_t)(os ? os->index : pe_output_count + 1));
			break;
		case IMAGE_REL_AMD64_SECREL:
			if (!os) {
				pe_error("SECREL relocation cannot be applied to absolute symbols");
				break;
			}
			if (s - os->rva > UINT32_MAX) {
				pe_error("overflow in SECREL relocation in section: %s", c->name);
				break;
			}
			pe_add32(off, (uint32_t)(s - os->rva));
			break;
		default:
			pe_error("unsupported relocation type 0x%x in %s", type, pe_file_name(f));
			break;
		}
	}
}

static void pe_chunk_write(pe_chunk *c, uint8_t *buf, uint8_t *image)
{
	switch (c->kind) {
	case PE_CHUNK_SECTION:
		pe_section_chunk_write(c, buf);
		break;
	case PE_CHUNK_COMMON:
	case PE_CHUNK_NULL:
	case PE_CHUNK_EMPTY:
		break;
	case PE_CHUNK_IMPORT_DIRECTORY:
		memset(buf, 0, PE_IMPORT_DIRECTORY_SIZE);
		pe_wr32(buf, c->target->rva);
		pe_wr32(buf + 12, c->target3->rva);
		pe_wr32(buf + 16, c->target2->rva);
		break;
	case PE_CHUNK_LOOKUP:
		pe_wr64(buf, c->target->rva);
		break;
	case PE_CHUNK_ORDINAL_ONLY:
		pe_wr64(buf, (1ull << 63) | c->value);
		break;
	case PE_CHUNK_HINT_NAME:
		pe_wr16(buf, (uint16_t)c->value);
		memcpy(buf + 2, c->str, strlen(c->str));
		break;
	case PE_CHUNK_STRING:
		memcpy(buf, c->str, c->size);
		break;
	case PE_CHUNK_IMPORT_THUNK:
		buf[0] = 0xff;
		buf[1] = 0x25;
		pe_wr32(buf + 2, (uint32_t)(pe_symbol_rva(c->sym) - c->rva - 6));
		break;
	case PE_CHUNK_ABSOLUTE_POINTER:
		pe_wr64(buf, c->value);
		break;
	case PE_CHUNK_PSEUDO_RELOC_TABLE: {
		if (!c->aux_count)
			break;
		pe_pseudo_reloc *rels = c->aux;
		pe_wr32(buf, 0);
		pe_wr32(buf + 4, 0);
		pe_wr32(buf + 8, 1);
		for (uint32_t i = 0; i < c->aux_count; i++) {
			pe_wr32(buf + 12 + 12 * i, (uint32_t)pe_symbol_rva(rels[i].sym));
			pe_wr32(buf + 16 + 12 * i, rels[i].target->rva + rels[i].offset);
			pe_wr32(buf + 20 + 12 * i, rels[i].flags);
		}
		break;
	}
	case PE_CHUNK_DEBUG_DIRECTORY: {
		pe_chunk *cv = pe_cv_record;
		memset(buf, 0, PE_DEBUG_DIRECTORY_SIZE);
		pe_wr32(buf + 12, IMAGE_DEBUG_TYPE_CODEVIEW);
		pe_wr32(buf + 16, cv->size);
		pe_wr32(buf + 20, cv->rva);
		pe_wr32(buf + 24, cv->os->file_offset + (cv->rva - cv->os->rva));
		break;
	}
	case PE_CHUNK_CODEVIEW_RECORD:
		/* (filled in after the build id hash, which sees zeros here) */
		memset(buf, 0, c->size);
		break;
	case PE_CHUNK_BASEREL_BLOCK:
		memcpy(buf, c->bytes, c->size);
		break;
	case PE_CHUNK_LOCAL_IMPORT:
		pe_wr64(buf, pe_symbol_rva(c->sym) + cfg.image_base);
		break;
	case PE_CHUNK_EXPORT_DIRECTORY: {
		pe_chunk *dll = c->aux;
		memset(buf, 0, PE_EXPORT_DIRECTORY_SIZE);
		pe_wr32(buf + 12, dll->rva);
		pe_wr32(buf + 16, pe_export_base);
		pe_wr32(buf + 20, (uint32_t)c->value);
		pe_wr32(buf + 24, c->aux_count);
		pe_wr32(buf + 28, c->target->rva);
		pe_wr32(buf + 32, c->target2->rva);
		pe_wr32(buf + 36, c->target3->rva);
		break;
	}
	case PE_CHUNK_EXPORT_ADDRESS_TABLE:
		for (uint32_t i = 0; i < cfg.nexports; i++) {
			pe_export *e = &cfg.exports[i];
			if (e->forward_chunk)
				pe_wr32(buf + 4 * (e->ordinal - pe_export_base), e->forward_chunk->rva);
			else if (e->sym && pe_sym_is_defined(e->sym))
				pe_wr32(buf + 4 * (e->ordinal - pe_export_base), (uint32_t)pe_symbol_rva(e->sym));
		}
		break;
	case PE_CHUNK_EXPORT_NAME_POINTERS: {
		pe_chunk **strs = c->aux;
		for (uint32_t i = 0; i < c->aux_count; i++)
			pe_wr32(buf + 4 * i, strs[i]->rva);
		break;
	}
	case PE_CHUNK_EXPORT_ORDINALS: {
		uint32_t k = 0;
		for (uint32_t i = 0; i < cfg.nexports; i++)
			if (!cfg.exports[i].no_name)
				pe_wr16(buf + 2 * k++, (uint16_t)(cfg.exports[i].ordinal - pe_export_base));
		break;
	}
	}
	(void)image;
}

/* ---------------- headers ---------------- */

static const uint8_t pe_dos_program[] = {
	0x0e, 0x1f, 0xba, 0x0e, 0x00, 0xb4, 0x09, 0xcd, 0x21, 0xb8, 0x01, 0x4c, 0xcd, 0x21, 0x54, 0x68,
	0x69, 0x73, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x20, 0x63, 0x61, 0x6e, 0x6e, 0x6f,
	0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6e, 0x20, 0x69, 0x6e, 0x20, 0x44, 0x4f, 0x53, 0x20,
	0x6d, 0x6f, 0x64, 0x65, 0x2e, 0x24, 0x00, 0x00,
};

static uint32_t pe_size_of_initialized_data(void)
{
	uint32_t r = 0;
	for (uint32_t i = 0; i < pe_output_count; i++)
		if (pe_outputs[i]->chars & IMAGE_SCN_CNT_INITIALIZED_DATA)
			r += pe_outputs[i]->raw_size;
	return r;
}

static void pe_headers_write(uint8_t *img)
{
	const uint32_t stub = PE_DOS_HEADER_SIZE + sizeof pe_dos_program;
	img[0] = 'M';
	img[1] = 'Z';
	pe_wr16(img + 2, stub % 512);
	pe_wr16(img + 4, (uint16_t)((stub + 511) / 512));
	pe_wr16(img + 8, PE_DOS_HEADER_SIZE / 16);
	pe_wr16(img + 24, PE_DOS_HEADER_SIZE);
	pe_wr32(img + 60, stub);
	memcpy(img + PE_DOS_HEADER_SIZE, pe_dos_program, sizeof pe_dos_program);
	uint8_t *p = img + stub;
	memcpy(p, "PE\0\0", 4);
	uint8_t *coff = p + 4;
	pe_wr16(coff, IMAGE_FILE_MACHINE_AMD64);
	pe_wr16(coff + 2, (uint16_t)pe_output_count);
	uint16_t ch = IMAGE_FILE_EXECUTABLE_IMAGE;
	if (cfg.large_address_aware)
		ch |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
	if (cfg.dll)
		ch |= IMAGE_FILE_DLL;
	if (!cfg.relocatable)
		ch |= IMAGE_FILE_RELOCS_STRIPPED;
	pe_wr16(coff + 16, PE32PLUS_OPTIONAL_HEADER_SIZE);
	pe_wr16(coff + 18, ch);
	if (pe_out_symbol_count || pe_strtab_size) {
		pe_wr32(coff + 8, (uint32_t)pe_symtab_offset);
		pe_wr32(coff + 12, pe_out_symbol_count);
	}
	uint8_t *o = coff + COFF_FILE_HEADER_SIZE;
	pe_wr16(o, PE32PLUS_MAGIC);
	o[2] = 14;
	o[3] = 0;
	if (pe_text && !pe_text->removed && pe_text->virtual_size) {
		pe_wr32(o + 4, pe_text->raw_size);
		pe_wr32(o + 20, pe_text->rva);
	}
	pe_wr32(o + 8, pe_size_of_initialized_data());
	if (!cfg.no_entry && pe_entry_sym && pe_sym_is_defined(pe_entry_sym))
		pe_wr32(o + 16, (uint32_t)pe_symbol_rva(pe_entry_sym));
	pe_wr64(o + 24, cfg.image_base);
	pe_wr32(o + 32, cfg.section_align);
	pe_wr32(o + 36, cfg.file_align);
	pe_wr16(o + 40, cfg.major_os);
	pe_wr16(o + 42, cfg.minor_os);
	pe_wr16(o + 44, cfg.major_image);
	pe_wr16(o + 46, cfg.minor_image);
	pe_wr16(o + 48, cfg.major_subsystem);
	pe_wr16(o + 50, cfg.minor_subsystem);
	pe_wr32(o + 56, pe_size_of_image);
	pe_wr32(o + 60, pe_size_of_headers);
	pe_wr16(o + 68, cfg.subsystem);
	uint16_t dc = 0;
	if (cfg.appcontainer)
		dc |= IMAGE_DLL_CHARACTERISTICS_APPCONTAINER;
	if (cfg.dynamic_base)
		dc |= IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE;
	if (cfg.high_entropy_va)
		dc |= IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA;
	if (cfg.no_bind)
		dc |= IMAGE_DLL_CHARACTERISTICS_NO_BIND;
	if (cfg.nx_compat)
		dc |= IMAGE_DLL_CHARACTERISTICS_NX_COMPAT;
	if (cfg.no_isolation)
		dc |= IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION;
	if (cfg.integrity_check)
		dc |= IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY;
	if (cfg.no_seh)
		dc |= IMAGE_DLL_CHARACTERISTICS_NO_SEH;
	if (cfg.ts_aware)
		dc |= IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE;
	pe_wr16(o + 70, dc);
	pe_wr64(o + 72, cfg.stack_reserve);
	pe_wr64(o + 80, cfg.stack_commit);
	pe_wr64(o + 88, cfg.heap_reserve);
	pe_wr64(o + 96, cfg.heap_commit);
	pe_wr32(o + 108, PE_DIR_COUNT);
	uint8_t *dir = o + 112;
#define PE_DIR(i, rva, size) do { pe_wr32(dir + 8 * (i), (rva)); pe_wr32(dir + 8 * (i) + 4, (size)); } while (0)
	if (pe_edata_start)
		PE_DIR(PE_DIR_EXPORT, pe_edata_start->rva, pe_edata_end->rva + pe_edata_end->size - pe_edata_start->rva);
	if (pe_import_table_start)
		PE_DIR(PE_DIR_IMPORT, pe_import_table_start->rva, pe_import_table_size);
	if (pe_iat_start)
		PE_DIR(PE_DIR_IAT, pe_iat_start->rva, pe_iat_size);
	if (pe_pdata_first)
		PE_DIR(PE_DIR_EXCEPTION, pe_pdata_first->rva, pe_pdata_last->rva + pe_pdata_last->size - pe_pdata_first->rva);
	if (!pe_reloc->removed && pe_reloc->virtual_size)
		PE_DIR(PE_DIR_BASERELOC, pe_reloc->rva, pe_reloc->virtual_size);
	pe_symbol *tls = pe_sym_find("_tls_used");
	if (tls && pe_sym_is_defined(tls))
		PE_DIR(PE_DIR_TLS, (uint32_t)pe_symbol_rva(tls), PE_TLS_DIRECTORY64_SIZE);
	if (pe_debug_directory)
		PE_DIR(PE_DIR_DEBUG, pe_debug_directory->rva, pe_debug_directory->size);
	pe_symbol *lc = pe_sym_find("_load_config_used");
	if (lc && lc->kind == PE_SYM_REGULAR && lc->chunk) {
		pe_chunk *c = lc->chunk;
		uint64_t off = lc->value;
		if (!c->has_data || off + 4 > c->size)
			pe_error("_load_config_used is malformed");
		else {
			uint32_t size = pe_rd32(c->data + off);
			if (off + size > c->size)
				pe_error("_load_config_used is too large");
			else
				PE_DIR(PE_DIR_LOAD_CONFIG, (uint32_t)pe_symbol_rva(lc), size);
		}
	}
#undef PE_DIR
	uint8_t *sh = dir + 8 * PE_DIR_COUNT;
	for (uint32_t i = 0; i < pe_output_count; i++, sh += COFF_SECTION_HEADER_SIZE) {
		pe_output *s = pe_outputs[i];
		if (s->strtab_offset) {
			char nm[16];
			snprintf(nm, sizeof nm, "/%u", s->strtab_offset);
			memcpy(sh, nm, strlen(nm) < 8 ? strlen(nm) : 8);
		} else
			memcpy(sh, s->name, strlen(s->name) < 8 ? strlen(s->name) : 8);
		pe_wr32(sh + 8, s->virtual_size);
		pe_wr32(sh + 12, s->rva);
		pe_wr32(sh + 16, s->raw_size);
		pe_wr32(sh + 20, s->file_offset);
		pe_wr32(sh + 36, s->chars);
	}
	if (pe_out_symbol_count || pe_strtab_size) {
		uint8_t *st = img + pe_symtab_offset;
		for (uint32_t i = 0; i < pe_out_symbol_count; i++)
			memcpy(st + (size_t)i * COFF_SYMBOL_SIZE, pe_out_symbols[i].raw, COFF_SYMBOL_SIZE);
		st += (size_t)pe_out_symbol_count * COFF_SYMBOL_SIZE;
		pe_wr32(st, (uint32_t)pe_strtab_size + 4);
		if (pe_strtab_size)
			memcpy(st + 4, pe_strtab, pe_strtab_size);
	}
}

static int pe_pdata_cmp(const void *a, const void *b)
{
	uint32_t x = pe_rd32(a), y = pe_rd32(b);
	return x < y ? -1 : x > y;
}

static uint8_t *pe_chunk_buffer(uint8_t *img, pe_chunk *c)
{
	return img + c->os->file_offset + (c->rva - c->os->rva);
}

static void pe_map_write(void)
{
	FILE *m = NULL;
	int fd = openat(pe_dirfd, cfg.map_file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd >= 0)
		m = fdopen(fd, "w");
	if (!m) {
		pe_error("cannot open %s: %s", cfg.map_file, strerror(errno));
		if (fd >= 0)
			close(fd);
		return;
	}
	fprintf(m, "Address  Size     Align Out     In      Symbol\n");
	for (uint32_t i = 0; i < pe_output_count; i++) {
		pe_output *o = pe_outputs[i];
		fprintf(m, "%08x %08x %5u %s\n", o->rva, o->virtual_size, cfg.section_align, o->name);
		for (uint32_t k = 0; k < o->nchunks; k++) {
			pe_chunk *c = o->chunks[k];
			if (c->kind != PE_CHUNK_SECTION)
				continue;
			fprintf(m, "%08x %08x %5u         %s:(%s)\n", c->rva, c->size, c->align, c->file->path, c->name);
		}
	}
	fclose(m);
}

static bool pe_write_run(void)
{
	pe_symbol_table();
	if (pe_file_size > UINT32_MAX) {
		pe_error("image size (%llu) exceeds maximum allowable size (4294967295)", (unsigned long long)pe_file_size);
		return false;
	}
	uint8_t *img = calloc(1, pe_file_size ? pe_file_size : 1);
	if (!img)
		pe_oom();
	/* sections: code gaps are int3 */
	for (uint32_t i = 0; i < pe_output_count; i++) {
		pe_output *o = pe_outputs[i];
		uint8_t *sb = img + o->file_offset;
		if (o->raw_size && (o->chars & IMAGE_SCN_CNT_CODE))
			memset(sb, 0xcc, o->raw_size);
		for (uint32_t k = 0; k < o->nchunks; k++) {
			pe_chunk *c = o->chunks[k];
			if (!c->has_data || !o->raw_size)
				continue;
			pe_chunk_write(c, sb + (c->rva - o->rva), img);
		}
	}
	pe_headers_write(img);
	/* .pdata: by function start */
	if (pe_pdata_first && pe_pdata_first->os && !pe_pdata_first->os->removed) {
		uint8_t *b = pe_chunk_buffer(img, pe_pdata_first);
		uint8_t *e = pe_chunk_buffer(img, pe_pdata_last) + pe_pdata_last->size;
		if ((e - b) % 12)
			pe_error("unexpected .pdata size: %ld is not a multiple of 12", (long)(e - b));
		else
			qsort(b, (size_t)(e - b) / 12, 12, pe_pdata_cmp);
	}
	/* the TLS directory's alignment field */
	if (pe_tls_alignment) {
		pe_symbol *tls = pe_sym_find("_tls_used");
		pe_output *os = tls && pe_sym_is_defined(tls) ? pe_symbol_output(tls) : NULL;
		if (os) {
			uint64_t off = pe_symbol_rva(tls) - os->rva;
			if (off + PE_TLS_DIRECTORY64_SIZE > os->raw_size)
				pe_error("_tls_used sym is malformed");
			else {
				uint8_t *d = img + os->file_offset + off;
				uint32_t a = 0, al = pe_tls_alignment;
				while ((1u << a) < al)
					a++;
				uint32_t chars = pe_rd32(d + 36);
				pe_wr32(d + 36, (chars & ~(uint32_t)IMAGE_SCN_ALIGN_MASK) | ((a + 1) << 20));
			}
		}
	}
	/* build id and time stamps */
	uint32_t timestamp = 0;
	if (cfg.insert_timestamp) {
		const char *e = getenv("SOURCE_DATE_EPOCH");
		if (e) {
			uint64_t v;
			if (!pe_parse_u64(e, &v) || v > UINT32_MAX)
				pe_error("invalid SOURCE_DATE_EPOCH timestamp: %s.  Expected 32-bit integer", e);
			timestamp = (uint32_t)v;
		} else
			timestamp = (uint32_t)time(NULL);
	}
	if (pe_cv_record && !pe_cv_record->os->removed) {
		uint8_t *cv = pe_chunk_buffer(img, pe_cv_record);
		uint64_t h = cfg.build_id ? vdigest_xxh3(img, pe_file_size) : 0;
		if (cfg.build_id) {
			memcpy(cv, "RSDS", 4);
			pe_wr64(cv + 4, h);
			memcpy(cv + 12, "LLD PDB.", 8);
			pe_wr32(cv + 20, 1);
		}
	}
	if (pe_debug_directory && !pe_debug_directory->os->removed)
		pe_wr32(pe_chunk_buffer(img, pe_debug_directory) + 4, timestamp);
	pe_wr32(img + PE_DOS_HEADER_SIZE + sizeof pe_dos_program + 4 + 4, timestamp);
	if (pe_error_count) {
		free(img);
		return false;
	}
	int fd = openat(pe_dirfd, cfg.output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);
	bool ok = fd >= 0;
	size_t done = 0;
	while (ok && done < pe_file_size) {
		ssize_t w = write(fd, img + done, pe_file_size - done);
		if (w <= 0)
			ok = false;
		else
			done += (size_t)w;
	}
	if (!ok)
		pe_error("cannot write %s: %s", cfg.output, strerror(errno));
	if (fd >= 0)
		close(fd);
	free(img);
	if (cfg.map_file)
		pe_map_write();
	return ok;
}
