/*
 * function-output.c: hand a finished function's text to the unit, write an ELF symbol header
 * (section, visibility, alignment, .type, the label), and the constructor/destructor table entries.
 *
 * codegen_chunk_add appends one finished chunk, keyed by the function's order key, under E->cmu: chunks
 * come from any worker thread and are stitched in key order at the end, which is what makes the
 * output independent of thread timing.  codegen_symbol_header_append writes what clang writes before a function or
 * object (an explicit section, -ffunction-sections/-fdata-sections, hidden/protected, .p2align,
 * -fpatchable-function-entry nops on x86), as text or, in the direct object path, as an item
 * (obj.c).  Called by the backends' function finishers and by data.c.
 */
#include "codegen-internal.h"

VPRIV void codegen_chunk_add(codegen_context *e, uint64_t key, size_t start)
{
	codegen_unit *E = e->unit;

	pthread_mutex_lock(&E->chunk_mutex);
	E->chunks[E->chunk_count++] = (codegen_chunk){ key, e->out.p + start, e->out.n - start, e->round_log + e->round_log_item_start,
					  (uint32_t)((e->round_log_count - e->round_log_item_start) / 2) };
	pthread_mutex_unlock(&E->chunk_mutex);
}

/*
 * COFF (Windows x64), as clang: a function's .def/.scl/.type/.endef record (storage class 2
 * external, 3 static; type 32 a function) first; sections .text, .data, .bss, .rdata ("dr") and
 * .tls$ ("dw"); a named section "xr", "dr" or "dw"; -ffunction-sections / -fdata-sections put each
 * in .text$NAME (.data$, .rdata$, .bss$) as a one_only COMDAT keyed by it; no .type or .size.
 */
static void sym_header_coff(codegen_context *e, tbuf *b, uint32_t s, bool fn, const char *defsec)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	const attrs *a = codegen_symbol_attributes(c, s);
	bool global = y->sc != AST_STORAGE_STATIC && y->depth == 0 && y->name;

	if (fn) {
		vtext_string_append(b, "\t.def\t");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, global ? ";\n\t.scl\t2;\n\t.type\t32;\n\t.endef\n" : ";\n\t.scl\t3;\n\t.type\t32;\n\t.endef\n");
	}
	if (a->section) {
		vtext_string_append(b, "\t.section\t");
		vtext_string_append(b, c->strs[a->section - 1].s);
		vtext_string_append(b, fn ? ",\"xr\"\n" : (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_CONST) ? ",\"dr\"\n" : ",\"dw\"\n");
	} else if (c->o->sections & (fn ? 1 : 2)) {
		static const struct { const char *def, *base, *flags; } m[] = {
			{ "\t.text\n", ".text$", "xr" },
			{ "\t.data\n", ".data$", "dw" },
			{ "\t.bss\n", ".bss$", "bw" },
			{ "\t.section\t.rdata,\"dr\"\n", ".rdata$", "dr" },
			{ "\t.section\t.tls$,\"dw\"\n", ".tls$$", "dw" },
		};
		size_t k = 0;

		while (k < sizeof m / sizeof *m && strcmp(defsec, m[k].def))
			k++;
		if (k < sizeof m / sizeof *m) {
			vtext_string_append(b, "\t.section\t");
			vtext_string_append(b, m[k].base);
			codegen_symbol_name_append(c, b, s);
			vtext_format_append(b, ",\"%s\",one_only,", m[k].flags);
			codegen_symbol_name_append(c, b, s);
			vtext_char_append(b, '\n');
		} else {
			vtext_string_append(b, defsec);
		}
	} else {
		vtext_string_append(b, defsec);
	}
}

