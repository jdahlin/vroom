/*
 * Resolution, in lld-link's order (LinkerDriver::linkerMain): the command
 * line's files are queued and read FIFO; an archive adds lazy symbols and
 * queues the members that define symbols already undefined; an object's
 * undefined references to lazy symbols queue their members at the end.
 * Then the entry point, the linker-defined symbols, exports, alternate
 * names and _load_config_used, until nothing new is read; MinGW
 * auto-import loads; the remaining undefined symbols are resolved (weak
 * aliases, __imp_ of local symbols, auto-import) or reported; crtend.o is
 * moved last; COMMON alignment from -aligncomm; --gc-sections.
 */

typedef struct {
	const char *path;		/* a command-line file, or NULL: an archive member */
	bool whole;
	pe_archive *archive;
	uint32_t offset;
	const char *why;
} pe_task;

static pe_task *pe_tasks;
static uint32_t pe_task_count, pe_task_capacity, pe_task_next;
static pe_symbol **pe_gc_roots;
static uint32_t pe_gc_root_count, pe_gc_root_capacity;
static const char **pe_directive_exports;
static uint32_t pe_directive_export_count;
static pe_chunk **pe_local_import_chunks;
static uint32_t pe_local_import_count, pe_local_import_capacity;
static pe_symbol *pe_entry_sym;

static void pe_resolve_reset(void)
{
	free(pe_tasks);
	pe_tasks = NULL;
	pe_task_count = pe_task_capacity = pe_task_next = 0;
	free(pe_gc_roots);
	pe_gc_roots = NULL;
	pe_gc_root_count = pe_gc_root_capacity = 0;
	free((void *)pe_directive_exports);
	pe_directive_exports = NULL;
	pe_directive_export_count = 0;
	free(pe_local_import_chunks);
	pe_local_import_chunks = NULL;
	pe_local_import_count = pe_local_import_capacity = 0;
	pe_entry_sym = NULL;
}

static void pe_queue_member(pe_archive *a, uint32_t offset, const char *why)
{
	if (!pe_archive_seen_insert(a, offset))
		return;
	pe_task t = { NULL, false, a, offset, why };
	PE_PUSH(pe_tasks, pe_task_count, pe_task_capacity, t);
}

static void pe_gc_root_add(pe_symbol *s)
{
	if (s->gc_root)
		return;
	s->gc_root = true;
	PE_PUSH(pe_gc_roots, pe_gc_root_count, pe_gc_root_capacity, s);
}

/* LinkerDriver::addUndefined: a GC root */
static pe_symbol *pe_driver_undefined(const char *name)
{
	pe_symbol *s = pe_add_undefined(name, strlen(name), NULL, false);
	pe_gc_root_add(s);
	return s;
}

/* ---------------- .drectve ---------------- */

/* Windows command-line tokenizing (lld uses cl::TokenizeWindowsCommandLine) */
static uint32_t pe_directive_tokens(const char *s, size_t n, char ***out)
{
	char **v = NULL;
	uint32_t count = 0, cap = 0;
	size_t i = 0;
	while (i < n) {
		while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == 0))
			i++;
		if (i >= n)
			break;
		char *tok = pe_alloc(n - i + 1);
		size_t k = 0;
		bool quoted = false;
		while (i < n) {
			char c = s[i];
			if (!quoted && (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0))
				break;
			if (c == '\\') {
				size_t bs = 0;
				while (i < n && s[i] == '\\') {
					bs++;
					i++;
				}
				if (i < n && s[i] == '"') {
					for (size_t b = 0; b < bs / 2; b++)
						tok[k++] = '\\';
					if (bs & 1) {
						tok[k++] = '"';
						i++;
					}
				} else
					for (size_t b = 0; b < bs; b++)
						tok[k++] = '\\';
				continue;
			}
			if (c == '"') {
				if (quoted && i + 1 < n && s[i + 1] == '"') {
					tok[k++] = '"';
					i += 2;
					continue;
				}
				quoted = !quoted;
				i++;
				continue;
			}
			tok[k++] = c;
			i++;
		}
		tok[k] = 0;
		PE_PUSH(v, count, cap, tok);
	}
	*out = v;
	return count;
}

