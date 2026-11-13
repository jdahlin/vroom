/*
 * Layout, as lld's Writer::run up to the file: import tables, partial and
 * output sections, import thunks, the misc chunks (local imports, the debug
 * directory and build id record, pseudo relocations, constructor lists),
 * the export table, section merging, the order of output sections, and
 * addresses (with the base relocation blocks made when .reloc's turn comes).
 */

static pe_partial **pe_partials;	/* sorted by (name, chars) */
static uint32_t pe_partial_count, pe_partial_capacity;
static pe_output **pe_outputs;
static uint32_t pe_output_count, pe_output_capacity;
static pe_output *pe_text, *pe_rdata, *pe_buildid, *pe_data, *pe_pdata, *pe_edata, *pe_reloc, *pe_ctors, *pe_dtors;
static pe_chunk *pe_pdata_first, *pe_pdata_last;
static pe_chunk *pe_import_table_start, *pe_iat_start;
static uint32_t pe_import_table_size, pe_iat_size;
static pe_chunk *pe_edata_start, *pe_edata_end;
static pe_chunk *pe_debug_directory, *pe_cv_record;
static uint32_t pe_tls_alignment;
static uint32_t pe_size_of_headers, pe_size_of_image;
static uint64_t pe_file_size;
/* idata */
static pe_chunk **pe_idata_dirs, **pe_idata_lookups, **pe_idata_addresses, **pe_idata_hints, **pe_idata_names;
static uint32_t pe_ndirs, pe_nlookups, pe_naddresses, pe_nhints, pe_nnames;
static uint32_t pe_cdirs, pe_clookups, pe_caddresses, pe_chints, pe_cnames;

typedef struct { pe_symbol *sym; pe_chunk *target; uint32_t offset; uint32_t flags; } pe_pseudo_reloc;

static void pe_layout_reset(void)
{
	for (uint32_t i = 0; i < pe_partial_count; i++)
		free(pe_partials[i]->chunks);
	free(pe_partials);
	pe_partials = NULL;
	pe_partial_count = pe_partial_capacity = 0;
	for (uint32_t i = 0; i < pe_output_count; i++)
		free(pe_outputs[i]->chunks);
	free(pe_outputs);
	pe_outputs = NULL;
	pe_output_count = pe_output_capacity = 0;
	pe_text = pe_rdata = pe_buildid = pe_data = pe_pdata = pe_edata = pe_reloc = pe_ctors = pe_dtors = NULL;
	pe_pdata_first = pe_pdata_last = NULL;
	pe_import_table_start = pe_iat_start = NULL;
	pe_import_table_size = pe_iat_size = 0;
	pe_edata_start = pe_edata_end = NULL;
	pe_debug_directory = pe_cv_record = NULL;
	pe_tls_alignment = 0;
	pe_size_of_headers = pe_size_of_image = 0;
	pe_file_size = 0;
	free(pe_idata_dirs);
	free(pe_idata_lookups);
	free(pe_idata_addresses);
	free(pe_idata_hints);
	free(pe_idata_names);
	pe_idata_dirs = pe_idata_lookups = pe_idata_addresses = pe_idata_hints = pe_idata_names = NULL;
	pe_ndirs = pe_nlookups = pe_naddresses = pe_nhints = pe_nnames = 0;
	pe_cdirs = pe_clookups = pe_caddresses = pe_chints = pe_cnames = 0;
}

static int pe_partial_cmp(const char *name, uint32_t chars, const pe_partial *p)
{
	int c = strcmp(name, p->name);
	if (c)
		return c;
	return chars < p->chars ? -1 : chars > p->chars ? 1 : 0;
}