VPRIV void codegen_symbol_header_append(codegen_context *e, tbuf *b, uint32_t s, bool fn, const char *defsec, uint32_t align)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	const attrs *a = codegen_symbol_attributes(c, s);
	bool global = y->sc != AST_STORAGE_STATIC && y->depth == 0 && y->name;
	size_t h = codegen_item_open(e, CODEGEN_ITEM_TEXT);

	if (CODEGEN_IS_WIN64(c)) {
		sym_header_coff(e, b, s, fn, defsec);
	} else if (a->section) {
		const char *sn = c->strs[a->section - 1].s;
		bool bss = !strncmp(sn, ".bss", 4) || !strncmp(sn, ".tbss", 5);

		vtext_string_append(b, "\t.section\t\"");
		vtext_string_append(b, sn);
		vtext_string_append(b, fn ? "\",\"ax\",@progbits\n" : bss ? "\",\"aw\",@nobits\n" :
			 (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_CONST) && !strncmp(sn, ".rodata", 7) ? "\",\"a\",@progbits\n" : "\",\"aw\",@progbits\n");
	} else if (!CODEGEN_IS_DARWIN(c) && (c->o->sections & (fn ? 1 : 2))) {
		/* -ffunction-sections / -fdata-sections: .text.NAME etc., as clang */
		static const struct { const char *def, *base, *rest; } m[] = {
			{ "\t.text\n", ".text.", ",\"ax\",@progbits\n" },
			{ "\t.data\n", ".data.", ",\"aw\",@progbits\n" },
			{ "\t.bss\n", ".bss.", ",\"aw\",@nobits\n" },
		};
		const char *base = NULL, *rest = NULL;
		char nb[16];

		for (size_t k = 0; k < sizeof m / sizeof *m && !base; k++)
			if (!strcmp(defsec, m[k].def))
				base = m[k].base, rest = m[k].rest;
		if (!base && !strncmp(defsec, "\t.section\t.", 10)) {
			/* "\t.section\t.rodata,..." -> ".rodata." and ",..." */
			const char *cm = strchr(defsec + 10, ',');
			size_t n = cm ? (size_t)(cm - (defsec + 10)) : 0;

			if (cm && n + 2 <= sizeof nb) {
				memcpy(nb, defsec + 10, n);
				nb[n] = '.', nb[n + 1] = 0;
				base = nb, rest = cm;
			}
		}
		if (base) {
			vtext_string_append(b, "\t.section\t");
			vtext_string_append(b, base);
			codegen_symbol_name_append(c, b, s);
			vtext_string_append(b, rest);
		} else {
			vtext_string_append(b, defsec);
		}
	} else {
		vtext_string_append(b, defsec);
	}
	if (h != SIZE_MAX && !(fn && CODEGEN_IS_X64(c) && c->o->pfe_m) && !CODEGEN_IS_WIN64(c)) {	/* (COFF: text for codegen_clock_seconds, WIN5) */
		codegen_symbol_header *sh;

		codegen_item_close(e, h);
		h = codegen_item_open(e, CODEGEN_ITEM_SYMBOL_HEADER);
		sh = codegen_item_room_need(e, sizeof *sh);
		*sh = (codegen_symbol_header){ .symbol = s, .alignment_log2 = (uint8_t)__builtin_ctz(align ? align : 1), .is_function = fn };
		if (global) {
			sh->bind = (y->flags & AST_SYMBOL_FLAG_WEAK) ? STB_WEAK : STB_GLOBAL;
			if (a->visibility) {
				const char *v = c->strs[a->visibility - 1].s;

				sh->visibility = !strcmp(v, "hidden") ? STV_HIDDEN : !strcmp(v, "protected") ? STV_PROTECTED : 0;
			}
		}
		codegen_item_close(e, h);
		return;
	}
	if (global) {
		vtext_string_append(b, (y->flags & AST_SYMBOL_FLAG_WEAK) ? "\t.weak\t" : "\t.globl\t");
		codegen_symbol_name_append(c, b, s);
		vtext_char_append(b, '\n');
		if (a->visibility && !CODEGEN_IS_WIN64(c)) {	/* (COFF has no symbol visibility) */
			const char *v = c->strs[a->visibility - 1].s;

			if (!strcmp(v, "hidden") || !strcmp(v, "protected")) {
				vtext_string_append(b, !strcmp(v, "hidden") ? "\t.hidden\t" : "\t.protected\t");
				codegen_symbol_name_append(c, b, s);
				vtext_char_append(b, '\n');
			}
		}
	}
	vtext_string_append(b, "\t.p2align\t");
	vtext_unsigned_append(b, (uint64_t)__builtin_ctz(align ? align : 1));
	if (CODEGEN_IS_WIN64(c)) {
		vtext_char_append(b, '\n');	/* (COFF: the .def record, no .type) */
	} else {
		vtext_string_append(b, fn ? "\n\t.type\t" : "\n\t.type\t");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, fn ? ",@function\n" : ",@object\n");
	}
	if (fn && CODEGEN_IS_X64(c) && c->o->pfe_m) {
		/* -fpatchable-function-entry=N,M: M nops before the entry, recorded for ftrace */
		vtext_string_append(b, ".Lpfe");
		vtext_unsigned_append(b, s);
		vtext_string_append(b, ":\n");
		for (int k = 0; k < c->o->pfe_m; k++)
			vtext_string_append(b, "\tnop\n");
	}
	codegen_symbol_name_append(c, b, s);
	vtext_string_append(b, ":\n");
	codegen_item_close(e, h);
}

/* a constructor's or destructor's pointer: .init_array[.prio] / .fini_array[.prio], as clang */
VPRIV void codegen_constructor_entry_append(cc *c, tbuf *b, uint32_t s, bool fini)
{
	const attrs *a = codegen_symbol_attributes(c, s);

	if (CODEGEN_IS_WIN64(c)) {
		/* MinGW: .ctors/.dtors, a priority as .ctors.NNNNN (65535 - priority), as clang */
		vtext_string_append(b, fini ? "\t.section\t.dtors" : "\t.section\t.ctors");
		if (a && a->prio)
			vtext_format_append(b, ".%05u", 65535 - a->prio);
		vtext_string_append(b, ",\"dw\"\n\t.p2align\t3\n\t.quad\t");
		codegen_symbol_name_append(c, b, s);
		vtext_char_append(b, '\n');
		return;
	}

	vtext_string_append(b, fini ? "\t.section\t.fini_array" : "\t.section\t.init_array");
	if (a && a->prio) {
		vtext_char_append(b, '.');
		vtext_unsigned_append(b, a->prio);
	}
	vtext_string_append(b, fini ? ",\"aw\",@fini_array\n\t.p2align\t3\n" : ",\"aw\",@init_array\n\t.p2align\t3\n");
	vtext_string_append(b, CODEGEN_IS_X64(c) ? "\t.quad\t" : "\t.xword\t");
	codegen_symbol_name_append(c, b, s);
	vtext_char_append(b, '\n');
}