/* "-NAME:value" or "/NAME:value", NAME case-insensitive: the value or NULL */
static const char *pe_directive_opt(const char *tok, const char *name)
{
	if (tok[0] != '-' && tok[0] != '/')
		return NULL;
	size_t n = strlen(name);
	if (strncasecmp(tok + 1, name, n))
		return NULL;
	if (tok[1 + n] == ':')
		return tok + 2 + n;
	if (tok[1 + n] == 0)
		return "";
	return NULL;
}

static void pe_aligncomm_parse(const char *v)
{
	const char *comma = strrchr(v, ',');
	if (!comma) {
		pe_error("/aligncomm: invalid argument: %s", v);
		return;
	}
	char *name = pe_strndup(v, (size_t)(comma - v));
	uint64_t a;
	if (!pe_parse_u64(comma + 1, &a) || a > 31) {
		pe_error("/aligncomm: invalid argument: %s", v);
		return;
	}
	for (uint32_t i = 0; i < cfg.nalign_comm; i++)
		if (!strcmp(cfg.align_comm[i].name, name)) {
			if (cfg.align_comm[i].align < (uint32_t)a)
				cfg.align_comm[i].align = (uint32_t)a;
			return;
		}
	cfg.align_comm = realloc(cfg.align_comm, (cfg.nalign_comm + 1) * sizeof *cfg.align_comm);
	if (!cfg.align_comm)
		pe_oom();
	cfg.align_comm[cfg.nalign_comm].name = name;
	cfg.align_comm[cfg.nalign_comm].align = (uint32_t)a;
	cfg.nalign_comm++;
}

static void pe_alternate_name_add(const char *from, const char *to)
{
	for (uint32_t i = 0; i < cfg.nalternate_names; i++)
		if (!strcmp(cfg.alternate_names[i].from, from)) {
			if (strcmp(cfg.alternate_names[i].to, to))
				pe_error("/alternatename: conflicts: %s", from);
			return;
		}
	cfg.alternate_names = realloc(cfg.alternate_names, (cfg.nalternate_names + 1) * sizeof *cfg.alternate_names);
	if (!cfg.alternate_names)
		pe_oom();
	cfg.alternate_names[cfg.nalternate_names].from = from;
	cfg.alternate_names[cfg.nalternate_names].to = to;
	cfg.nalternate_names++;
}

static void pe_queue_path(const char *path, bool whole);