/* lld createPartialSection: find or insert, keeping the list sorted */
static pe_partial *pe_partial_get(const char *name, uint32_t chars)
{
	uint32_t lo = 0, hi = pe_partial_count;
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		int c = pe_partial_cmp(name, chars, pe_partials[mid]);
		if (!c)
			return pe_partials[mid];
		if (c < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	pe_partial *p = pe_alloc(sizeof *p);
	p->name = name;
	p->chars = chars;
	PE_PUSH(pe_partials, pe_partial_count, pe_partial_capacity, p);
	memmove(&pe_partials[lo + 1], &pe_partials[lo], (pe_partial_count - 1 - lo) * sizeof *pe_partials);
	pe_partials[lo] = p;
	return p;
}

static pe_partial *pe_partial_find(const char *name, uint32_t chars)
{
	for (uint32_t i = 0; i < pe_partial_count; i++)
		if (!pe_partial_cmp(name, chars, pe_partials[i]))
			return pe_partials[i];
	return NULL;
}

static void pe_partial_add(pe_partial *p, pe_chunk *c)
{
	PE_PUSH(p->chunks, p->nchunks, p->capacity, c);
}

static void pe_output_add(pe_output *o, pe_chunk *c)
{
	PE_PUSH(o->chunks, o->nchunks, o->capacity, c);
	c->os = o;
}

static pe_output *pe_output_get(const char *name, uint32_t chars)
{
	for (uint32_t i = 0; i < pe_output_count; i++)
		if (!strcmp(pe_outputs[i]->name, name) && pe_outputs[i]->chars == chars)
			return pe_outputs[i];
	pe_output *o = pe_alloc(sizeof *o);
	o->name = name;
	o->chars = chars;
	PE_PUSH(pe_outputs, pe_output_count, pe_output_capacity, o);
	return o;
}

static pe_output *pe_output_find(const char *name)
{
	for (uint32_t i = 0; i < pe_output_count; i++)
		if (!strcmp(pe_outputs[i]->name, name))
			return pe_outputs[i];
	return NULL;
}

/* ".text$foo" -> ".text"; ".ctors.01234" -> ".ctors" */
static const char *pe_output_section_name(const char *name)
{
	size_t n = strcspn(name, "$");
	const char *dot = n > 1 ? memchr(name + 1, '.', n - 1) : NULL;
	if (dot)
		n = (size_t)(dot - name);
	return name[n] ? pe_strndup(name, n) : name;
}

static bool pe_strip_suffix(const pe_chunk *c)
{
	if (c->kind != PE_CHUNK_SECTION || !c->is_comdat || !strchr(c->name, '$'))
		return false;
	static const char *const p[] = { ".text$", ".data$", ".rdata$", ".pdata$", ".xdata$", ".eh_frame$" };
	for (size_t i = 0; i < 6; i++)
		if (!strncmp(c->name, p[i], strlen(p[i])))
			return true;
	return false;
}

static pe_chunk *pe_synth(uint8_t kind, uint32_t size, uint32_t align)
{
	pe_chunk *c = pe_chunk_new(kind);
	c->size = size;
	c->align = align;
	return c;
}

/* ---------------- .idata (lld IdataContents) ---------------- */

static int pe_import_name_cmp(const void *a, const void *b)
{
	const pe_file *x = *(pe_file *const *)a, *y = *(pe_file *const *)b;
	const char *nx = x->imp_sym->name + 6, *ny = y->imp_sym->name + 6;
	int c = strcmp(nx, ny);
	if (c)
		return c;
	return x->order < y->order ? -1 : x->order > y->order;
}

static bool pe_idata_create(void)
{
	/* live import files, grouped by DLL (first appearance, case-insensitive), names sorted */
	const char **dlls = NULL;
	uint32_t ndll = 0, capdll = 0;
	for (uint32_t i = 0; i < pe_import_count; i++) {
		pe_file *f = pe_imports[i];
		if (!f->live || !f->imp_sym || f->imp_sym->kind != PE_SYM_IMPORT_DATA || f->imp_sym->file != f)
			continue;
		bool known = false;
		for (uint32_t k = 0; k < ndll && !known; k++)
			known = !strcasecmp(dlls[k], f->dll_name);
		if (!known)
			PE_PUSH(dlls, ndll, capdll, f->dll_name);
	}
	pe_file **group = malloc((pe_import_count + 1) * sizeof *group);
	if (!group)
		pe_oom();
	for (uint32_t d = 0; d < ndll; d++) {
		uint32_t n = 0;
		for (uint32_t i = 0; i < pe_import_count; i++) {
			pe_file *f = pe_imports[i];
			if (f->live && f->imp_sym && f->imp_sym->kind == PE_SYM_IMPORT_DATA && f->imp_sym->file == f &&
			    !strcasecmp(f->dll_name, dlls[d]))
				group[n++] = f;
		}
		qsort(group, n, sizeof *group, pe_import_name_cmp);
		uint32_t base = pe_naddresses;
		for (uint32_t i = 0; i < n; i++) {
			pe_file *f = group[i];
			pe_chunk *lk, *ad;
			if (!f->external_name[0]) {
				lk = pe_synth(PE_CHUNK_ORDINAL_ONLY, 8, 8);
				ad = pe_synth(PE_CHUNK_ORDINAL_ONLY, 8, 8);
				lk->value = ad->value = f->hint;
			} else {
				size_t ln = strlen(f->external_name);
				pe_chunk *h = pe_synth(PE_CHUNK_HINT_NAME, (uint32_t)pe_align_up(ln + 3, 2), 2);
				h->str = f->external_name;
				h->value = f->hint;
				lk = pe_synth(PE_CHUNK_LOOKUP, 8, 8);
				ad = pe_synth(PE_CHUNK_LOOKUP, 8, 8);
				lk->target = ad->target = h;
				PE_PUSH(pe_idata_hints, pe_nhints, pe_chints, h);
			}
			PE_PUSH(pe_idata_lookups, pe_nlookups, pe_clookups, lk);
			PE_PUSH(pe_idata_addresses, pe_naddresses, pe_caddresses, ad);
		}
		pe_chunk *nl = pe_synth(PE_CHUNK_NULL, 8, 8);
		nl->has_data = false;
		pe_chunk *na = pe_synth(PE_CHUNK_NULL, 8, 8);
		na->has_data = false;
		PE_PUSH(pe_idata_lookups, pe_nlookups, pe_clookups, nl);
		PE_PUSH(pe_idata_addresses, pe_naddresses, pe_caddresses, na);
		for (uint32_t i = 0; i < n; i++) {
			group[i]->location = pe_idata_addresses[base + i];
			group[i]->imp_sym->chunk = group[i]->location;
		}
		pe_chunk *name = pe_synth(PE_CHUNK_STRING, (uint32_t)strlen(group[0]->dll_name) + 1, 1);
		name->str = group[0]->dll_name;
		PE_PUSH(pe_idata_names, pe_nnames, pe_cnames, name);
		pe_chunk *dir = pe_synth(PE_CHUNK_IMPORT_DIRECTORY, PE_IMPORT_DIRECTORY_SIZE, 4);
		dir->target = pe_idata_lookups[base];
		dir->target2 = pe_idata_addresses[base];
		dir->target3 = name;
		PE_PUSH(pe_idata_dirs, pe_ndirs, pe_cdirs, dir);
	}
	pe_chunk *term = pe_synth(PE_CHUNK_NULL, PE_IMPORT_DIRECTORY_SIZE, 4);
	term->has_data = false;
	PE_PUSH(pe_idata_dirs, pe_ndirs, pe_cdirs, term);
	free(group);
	free(dlls);
	return ndll != 0;
}

static bool pe_any_live_import(void)
{
	for (uint32_t i = 0; i < pe_import_count; i++) {
		pe_file *f = pe_imports[i];
		if (f->live && f->imp_sym && f->imp_sym->kind == PE_SYM_IMPORT_DATA && f->imp_sym->file == f)
			return true;
	}
	return false;
}

/* sortCRTSectionChunks: chunks of one object by section number (lld's comparator, stably) */
static bool pe_crt_before(const pe_chunk *a, const pe_chunk *b)
{
	return a->kind == PE_CHUNK_SECTION && b->kind == PE_CHUNK_SECTION && a->file == b->file &&
	       a->section_index < b->section_index;
}

static void pe_crt_sort(pe_chunk **v, uint32_t n)
{
	for (uint32_t i = 1; i < n; i++) {
		pe_chunk *x = v[i];
		uint32_t j = i;
		while (j > 0 && pe_crt_before(x, v[j - 1])) {
			v[j] = v[j - 1];
			j--;
		}
		v[j] = x;
	}
}

static bool pe_idata_chunk_before(const pe_chunk *s, const pe_chunk *t)
{
	bool a = s->kind == PE_CHUNK_SECTION, b = t->kind == PE_CHUNK_SECTION;
	if (!a || !b)
		return a && !b;
	const char *pa = s->file->parent ? s->file->parent : "", *pb = t->file->parent ? t->file->parent : "";
	int c = strcmp(pa, pb);
	if (c)
		return c < 0;
	return strcmp(s->file->path, t->file->path) < 0;
}

static void pe_create_sections(void)
{
	const uint32_t data = IMAGE_SCN_CNT_INITIALIZED_DATA, bss = IMAGE_SCN_CNT_UNINITIALIZED_DATA;
	const uint32_t code = IMAGE_SCN_CNT_CODE, r = IMAGE_SCN_MEM_READ, w = IMAGE_SCN_MEM_WRITE,
		       x = IMAGE_SCN_MEM_EXECUTE, discardable = IMAGE_SCN_MEM_DISCARDABLE;
	/* link.exe's section order */
	pe_text = pe_output_get(".text", code | r | x);
	pe_output_get(".bss", bss | r | w);
	pe_rdata = pe_output_get(".rdata", data | r);
	pe_buildid = pe_output_get(".buildid", data | r);
	pe_data = pe_output_get(".data", data | r | w);
	pe_pdata = pe_output_get(".pdata", data | r);
	pe_output_get(".idata", data | r);
	pe_edata = pe_output_get(".edata", data | r);
	pe_output_get(".didat", data | r);
	pe_output_get(".rsrc", data | r);
	pe_reloc = pe_output_get(".reloc", data | discardable | r);
	pe_ctors = pe_output_get(".ctors", data | r | w);
	pe_dtors = pe_output_get(".dtors", data | r | w);

	for (uint32_t fi = 0; fi < pe_object_count; fi++) {
		pe_file *f = pe_objects[fi];
		for (uint32_t ci = 0; ci < f->nchunks; ci++) {
			pe_chunk *c = f->chunks[ci];
			if (!c->live)
				continue;
			const char *name = c->name;
			if (pe_strip_suffix(c))
				name = pe_strndup(name, strcspn(name, "$"));
			if (!strncmp(name, ".tls", 4) && c->align > pe_tls_alignment)
				pe_tls_alignment = c->align;
			pe_partial_add(pe_partial_get(name, c->chars), c);
		}
	}
	/* GNU import libraries: .idata$N as read-only data, sorted by library/object */
	bool has_idata = false;
	uint32_t rdata_chars = data | r;
	for (bool again = true; again;) {
		again = false;
		for (uint32_t i = 0; i < pe_partial_count && !again; i++) {
			pe_partial *p = pe_partials[i];
			if (strncmp(p->name, ".idata", 6) || (p->name[6] && p->name[6] != '$') || p->chars == rdata_chars || !p->nchunks)
				continue;
			pe_partial *dst = pe_partial_get(p->name, rdata_chars);
			for (uint32_t k = 0; k < p->nchunks; k++)
				pe_partial_add(dst, p->chunks[k]);
			p->nchunks = 0;
			again = true;	/* (the list may have shifted) */
		}
	}
	for (uint32_t i = 0; i < pe_partial_count; i++) {
		pe_partial *p = pe_partials[i];
		if (strncmp(p->name, ".idata", 6))
			continue;
		if (p->nchunks)
			has_idata = true;
		for (uint32_t a = 1; a < p->nchunks; a++) {
			pe_chunk *v = p->chunks[a];
			uint32_t b = a;
			while (b > 0 && pe_idata_chunk_before(v, p->chunks[b - 1])) {
				p->chunks[b] = p->chunks[b - 1];
				b--;
			}
			p->chunks[b] = v;
		}
	}
	if (pe_any_live_import())
		has_idata = true;
	if (has_idata) {
		pe_idata_create();
		/* lld addSyntheticIdata (only when there is something to import) */
		struct { const char *n; pe_chunk **v; uint32_t c; } add[] = {
			{ ".idata$2", pe_idata_dirs, pe_ndirs }, { ".idata$4", pe_idata_lookups, pe_nlookups },
			{ ".idata$5", pe_idata_addresses, pe_naddresses }, { ".idata$6", pe_idata_hints, pe_nhints },
			{ ".idata$7", pe_idata_names, pe_nnames },
		};
		for (size_t i = 0; i < 5; i++) {
			if (!strcmp(add[i].n, ".idata$6") && !add[i].c)
				continue;
			if (!add[i].c && strcmp(add[i].n, ".idata$2") && strcmp(add[i].n, ".idata$4") && strcmp(add[i].n, ".idata$5") && strcmp(add[i].n, ".idata$7"))
				continue;
			pe_partial *p = pe_partial_get(add[i].n, rdata_chars);
			for (uint32_t k = 0; k < add[i].c; k++)
				pe_partial_add(p, add[i].v[k]);
		}
		/* locateImportTables */
		pe_partial *dirs = pe_partial_find(".idata$2", rdata_chars);
		if (dirs) {
			if (dirs->nchunks)
				pe_import_table_start = dirs->chunks[0];
			for (uint32_t k = 0; k < dirs->nchunks; k++)
				pe_import_table_size += dirs->chunks[k]->size;
		}
		pe_partial *iat = pe_partial_find(".idata$5", rdata_chars);
		if (iat) {
			if (iat->nchunks)
				pe_iat_start = iat->chunks[0];
			for (uint32_t k = 0; k < iat->nchunks; k++)
				pe_iat_size += iat->chunks[k]->size;
		}
	}
	for (uint32_t i = 0; i < pe_partial_count; i++) {
		pe_partial *p = pe_partials[i];
		const char *name = pe_output_section_name(p->name);
		uint32_t chars = p->chars;
		if (!strcmp(name, ".CRT")) {
			chars = data | r;
			pe_log("Processing section %s -> %s", p->name, name);
			pe_crt_sort(p->chunks, p->nchunks);
		}
		pe_output *o = pe_output_get(name, chars);
		for (uint32_t k = 0; k < p->nchunks; k++)
			pe_output_add(o, p->chunks[k]);
	}
	/* discardable sections last, .debug_* after the others, .rsrc before them */
	pe_output **sorted = malloc(pe_output_count * sizeof *sorted);
	if (!sorted)
		pe_oom();
	uint32_t n = 0;
	for (int cls = 0; cls < 4; cls++)
		for (uint32_t i = 0; i < pe_output_count; i++) {
			pe_output *o = pe_outputs[i];
			int c = (o->chars & IMAGE_SCN_MEM_DISCARDABLE) ? (!strncmp(o->name, ".debug_", 7) ? 3 : 2) :
				!strcmp(o->name, ".rsrc") ? 1 : 0;
			if (c == cls)
				sorted[n++] = o;
		}
	memcpy(pe_outputs, sorted, n * sizeof *sorted);
	free(sorted);
}

static void pe_append_import_thunks(void)
{
	for (uint32_t i = 0; i < pe_import_count; i++) {
		pe_file *f = pe_imports[i];
		if (!f->live || !f->thunk_sym || !f->thunk_live)
			continue;
		if (f->thunk_sym->kind != PE_SYM_IMPORT_THUNK) {
			pe_error("%s was replaced", f->thunk_sym->name);
			continue;
		}
		pe_output_add(pe_text, f->thunk_sym->chunk);
	}
}

static void pe_runtime_pseudo_relocs(void)
{
	pe_pseudo_reloc *rels = NULL;
	uint32_t n = 0, cap = 0;
	for (uint32_t fi = 0; fi < pe_object_count; fi++) {
		pe_file *f = pe_objects[fi];
		for (uint32_t ci = 0; ci < f->nchunks; ci++) {
			pe_chunk *c = f->chunks[ci];
			if (c->kind != PE_CHUNK_SECTION || !c->live || (c->header_chars & IMAGE_SCN_MEM_DISCARDABLE))
				continue;
			for (uint32_t r = 0; r < c->nrelocs; r++) {
				const uint8_t *rel = c->relocs + (size_t)r * COFF_RELOCATION_SIZE;
				uint32_t ix = pe_rd32(rel + 4);
				pe_symbol *t = ix < f->nsyms ? f->syms[ix] : NULL;
				if (!t || !t->runtime_pseudo_reloc || !pe_sym_is_defined(t) || !t->chunk)
					continue;
				uint16_t type = pe_rd16(rel + 8);
				int bits = type == IMAGE_REL_AMD64_ADDR64 ? 64 :
					   (type == IMAGE_REL_AMD64_ADDR32 || type == IMAGE_REL_AMD64_ADDR32NB ||
					    (type >= IMAGE_REL_AMD64_REL32 && type <= IMAGE_REL_AMD64_REL32_5)) ? 32 : 0;
				if (!bits) {
					pe_error("unable to automatically import from %s with relocation type %u in %s", t->name, type, pe_file_name(f));
					continue;
				}
				if (bits < 64)
					pe_warn("runtime pseudo relocation in %s against symbol %s is too narrow (only %d bits wide); this can fail at runtime depending on memory layout",
						pe_file_name(f), t->name, bits);
				pe_pseudo_reloc pr = { t, c, pe_rd32(rel), (uint32_t)bits };
				PE_PUSH(rels, n, cap, pr);
			}
		}
	}
	if (!cfg.pseudo_relocs) {
		for (uint32_t i = 0; i < n; i++)
			pe_error("automatic dllimport of %s in %s requires pseudo relocations", rels[i].sym->name, pe_file_name(rels[i].target->file));
		free(rels);
		return;
	}
	if (n) {
		pe_log("Writing %u runtime pseudo relocations", n);
		if (!pe_sym_find("_pei386_runtime_relocator"))
			pe_error("output image has runtime pseudo relocations, but the function _pei386_runtime_relocator is missing; it is needed for fixing the relocations at runtime");
	}
	pe_chunk *table = pe_synth(PE_CHUNK_PSEUDO_RELOC_TABLE, n ? 12 + 12 * n : 0, 4);
	table->aux = rels;
	table->aux_count = n;
	pe_output_add(pe_rdata, table);
	pe_chunk *end = pe_synth(PE_CHUNK_EMPTY, 0, 1);
	pe_output_add(pe_rdata, end);
	pe_symbol *head = pe_sym_find("__RUNTIME_PSEUDO_RELOC_LIST__");
	pe_symbol *tail = pe_sym_find("__RUNTIME_PSEUDO_RELOC_LIST_END__");
	if (head) {
		pe_sym_replace(head, PE_SYM_SYNTHETIC, NULL);
		head->chunk = table;
	}
	if (tail) {
		pe_sym_replace(tail, PE_SYM_SYNTHETIC, NULL);
		tail->chunk = end;
	}
}

static void pe_insert_at_start(pe_output *o, pe_chunk *c)
{
	PE_PUSH(o->chunks, o->nchunks, o->capacity, c);
	memmove(&o->chunks[1], &o->chunks[0], (o->nchunks - 1) * sizeof *o->chunks);
	o->chunks[0] = c;
	c->os = o;
}

static void pe_ctor_dtor_lists(void)
{
	pe_chunk *ch = pe_synth(PE_CHUNK_ABSOLUTE_POINTER, 8, 8), *ce = pe_synth(PE_CHUNK_ABSOLUTE_POINTER, 8, 8);
	pe_chunk *dh = pe_synth(PE_CHUNK_ABSOLUTE_POINTER, 8, 8), *de = pe_synth(PE_CHUNK_ABSOLUTE_POINTER, 8, 8);
	ch->value = dh->value = UINT64_MAX;
	pe_insert_at_start(pe_ctors, ch);
	pe_output_add(pe_ctors, ce);
	pe_insert_at_start(pe_dtors, dh);
	pe_output_add(pe_dtors, de);
	pe_symbol *cs = pe_sym_find("__CTOR_LIST__"), *ds = pe_sym_find("__DTOR_LIST__");
	if (cs) {
		pe_sym_replace(cs, PE_SYM_SYNTHETIC, NULL);
		cs->chunk = ch;
	}
	if (ds) {
		pe_sym_replace(ds, PE_SYM_SYNTHETIC, NULL);
		ds->chunk = dh;
	}
}

static void pe_misc_chunks(void)
{
	/* (createGuardCFTables without /guard:cf) the image is instrumented if any object was built so */
	for (uint32_t i = 0; i < pe_object_count; i++)
		if (pe_objects[i]->feat00 & 0x800) {
			pe_symbol *g = pe_sym_find("__guard_flags");
			if (g && g->kind == PE_SYM_ABSOLUTE)
				g->value = 0x100;
			break;
		}
	for (uint32_t i = 0; i < pe_local_import_count; i++)
		pe_output_add(pe_rdata, pe_local_import_chunks[i]);
	if (cfg.build_id || cfg.debug) {
		pe_debug_directory = pe_synth(PE_CHUNK_DEBUG_DIRECTORY, PE_DEBUG_DIRECTORY_SIZE, 4);
		pe_output_add(pe_buildid, pe_debug_directory);
		pe_cv_record = pe_synth(PE_CHUNK_CODEVIEW_RECORD, 25, 4);
		pe_symbol *b = pe_sym_find("__buildid");
		if (b) {
			pe_sym_replace(b, PE_SYM_SYNTHETIC, NULL);
			b->chunk = pe_cv_record;
			b->value = 4;
		}
		pe_output_add(pe_buildid, pe_cv_record);
	}
	if (cfg.auto_import)
		pe_runtime_pseudo_relocs();
	pe_ctor_dtor_lists();
}

/* ---------------- exports (.edata) ---------------- */

static void pe_export_table(void)
{
	if (pe_edata->nchunks) {
		pe_edata_start = pe_edata->chunks[0];
		pe_edata_end = pe_edata->chunks[pe_edata->nchunks - 1];
		return;
	}
	if (!cfg.nexports)
		return;
	uint32_t max = 0, nnames = 0;
	for (uint32_t i = 0; i < cfg.nexports; i++) {
		if (cfg.exports[i].ordinal > max)
			max = cfg.exports[i].ordinal;
		if (!cfg.exports[i].no_name)
			nnames++;
	}
	pe_chunk *dll = pe_synth(PE_CHUNK_STRING, (uint32_t)strlen(pe_basename(cfg.output)) + 1, 1);
	dll->str = pe_basename(cfg.output);
	pe_chunk *addr = pe_synth(PE_CHUNK_EXPORT_ADDRESS_TABLE, max * 4, 4);
	pe_chunk *names = pe_synth(PE_CHUNK_EXPORT_NAME_POINTERS, nnames * 4, 4);
	pe_chunk *ords = pe_synth(PE_CHUNK_EXPORT_ORDINALS, nnames * 2, 2);
	pe_chunk *dir = pe_synth(PE_CHUNK_EXPORT_DIRECTORY, PE_EXPORT_DIRECTORY_SIZE, 4);
	pe_chunk **strs = pe_alloc((nnames + 1) * sizeof *strs);
	uint32_t k = 0;
	for (uint32_t i = 0; i < cfg.nexports; i++)
		if (!cfg.exports[i].no_name) {
			pe_chunk *s = pe_synth(PE_CHUNK_STRING, (uint32_t)strlen(cfg.exports[i].name) + 1, 1);
			s->str = cfg.exports[i].name;
			strs[k++] = s;
		}
	dir->target = addr;
	dir->target2 = names;
	dir->target3 = ords;
	dir->sym = NULL;
	dir->aux = dll;
	dir->value = max;
	dir->aux_count = nnames;
	names->aux = strs;
	names->aux_count = nnames;
	pe_output_add(pe_edata, dir);
	pe_output_add(pe_edata, dll);
	pe_output_add(pe_edata, addr);
	pe_output_add(pe_edata, names);
	pe_output_add(pe_edata, ords);
	for (uint32_t i = 0; i < nnames; i++)
		pe_output_add(pe_edata, strs[i]);
	pe_edata_start = dir;
	pe_edata_end = pe_edata->chunks[pe_edata->nchunks - 1];
}

/* ---------------- merging, addresses ---------------- */

static void pe_merge_sections(void)
{
	if (pe_pdata->nchunks) {
		pe_pdata_first = pe_pdata->chunks[0];
		pe_pdata_last = pe_pdata->chunks[pe_pdata->nchunks - 1];
	}
	/* lld's defaults (a std::map: in key order) */
	static const struct { const char *from, *to; } merges[] = {
		{ ".00cfg", ".rdata" }, { ".CRT", ".rdata" }, { ".bss", ".data" }, { ".ctors", ".rdata" },
		{ ".didat", ".rdata" }, { ".dtors", ".rdata" }, { ".edata", ".rdata" }, { ".idata", ".rdata" },
		{ ".xdata", ".rdata" },
	};
	for (size_t i = 0; i < sizeof merges / sizeof merges[0]; i++) {
		pe_output *from = pe_output_find(merges[i].from);
		if (!from)
			continue;
		pe_output *to = pe_output_find(merges[i].to);
		if (!to) {
			from->name = merges[i].to;
			continue;
		}
		for (uint32_t k = 0; k < from->nchunks; k++)
			pe_output_add(to, from->chunks[k]);
		from->nchunks = 0;
	}
}

static void pe_remove_unused_sections(void)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < pe_output_count; i++)
		if (pe_outputs[i] == pe_reloc || pe_outputs[i]->nchunks)
			pe_outputs[n++] = pe_outputs[i];
	pe_output_count = n;
}

