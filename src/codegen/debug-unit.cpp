/*
 * debug-unit.cpp: -g: a translation unit's compile unit (.debug_abbrev, .debug_info, the ranges),
 * written after the unit's code as assembler text.  One DW_TAG_compile_unit with a
 * DW_TAG_subprogram per function emitted (doc/PLAN-debuginfo.md); the line table is the
 * assembler's, from the .loc rows (debug-rows.cpp).
 */
#include "common/literal.h"
#include "codegen-internal.h"

/* the abbreviation codes */
enum {
    DEBUG_UNIT = 1,
    DEBUG_FUNCTION,         /* DW_AT_name */
    DEBUG_FUNCTION_LINKAGE, /* DW_AT_name and DW_AT_linkage_name */
    DEBUG_FUNCTION_UNNAMED, /* DW_AT_linkage_name alone */
};

static bool debug_chunk_is_function(const cc *c, const codegen_chunk *chunk)
{
    uint64_t key = chunk->key;

    return !(key & 1) && key < ((uint64_t)1 << 62) &&
           c->syms[key >> 1].kind == AST_SYMBOL_FUNCTION;
}

/* "NAME, the label after function s's last instruction */
static void debug_function_end_append(const cc *c, tbuf *b, uint32_t s)
{
    const char *end = ".Lfe";

    if (CODEGEN_IS_X64(c))
        end = ".Lfunc_end";
    vtext_format_append(b, "%s%u", end, s);
}

/* a DW_FORM_string of a symbol's identifier (no escapes needed; the intern table's strings end
   at their length) */
static void debug_identifier_append(const cc *c, tbuf *b, uint32_t s)
{
    uint32_t name = c->syms[s].name;

    vtext_string_append(b, "\t.asciz\t\"");
    vtext_append(b, vintern_ents[name].s, vintern_ents[name].len);
    vtext_string_append(b, "\"\n");
}

/* a DW_FORM_string of a symbol's name as the assembler has it */
static void debug_linkage_name_append(cc *c, tbuf *b, uint32_t s)
{
    const attrs *a = codegen_symbol_attributes(c, s);

    if (!a->asmname) {
        debug_identifier_append(c, b, s);
        return;
    }
    vtext_string_append(b, "\t.asciz\t");
    codegen_string_quote_append(b, c->strs[a->asmname - 1].s);
    vtext_char_append(b, '\n');
}

/*
 * A function's DIE: where it is, and what it is called.  C: its name in the source.  The other
 * front end (C++, c->describe_location set): the symbol is the mangled linkage name, the source's
 * identifier is attrs' debug_name (0: none, an operator's or a closure's).
 */
static void debug_function_append(cc *c, tbuf *b, uint32_t s, int version,
                                  const char *word, const char *quad)
{
    const sym *y = &c->syms[s];
    const attrs *a = codegen_symbol_attributes(c, s);
    bool other = c->describe_location != NULL;
    int code = DEBUG_FUNCTION;

    if (other && a->debug_name)
        code = DEBUG_FUNCTION_LINKAGE;
    else if (other)
        code = DEBUG_FUNCTION_UNNAMED;
    vtext_format_append(b, "\t.byte\t%d\n", code);
    if (code == DEBUG_FUNCTION_LINKAGE) {
        vtext_string_append(b, "\t.asciz\t");
        codegen_string_quote_append(b, c->strs[a->debug_name - 1].s);
        vtext_char_append(b, '\n');
    } else if (code == DEBUG_FUNCTION) {
        debug_identifier_append(c, b, s);
    }
    if (code != DEBUG_FUNCTION)
        debug_linkage_name_append(c, b, s);
    vtext_format_append(b, "\t.%s\t", quad);
    codegen_symbol_name_append(c, b, s);
    if (version >= 4) { /* the length */
        vtext_format_append(b, "\n\t.%s\t", word);
        debug_function_end_append(c, b, s);
        vtext_char_append(b, '-');
        codegen_symbol_name_append(c, b, s);
    } else { /* the end's address */
        vtext_format_append(b, "\n\t.%s\t", quad);
        debug_function_end_append(c, b, s);
    }
    vtext_format_append(b, "\n\t.byte\t%d\n", y->sc != AST_STORAGE_STATIC);
}