static void pe_directives_parse(pe_file *f)
{
	if (!f->directives_len)
		return;
	pe_log("Directives: %s: %.*s", pe_file_name(f), (int)f->directives_len, f->directives);
	char **tok;
	uint32_t n = pe_directive_tokens(f->directives, f->directives_len, &tok);
	for (uint32_t i = 0; i < n; i++) {
		const char *t = tok[i], *v;
		if ((v = pe_directive_opt(t, "export"))) {
			bool dup = false;
			for (uint32_t k = 0; k < pe_directive_export_count && !dup; k++)
				dup = !strcmp(pe_directive_exports[k], v);
			if (dup)
				continue;
			PE_PUSH_STR(pe_directive_exports, pe_directive_export_count, v);
			pe_export_parse(v, true);
		} else if ((v = pe_directive_opt(t, "include"))) {
			pe_driver_undefined(v);
		} else if ((v = pe_directive_opt(t, "exclude-symbols"))) {
			for (const char *s = v; *s;) {
				size_t k = strcspn(s, ",");
				if (k)
					PE_PUSH_STR(cfg.exclude_symbols, cfg.nexclude_symbols, pe_strndup(s, k));
				s += k;
				if (*s)
					s++;
			}
		} else if ((v = pe_directive_opt(t, "aligncomm"))) {
			pe_aligncomm_parse(v);
		} else if ((v = pe_directive_opt(t, "alternatename"))) {
			const char *eq = strchr(v, '=');
			if (!eq || eq == v || !eq[1])
				pe_error("/alternatename: invalid argument: %s", v);
			else
				pe_alternate_name_add(pe_strndup(v, (size_t)(eq - v)), eq + 1);
		} else if ((v = pe_directive_opt(t, "defaultlib"))) {
			/* lld findLib: the name as given, with .lib appended without an extension */
			const char *name = v;
			if (!strchr(pe_basename(name), '.'))
				name = pe_format("%s.lib", name);
			for (uint32_t d = 0; d < pe_search_dir_count; d++) {
				const char *p = pe_path_join(pe_search_dirs[d], name);
				if (pe_file_exists(p)) {
					pe_queue_path(p, false);
					break;
				}
			}
		} else if (pe_directive_opt(t, "nodefaultlib") || pe_directive_opt(t, "failifmismatch") ||
			   pe_directive_opt(t, "editandcontinue") || pe_directive_opt(t, "guardsym") ||
			   pe_directive_opt(t, "throwingnew") || pe_directive_opt(t, "manifestdependency") ||
			   pe_directive_opt(t, "release") || pe_directive_opt(t, "inferasanlibs") ||
			   pe_directive_opt(t, "inferasanlibs:no")) {
			/* nothing to do for these here */
		} else if ((v = pe_directive_opt(t, "merge")) || (v = pe_directive_opt(t, "section")) ||
			   (v = pe_directive_opt(t, "entry")) || (v = pe_directive_opt(t, "subsystem")) ||
			   (v = pe_directive_opt(t, "stack"))) {
			pe_error("%s in .drectve is not supported by vlink (%s)", t, pe_file_name(f));
		} else
			pe_error("%s is not allowed in .drectve (%s)", t, pe_file_name(f));
	}
	free(tok);
}

/* ---------------- reading files ---------------- */

static bool pe_is_coff_object(const uint8_t *d, size_t n)
{
	return n >= COFF_FILE_HEADER_SIZE && pe_rd16(d) == IMAGE_FILE_MACHINE_AMD64;
}

static bool pe_is_short_import(const uint8_t *d, size_t n)
{
	return n >= 4 && pe_rd16(d) == 0 && pe_rd16(d + 2) == IMPORT_OBJECT_HDR_SIG2;
}

static void pe_buffer_add(const char *path, const char *parent, const uint8_t *d, size_t n)
{
	pe_file *f = pe_alloc(sizeof *f);
	f->path = path;
	f->parent = parent;
	f->data = d;
	f->size = n;
	pe_log("Reading %s", pe_file_name(f));
	if (pe_is_short_import(d, n)) {
		f->kind = PE_FILE_IMPORT;
		if (pe_import_parse(f)) {
			f->order = pe_import_count;
			PE_PUSH(pe_imports, pe_import_count, pe_import_capacity, f);
		}
		return;
	}
	if (n >= 2 && (pe_rd16(d) == IMAGE_FILE_MACHINE_I386 || pe_rd16(d) == IMAGE_FILE_MACHINE_ARM64 ||
		       pe_rd16(d) == 0x1c4 || pe_rd16(d) == 0xa641)) {
		pe_error("%s: machine type %#x conflicts with x64", pe_file_name(f), pe_rd16(d));
		return;
	}
	if (!pe_is_coff_object(d, n)) {
		if (n >= 4 && !memcmp(d, "\x7f" "ELF", 4))
			pe_error("%s: unknown file type (an ELF file)", pe_file_name(f));
		else if (n >= 2 && d[0] == 'M' && d[1] == 'Z')
			pe_error("%s: bad file type. Did you specify a DLL instead of an import library?", pe_file_name(f));
		else
			pe_error("%s: unknown file type", pe_file_name(f));
		return;
	}
	f->kind = PE_FILE_OBJECT;
	if (!pe_obj_parse(f))
		return;
	f->order = pe_object_count;
	PE_PUSH(pe_objects, pe_object_count, pe_object_capacity, f);
	pe_directives_parse(f);
}

static void pe_queue_path(const char *path, bool whole)
{
	pe_task t = { path, whole, NULL, 0, NULL };
	PE_PUSH(pe_tasks, pe_task_count, pe_task_capacity, t);
}