typedef struct { uint32_t rva; uint8_t type; } pe_baserel;

static int pe_baserel_cmp(const void *a, const void *b)
{
	const pe_baserel *x = a, *y = b;
	return x->rva < y->rva ? -1 : x->rva > y->rva;
}

static void pe_baserel_blocks(pe_baserel *v, uint32_t n)
{
	if (!n)
		return;
	qsort(v, n, sizeof *v, pe_baserel_cmp);
	uint32_t i = 0;
	while (i < n) {
		uint32_t page = v[i].rva & ~0xfffu, j = i;
		while (j < n && (v[j].rva & ~0xfffu) == page)
			j++;
		uint32_t size = (uint32_t)pe_align_up((j - i) * 2 + 8, 4);
		pe_chunk *c = pe_synth(PE_CHUNK_BASEREL_BLOCK, size, 4);
		c->bytes = pe_alloc(size);
		pe_wr32(c->bytes, page);
		pe_wr32(c->bytes + 4, size);
		for (uint32_t k = i; k < j; k++)
			pe_wr16(c->bytes + 8 + (k - i) * 2, (uint16_t)(v[k].type << 12 | (v[k].rva - page)));
		pe_output_add(pe_reloc, c);
		i = j;
	}
}

static void pe_add_baserels(void)
{
	if (!cfg.relocatable)
		return;
	pe_reloc->nchunks = 0;
	pe_baserel *v = NULL;
	uint32_t n = 0, cap = 0;
	for (uint32_t oi = 0; oi < pe_output_count; oi++) {
		pe_output *o = pe_outputs[oi];
		if (o->chars & IMAGE_SCN_MEM_DISCARDABLE)
			continue;
		for (uint32_t ci = 0; ci < o->nchunks; ci++) {
			pe_chunk *c = o->chunks[ci];
			if (c->kind != PE_CHUNK_SECTION)
				continue;
			for (uint32_t r = 0; r < c->nrelocs; r++) {
				const uint8_t *rel = c->relocs + (size_t)r * COFF_RELOCATION_SIZE;
				if (pe_rd16(rel + 8) != IMAGE_REL_AMD64_ADDR64)
					continue;
				uint32_t ix = pe_rd32(rel + 4);
				pe_symbol *t = ix < c->file->nsyms ? c->file->syms[ix] : NULL;
				if (!t || t->kind == PE_SYM_ABSOLUTE)
					continue;
				pe_baserel b = { c->rva + pe_rd32(rel), IMAGE_REL_BASED_DIR64 };
				PE_PUSH(v, n, cap, b);
			}
		}
		pe_baserel_blocks(v, n);
		n = 0;
	}
	free(v);
}