/* the subprograms' abbreviations: name 3, linkage_name 110 (a string 8), low_pc 17 (addr 1),
   high_pc 18 (data4 6, below DWARF 4 an addr), external 63 (flag 12) */
static void debug_function_abbreviations_append(tbuf *b, int version)
{
    int high = 1;

    if (version >= 4)
        high = 6;
    vtext_format_append(
        b,
        "\t.byte\t%d, 46, 0, 3, 8, 17, 1, 18, %d, 63, 12, 0, 0\n"
        "\t.byte\t%d, 46, 0, 3, 8, 110, 8, 17, 1, 18, %d, 63, 12, 0, 0\n"
        "\t.byte\t%d, 46, 0, 110, 8, 17, 1, 18, %d, 63, 12, 0, 0\n"
        "\t.byte\t0\n",
        DEBUG_FUNCTION, high, DEBUG_FUNCTION_LINKAGE, high,
        DEBUG_FUNCTION_UNNAMED, high);
}

VPRIV void codegen_debug_unit_append(cc *c, tbuf *tail,
                                     const codegen_chunk *chunks,
                                     uint32_t chunk_count)
{
    const cc_options *o = c->o;
    /* one compile unit over .text: producer, C11, name, its line table, dir, pc range;
			   DWARF 5's unit header has the unit type and the address size before the abbreviations'
			   offset, 4's (and 2's, 3's) after it; below 4 the line table's offset is data4 and the
			   high pc an address */
    /* (x86: .word is 2 bytes there, so .short/.long/.quad; COFF: "dr" sections, section-relative offsets) */
    bool x = CODEGEN_IS_X64(c), w = CODEGEN_IS_WIN64(c);
    int v = CODEGEN_DWARF_VERSION(c);
    const char *half = x ? "short" : "hword", *word = x ? "long" : "word",
               *quad = x ? "quad" : "xword";
    const char *offset = w ? "secrel32" : word;
    /* COFF: a function outside .text (-ffunction-sections' .text$NAME COMDATs, a section
			   attribute) makes the unit's pc range DW_AT_ranges, one range per function, as clang
			   (a debugger finds the unit of an address by its ranges); not below DWARF 3.  ELF: so
			   does a function in a COMDAT group (C++ vague linkage) */
    bool ranges = false;

    for (uint32_t i = 0;
         !CODEGEN_IS_DARWIN(c) && v >= 3 && i < chunk_count && !ranges; i++) {
        uint32_t s = (uint32_t)(chunks[i].key >> 1);

        if (!debug_chunk_is_function(c, &chunks[i]))
            continue;
        if (w)
            ranges =
                (o->sections & 1) || codegen_symbol_attributes(c, s)->section;
        else
            ranges = c->syms[s].flags & AST_SYMBOL_FLAG_COMDAT;
    }
    vtext_format_append(tail,
                        "\t.text\n.Ldbg_text1:\n\t.section\t.debug_"
                        "abbrev,%s\n.Ldbg_abbrev:\n"
                        "\t.byte\t1, 17, 1, 37, 8, 19, 5, 3, 8, 16, "
                        "%s, 27, 8, 17, 1, %s, %s, 0, 0\n",
                        w ? "\"dr\"" : "\"\",@progbits", v >= 4 ? "23" : "6",
                        ranges ? "85" : "18",
                        ranges   ? (v >= 4 ? "23" : "6")
                        : v >= 4 ? "6"
                                 : "1");
    debug_function_abbreviations_append(tail, v);
    vtext_format_append(tail, "\t.section\t.debug_info,%s\n",
                        w ? "\"dr\"" : "\"\",@progbits");
    vtext_format_append(tail,
                        "\t.%s\t.Ldbg_cu_end-.Ldbg_cu_start\n.Ldbg_cu_"
                        "start:\n\t.%s\t%d\n",
                        word, half, v);
    if (v >= 5)
        vtext_format_append(tail, "\t.byte\t1, 8\n\t.%s\t.Ldbg_abbrev\n",
                            offset);
    else
        vtext_format_append(tail, "\t.%s\t.Ldbg_abbrev\n\t.byte\t8\n", offset);
    /* DW_LANG_C11, DW_LANG_C_plus_plus_14 (debuggers demangle by it) */
    int language = 29;

    if (c->describe_location)
        language = 33;
    vtext_format_append(tail,
                        "\t.byte\t1\n\t.asciz\t\"vcc\"\n\t.%s\t%d\n\t.asciz\t",
                        half, language);
    codegen_string_quote_append(tail, o->dbg_name ? o->dbg_name : "<stdin>");
    vtext_format_append(tail, "\n\t.%s\t.Lline_table_start0\n\t.asciz\t",
                        offset);
    codegen_string_quote_append(tail, o->dbg_dir ? o->dbg_dir : ".");
    if (ranges) {
        vtext_format_append(tail, "\n\t.%s\t0\n\t.%s\t.Ldbg_ranges\n", quad,
                            offset);
    } else if (v >= 4)
        vtext_format_append(tail,
                            "\n\t.%s\t.Ldbg_text0\n\t.%s\t.Ldbg_text1-."
                            "Ldbg_text0\n",
                            quad, word);
    else
        vtext_format_append(tail, "\n\t.%s\t.Ldbg_text0\n\t.%s\t.Ldbg_text1\n",
                            quad, quad);

    for (uint32_t i = 0; i < chunk_count; i++) {
        uint32_t function = (uint32_t)(chunks[i].key >> 1);

        /* (COFF: a weak function's name is a weak external, whose address in
           the object is not the code's: no entry) */
        if (debug_chunk_is_function(c, &chunks[i]) &&
            !(w && (c->syms[function].flags & AST_SYMBOL_FLAG_WEAK)))
            debug_function_append(c, tail, function, v, word, quad);
    }
    vtext_string_append(tail, "\t.byte\t0\n.Ldbg_cu_end:\n");
    if (ranges) {
        const char *name = ".debug_ranges", *flags = "\"\",@progbits";

        if (v >= 5)
            name = ".debug_rnglists";
        if (w)
            flags = "\"dr\"";
        vtext_format_append(tail, "\t.section\t%s,%s\n", name, flags);
        if (v >= 5)
            vtext_string_append(
                tail, "\t.long\t.Ldbg_rnglists_end-.Ldbg_rnglists_start\n"
                      ".Ldbg_rnglists_start:\n\t.short\t5\n\t.byte\t8, "
                      "0\n\t.long\t0\n");
        vtext_string_append(tail, ".Ldbg_ranges:\n");
        for (uint32_t i = 0; i < chunk_count; i++) {
            uint32_t function = (uint32_t)(chunks[i].key >> 1);

            if (!debug_chunk_is_function(c, &chunks[i]))
                continue;
            /* DWARF 5: DW_RLE_start_end (6); below: a begin, end pair (the base is the unit's 0) */
            vtext_string_append(tail,
                                v >= 5 ? "\t.byte\t6\n\t.quad\t" : "\t.quad\t");
            codegen_symbol_name_append(c, tail, function);
            vtext_string_append(tail, "\n\t.quad\t");
            debug_function_end_append(c, tail, function);
            vtext_char_append(tail, '\n');
        }
        vtext_string_append(tail, v >= 5 ? "\t.byte\t0\n.Ldbg_rnglists_end:\n"
                                         : "\t.quad\t0\n\t.quad\t0\n");
    }
    vtext_string_append(
        tail, w ? "\t.section\t.debug_line,\"dr\"\n.Lline_table_start0:\n"
                : "\t.section\t.debug_line,\"\",@progbits\n.Lline_table_"
                  "start0:\n");
}