static void pe_task_run(pe_task *t)
{
	if (!t->path) {
		const uint8_t *body;
		size_t n;
		const char *name;
		if (!pe_archive_member(t->archive, t->offset, &body, &n, &name))
			return;
		uint32_t before = pe_object_count + pe_import_count;
		pe_buffer_add(name, t->archive->path, body, n);
		if (pe_object_count + pe_import_count > before && t->why)
			pe_log("Loaded %s(%s) for %s", pe_basename(t->archive->path), name, t->why);
		return;
	}
	size_t n;
	const uint8_t *d = pe_file_map(t->path, &n);
	if (!d)
		return;
	if (n >= 8 && (!memcmp(d, "!<arch>\n", 8) || !memcmp(d, "!<thin>\n", 8))) {
		pe_log("Reading %s", t->path);
		pe_archive *a = pe_archive_open(t->path, d, n);
		if (!a)
			return;
		if (t->whole) {
			/* every member, in order */
			uint32_t off = 8;
			while ((uint64_t)off + 60 <= n) {
				const uint8_t *body;
				size_t sz;
				const char *name;
				if (!pe_archive_member(a, off, &body, &sz, &name))
					return;
				const uint8_t *h = d + off;
				bool special = h[0] == '/' && (h[1] == ' ' || (h[1] == '/' && h[2] == ' '));
				if (!special && pe_archive_seen_insert(a, off))
					pe_buffer_add(name, t->path, body, sz);
				off += 60 + (uint32_t)sz;
				off += off & 1;
			}
			return;
		}
		for (uint32_t i = 0; i < a->nsymbols; i++)
			pe_add_lazy(a, a->symbols[i].name, a->symbols[i].offset);
		return;
	}
	pe_buffer_add(t->path, NULL, d, n);
}

/* LinkerDriver::run: true if anything was read */
static bool pe_run(void)
{
	bool did = pe_task_next < pe_task_count;
	while (pe_task_next < pe_task_count && pe_error_count == 0) {
		pe_task t = pe_tasks[pe_task_next++];
		pe_task_run(&t);
	}
	return did;
}

/* ---------------- the driver's steps ---------------- */

static void pe_resolve_weak(pe_symbol *s)
{
	/* follow the chain of weak aliases to a definition (lld Undefined::getDefinedWeakAlias) */
	pe_symbol *a = s->weak_alias;
	for (int depth = 0; a && depth < 64; depth++) {
		if (pe_sym_is_defined(a))
			break;
		if (a->kind != PE_SYM_UNDEFINED)
			return;
		a = a->weak_alias;
	}
	if (!a || !pe_sym_is_defined(a))
		return;
	/* lld replaceKeepingName: the definition copied over the undefined symbol, which keeps its name */
	pe_symbol *next = s->hash_next;
	const char *name = s->name;
	uint32_t n = s->name_len;
	bool root = s->gc_root;
	*s = *a;
	s->hash_next = next;
	s->name = name;
	s->name_len = n;
	s->gc_root = root || a->gc_root;
	s->external = true;
}