static void pe_assign_addresses(void)
{
	pe_size_of_headers = (uint32_t)pe_align_up(PE_DOS_HEADER_SIZE + 56 + 4 + COFF_FILE_HEADER_SIZE +
						   PE32PLUS_OPTIONAL_HEADER_SIZE + COFF_SECTION_HEADER_SIZE * pe_output_count,
						   cfg.file_align);
	pe_file_size = pe_size_of_headers;
	uint64_t rva = pe_align_up(pe_size_of_headers, cfg.section_align);
	for (uint32_t oi = 0; oi < pe_output_count; oi++) {
		pe_output *o = pe_outputs[oi];
		if (o == pe_reloc)
			pe_add_baserels();
		uint64_t raw = 0, vsize = 0;
		o->rva = (uint32_t)rva;
		for (uint32_t ci = 0; ci < o->nchunks; ci++) {
			pe_chunk *c = o->chunks[ci];
			vsize = pe_align_up(vsize, c->align);
			c->rva = (uint32_t)(rva + vsize);
			c->os = o;
			vsize += c->size;
			if (c->has_data)
				raw = pe_align_up(vsize, cfg.file_align);
		}
		if (vsize > UINT32_MAX)
			pe_error("section larger than 4 GiB: %s", o->name);
		o->virtual_size = (uint32_t)vsize;
		o->raw_size = (uint32_t)raw;
		if (raw)
			o->file_offset = (uint32_t)pe_file_size;
		rva += pe_align_up(vsize, cfg.section_align);
		pe_file_size += pe_align_up(raw, cfg.file_align);
	}
	pe_size_of_image = (uint32_t)pe_align_up(rva, cfg.section_align);
	if (rva > UINT32_MAX)
		pe_error("image larger than 4 GiB");
}

static void pe_remove_empty_sections(void)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < pe_output_count; i++)
		if (pe_outputs[i]->virtual_size)
			pe_outputs[n++] = pe_outputs[i];
		else
			pe_outputs[i]->removed = true;
	pe_output_count = n;
	for (uint32_t i = 0; i < pe_output_count; i++)
		pe_outputs[i]->index = i + 1;
}

static bool pe_layout_run(void)
{
	pe_create_sections();
	pe_append_import_thunks();
	pe_misc_chunks();
	pe_export_table();
	pe_merge_sections();
	pe_remove_unused_sections();
	if (pe_error_count)
		return false;
	pe_assign_addresses();
	pe_remove_empty_sections();
	if (cfg.implib && (cfg.nexports || cfg.dll))
		pe_implib_write(cfg.implib);
	return pe_error_count == 0;
}