static bool pe_auto_import(pe_symbol *s)
{
	pe_symbol *imp = pe_sym_find(pe_format("__imp_%s", s->name));
	if (!imp || (imp->kind != PE_SYM_IMPORT_DATA && imp->kind != PE_SYM_REGULAR))
		return false;
	if (imp->kind == PE_SYM_IMPORT_DATA)
		pe_log("Automatically importing %s from %s", s->name, imp->file->dll_name);
	else
		pe_log("Automatically importing %s from %s", s->name, pe_file_name(imp->file));
	/* replaceKeepingName: the IAT slot, marked for a pseudo relocation per reference */
	pe_symbol *next = s->hash_next;
	const char *name = s->name;
	uint32_t n = s->name_len;
	bool root = s->gc_root, used = s->used_in_regular;
	*s = *imp;
	s->hash_next = next;
	s->name = name;
	s->name_len = n;
	s->gc_root = root;
	s->used_in_regular = used;
	s->written = false;
	s->runtime_pseudo_reloc = true;
	/* .refptr.NAME pointing at NAME alone becomes the IAT slot itself */
	pe_symbol *refptr = pe_sym_find(pe_format(".refptr.%s", name));
	if (refptr && refptr->kind == PE_SYM_REGULAR && refptr->chunk && refptr->chunk->size == 8 &&
	    refptr->chunk->kind == PE_CHUNK_SECTION && refptr->chunk->nrelocs == 1) {
		pe_chunk *rc = refptr->chunk;
		uint32_t ix = pe_rd32(rc->relocs + 4);
		if (ix < rc->file->nsyms && rc->file->syms[ix] == s) {
			pe_log("Replacing .refptr.%s with %s", name, imp->name);
			rc->live = false;
			pe_symbol *rn = refptr->hash_next;
			const char *rname = refptr->name;
			uint32_t rl = refptr->name_len;
			bool rroot = refptr->gc_root, rused = refptr->used_in_regular;
			*refptr = *imp;
			refptr->hash_next = rn;
			refptr->name = rname;
			refptr->name_len = rl;
			refptr->gc_root = rroot;
			refptr->used_in_regular = rused;
			refptr->written = false;
		}
	}
	return true;
}

/* where an undefined symbol is referenced (lld getSymbolLocations) */
static void pe_undefined_locations(pe_file *f, uint32_t symindex, char **out, size_t *len, uint32_t *shown, uint32_t *total)
{
	for (uint32_t ci = 0; ci < f->nchunks; ci++) {
		pe_chunk *c = f->chunks[ci];
		if (c->kind != PE_CHUNK_SECTION)
			continue;
		for (uint32_t r = 0; r < c->nrelocs; r++) {
			const uint8_t *rel = c->relocs + (size_t)r * COFF_RELOCATION_SIZE;
			if (pe_rd32(rel + 4) != symindex)
				continue;
			(*total)++;
			if (*shown >= 3)
				continue;
			uint32_t addr = pe_rd32(rel);
			pe_symbol *best = NULL;
			for (uint32_t k = 0; k < f->nsyms; k++) {
				pe_symbol *s = f->syms[k];
				if (!s || s->kind != PE_SYM_REGULAR || s->file != f || s->chunk != c || s->value > addr ||
				    (best && s->value < best->value))
					continue;
				best = s;
			}
			const char *loc = best ? pe_format("\n>>> referenced by %s:(%.*s)", pe_file_name(f), (int)best->name_len, best->name)
					       : pe_format("\n>>> referenced by %s", pe_file_name(f));
			size_t ln = strlen(loc);
			char *nb = pe_alloc(*len + ln + 1);
			memcpy(nb, *out, *len);
			memcpy(nb + *len, loc, ln + 1);
			*out = nb;
			*len += ln;
			(*shown)++;
		}
	}
}

static void pe_report_undefined(pe_symbol **undefs, uint32_t nundef)
{
	if (!nundef)
		return;
	for (uint32_t i = 0; i < pe_gc_root_count; i++)
		for (uint32_t k = 0; k < nundef; k++)
			if (undefs[k] == pe_gc_roots[i])
				pe_error("<root>: undefined symbol: %s", undefs[k]->name);
	/* per symbol, the files that refer to it, in file order */
	pe_symbol **order = NULL;
	uint32_t norder = 0, cap = 0;
	for (uint32_t fi = 0; fi < pe_object_count; fi++) {
		pe_file *f = pe_objects[fi];
		for (uint32_t k = 0; k < f->nsyms; k++) {
			pe_symbol *s = f->syms[k];
			if (!s || s->kind != PE_SYM_UNDEFINED || !s->used_in_regular)
				continue;
			bool listed = false, undef = false;
			for (uint32_t j = 0; j < nundef && !undef; j++)
				undef = undefs[j] == s;
			for (uint32_t j = 0; j < norder && !listed; j++)
				listed = order[j] == s;
			if (undef && !listed)
				PE_PUSH(order, norder, cap, s);
		}
	}
	for (uint32_t j = 0; j < norder; j++) {
		pe_symbol *s = order[j];
		char *msg = pe_strdup("");
		size_t len = 0;
		uint32_t shown = 0, total = 0;
		for (uint32_t fi = 0; fi < pe_object_count; fi++) {
			pe_file *f = pe_objects[fi];
			for (uint32_t k = 0; k < f->nsyms; k++)
				if (f->syms[k] == s)
					pe_undefined_locations(f, k, &msg, &len, &shown, &total);
		}
		if (shown < total)
			pe_error("undefined symbol: %s%s\n>>> referenced %u more times", s->name, msg, total - shown);
		else
			pe_error("undefined symbol: %s%s", s->name, msg);
	}
	free(order);
}

static void pe_resolve_remaining(void)
{
	pe_symbol **undefs = NULL;
	uint32_t nundef = 0, cap = 0;
	for (uint32_t i = 0; i < pe_global_count; i++) {
		pe_symbol *s = pe_globals[i];
		if (s->kind != PE_SYM_UNDEFINED || !s->used_in_regular)
			continue;
		if (s->weak_alias) {
			pe_resolve_weak(s);
			if (s->kind != PE_SYM_UNDEFINED)
				continue;
		}
		if (s->name_len > 6 && !memcmp(s->name, "__imp_", 6)) {
			pe_symbol *imp = pe_sym_find(s->name + 6);
			if (imp && imp->kind == PE_SYM_UNDEFINED && imp->weak_alias)
				pe_resolve_weak(imp);
			if (imp && pe_sym_is_defined(imp)) {
				pe_chunk *c = pe_chunk_new(PE_CHUNK_LOCAL_IMPORT);
				c->name = ".rdata";
				c->chars = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
				c->size = 8;
				c->align = 8;
				c->sym = imp;
				pe_sym_replace(s, PE_SYM_LOCAL_IMPORT, NULL);
				s->chunk = c;
				PE_PUSH(pe_local_import_chunks, pe_local_import_count, pe_local_import_capacity, c);
				continue;
			}
		}
		if (cfg.auto_import && pe_auto_import(s))
			continue;
		PE_PUSH(undefs, nundef, cap, s);
	}
	pe_report_undefined(undefs, nundef);
	free(undefs);
	/* lld's LNK4217 warning, once per object that refers to such an __imp_ symbol */
	if (pe_local_import_count)
		for (uint32_t fi = 0; fi < pe_object_count; fi++) {
			pe_file *f = pe_objects[fi];
			for (uint32_t k = 0; k < f->nsyms; k++) {
				pe_symbol *s = f->syms[k];
				if (s && s->kind == PE_SYM_LOCAL_IMPORT && s->chunk && s->chunk->sym)
					pe_warn("%s: locally defined symbol imported: %s (defined in %s) [LNK4217]", pe_file_name(f),
						s->chunk->sym->name, pe_file_name(s->chunk->sym->file));
			}
		}
}

static bool pe_is_crtend(const char *path)
{
	const char *b = pe_basename(path);
	return !strcmp(b, "crtend.o") || !strcmp(b, "crtend.obj");
}

/* MinGW's auto-import: load __imp_X members for still undefined X (SymbolTable::loadMinGWSymbols) */
static void pe_load_mingw_symbols(void)
{
	uint32_t n = pe_global_count;
	for (uint32_t i = 0; i < n; i++) {
		pe_symbol *s = pe_globals[i];
		if (s->kind != PE_SYM_UNDEFINED || s->weak_alias)
			continue;
		if (s->name_len >= 6 && !memcmp(s->name, "__imp_", 6))
			continue;
		pe_symbol *l = pe_sym_find(pe_format("__imp_%s", s->name));
		if (!l || l->pending_load || l->kind != PE_SYM_LAZY_ARCHIVE)
			continue;
		pe_log("Loading lazy %s from %s for automatic import", l->name, l->archive->path);
		pe_force_lazy(l);
	}
}

/* --gc-sections (lld markLive) */
static void pe_mark_live(void)
{
	pe_chunk **work = NULL;
	uint32_t n = 0, cap = 0;
	for (uint32_t fi = 0; fi < pe_object_count; fi++) {
		pe_file *f = pe_objects[fi];
		for (uint32_t ci = 0; ci < f->nchunks; ci++) {
			pe_chunk *c = f->chunks[ci];
			if (c->kind == PE_CHUNK_SECTION && c->live && !c->is_dwarf)
				PE_PUSH(work, n, cap, c);
		}
	}
#define PE_ENQUEUE(c) do { pe_chunk *pe_c_ = (c); if (pe_c_ && pe_c_->kind == PE_CHUNK_SECTION && !pe_c_->live) { \
	pe_c_->live = true; PE_PUSH(work, n, cap, pe_c_); } } while (0)
	for (uint32_t pass = 0;; pass++) {
		/* roots once, then the work list */
		if (pass == 0)
			for (uint32_t i = 0; i < pe_gc_root_count; i++) {
				pe_symbol *b = pe_gc_roots[i];
				if (b->kind == PE_SYM_REGULAR)
					PE_ENQUEUE(b->chunk);
				else if (b->kind == PE_SYM_IMPORT_DATA)
					b->file->live = true;
				else if (b->kind == PE_SYM_IMPORT_THUNK)
					b->wrapped->file->live = b->wrapped->file->thunk_live = true;
			}
		if (!n)
			break;
		pe_chunk *c = work[--n];
		for (uint32_t r = 0; r < c->nrelocs; r++) {
			uint32_t ix = pe_rd32(c->relocs + (size_t)r * COFF_RELOCATION_SIZE + 4);
			pe_symbol *b = ix < c->file->nsyms ? c->file->syms[ix] : NULL;
			if (!b)
				continue;
			if (b->kind == PE_SYM_REGULAR)
				PE_ENQUEUE(b->chunk);
			else if (b->kind == PE_SYM_IMPORT_DATA)
				b->file->live = true;
			else if (b->kind == PE_SYM_IMPORT_THUNK)
				b->wrapped->file->live = b->wrapped->file->thunk_live = true;
			else if (b->kind == PE_SYM_LOCAL_IMPORT && b->chunk->sym && b->chunk->sym->kind == PE_SYM_REGULAR)
				PE_ENQUEUE(b->chunk->sym->chunk);
		}
		for (pe_chunk *a = c->assoc_first; a; a = a->assoc_next)
			PE_ENQUEUE(a);
	}
#undef PE_ENQUEUE
	free(work);
}

static void pe_auto_export(void);
static void pe_exports_fixup(void);

static bool pe_resolve_run(void)
{
	/* lld findFileIfNew: a file named twice is read once (the name compared case-insensitively) */
	for (uint32_t i = 0; i < pe_input_count; i++) {
		bool seen = false;
		for (uint32_t k = 0; k < i && !seen; k++)
			seen = !strcasecmp(pe_inputs[k].path, pe_inputs[i].path);
		if (!seen)
			pe_queue_path(pe_inputs[i].path, pe_inputs[i].whole_archive);
	}
	pe_run();
	if (pe_error_count)
		return false;

	/* the entry point */
	if (cfg.entry) {
		if (!cfg.entry[0])
			cfg.no_entry = true;
		else
			pe_entry_sym = pe_driver_undefined(cfg.entry);
	} else if (cfg.dll) {
		pe_entry_sym = pe_driver_undefined("DllMainCRTStartup");
	} else {
		if (cfg.subsystem == IMAGE_SUBSYSTEM_UNKNOWN)
			cfg.subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
		const char *e = cfg.subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI ? "WinMainCRTStartup" : "mainCRTStartup";
		pe_entry_sym = pe_driver_undefined(e);
		pe_log("Entry name inferred: %s", e);
	}
	if (cfg.subsystem == IMAGE_SUBSYSTEM_UNKNOWN)
		cfg.subsystem = cfg.dll ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI;
	for (uint32_t i = 0; i < cfg.nincludes; i++)
		pe_driver_undefined(cfg.includes[i]);

	/* the linker's symbols */
	pe_add_synthetic("__ImageBase", NULL);
	static const char *const guards[] = {
		"__guard_fids_count", "__guard_fids_table", "__guard_flags", "__guard_iat_count", "__guard_iat_table",
		"__guard_longjmp_count", "__guard_longjmp_table", "__guard_eh_cont_count", "__guard_eh_cont_table",
	};
	for (size_t i = 0; i < sizeof guards / sizeof guards[0]; i++)
		pe_add_absolute(guards[i], strlen(guards[i]), 0, NULL, NULL);
	if (cfg.pseudo_relocs) {
		pe_add_absolute("__RUNTIME_PSEUDO_RELOC_LIST__", 29, 0, NULL, NULL);
		pe_add_absolute("__RUNTIME_PSEUDO_RELOC_LIST_END__", 33, 0, NULL, NULL);
	}
	pe_add_absolute("__CTOR_LIST__", 13, 0, NULL, NULL);
	pe_add_absolute("__DTOR_LIST__", 13, 0, NULL, NULL);
	if ((cfg.debug == 2 || cfg.build_id) && pe_sym_find("__buildid"))
		pe_driver_undefined("__buildid");
	pe_alternate_name_add("__image_base__", "__ImageBase");

	do {
		for (uint32_t i = 0; i < cfg.nexports; i++) {
			pe_export *e = &cfg.exports[i];
			if (e->forward_to)
				continue;
			e->sym = pe_driver_undefined(e->symbol_name ? e->symbol_name : e->name);
		}
		for (uint32_t i = 0; i < cfg.nalternate_names; i++) {
			pe_symbol *s = pe_sym_find(cfg.alternate_names[i].from);
			if (s && s->kind == PE_SYM_UNDEFINED && !s->weak_alias)
				s->weak_alias = pe_driver_undefined(cfg.alternate_names[i].to);
		}
		if (pe_sym_find("_load_config_used"))
			pe_driver_undefined("_load_config_used");
		for (uint32_t i = 0; i < cfg.noptional_includes; i++) {
			pe_symbol *s = pe_sym_find(cfg.optional_includes[i]);
			if (s && s->kind == PE_SYM_LAZY_ARCHIVE)
				pe_driver_undefined(cfg.optional_includes[i]);
		}
	} while (pe_run() && !pe_error_count);
	if (pe_error_count)
		return false;

	if (cfg.auto_import) {
		pe_load_mingw_symbols();
		pe_run();
	}
	pe_auto_export();
	pe_run();
	if (pe_error_count)
		return false;

	pe_resolve_remaining();
	if (pe_error_count)
		return false;

	/* crtend.o last: its chunks terminate lists */
	for (uint32_t i = 0; i < pe_object_count; i++)
		if (pe_is_crtend(pe_objects[i]->path) && !pe_objects[i]->parent) {
			pe_file *f = pe_objects[i];
			memmove(&pe_objects[i], &pe_objects[i + 1], (pe_object_count - i - 1) * sizeof *pe_objects);
			pe_objects[pe_object_count - 1] = f;
			break;
		}

	if (cfg.nexports || cfg.dll)
		pe_exports_fixup();

	for (uint32_t i = 0; i < cfg.nalign_comm; i++) {
		pe_symbol *s = pe_sym_find(cfg.align_comm[i].name);
		if (s && s->kind == PE_SYM_COMMON && s->chunk) {
			uint32_t a = 1u << cfg.align_comm[i].align;
			if (s->chunk->align < a)
				s->chunk->align = a;
		}
	}

	if (cfg.gc) {
		static const char *const personalities[] = { "__gxx_personality_v0", "__gcc_personality_v0", "rust_eh_personality" };
		for (size_t i = 0; i < 3; i++) {
			pe_symbol *d = pe_sym_find(personalities[i]);
			if (d && pe_sym_is_defined(d))
				pe_gc_root_add(d);
		}
		pe_mark_live();
	}
	return pe_error_count == 0;
}
