/*
 * debug-unit.cpp: -g: a translation unit's compile unit (.debug_abbrev, .debug_info, the ranges),
 * written after the unit's code as assembler text.  One DW_TAG_compile_unit with a
 * DW_TAG_subprogram per function emitted and the C types their signatures reach
 * (doc/PLAN-debuginfo.md); the line table is the assembler's, from the .loc rows
 * (debug-rows.cpp).  An entry's attributes are collected, then its abbreviation is found or made
 * from their shape; types are queued where referred to and written once, referred to by label.
 */
#include "common/literal.h"
#include "codegen-internal.h"

enum {
    DEBUG_TAG_ARRAY_TYPE = 0x01,
    DEBUG_TAG_ENUMERATION_TYPE = 0x04,
    DEBUG_TAG_FORMAL_PARAMETER = 0x05,
    DEBUG_TAG_MEMBER = 0x0d,
    DEBUG_TAG_POINTER_TYPE = 0x0f,
    DEBUG_TAG_STRUCTURE_TYPE = 0x13,
    DEBUG_TAG_SUBROUTINE_TYPE = 0x15,
    DEBUG_TAG_TYPEDEF = 0x16,
    DEBUG_TAG_UNION_TYPE = 0x17,
    DEBUG_TAG_UNSPECIFIED_PARAMETERS = 0x18,
    DEBUG_TAG_SUBRANGE_TYPE = 0x21,
    DEBUG_TAG_BASE_TYPE = 0x24,
    DEBUG_TAG_CONST_TYPE = 0x26,
    DEBUG_TAG_ENUMERATOR = 0x28,
    DEBUG_TAG_SUBPROGRAM = 0x2e,
    DEBUG_TAG_VOLATILE_TYPE = 0x35,
    DEBUG_TAG_RESTRICT_TYPE = 0x37,
    DEBUG_TAG_UNSPECIFIED_TYPE = 0x3b,
    DEBUG_TAG_ATOMIC_TYPE = 0x47,
};
enum {
    DEBUG_AT_NAME = 0x03,
    DEBUG_AT_BYTE_SIZE = 0x0b,
    DEBUG_AT_BIT_SIZE = 0x0d,
    DEBUG_AT_LOW_PC = 0x11,
    DEBUG_AT_HIGH_PC = 0x12,
    DEBUG_AT_CONST_VALUE = 0x1c,
    DEBUG_AT_PROTOTYPED = 0x27,
    DEBUG_AT_COUNT = 0x37,
    DEBUG_AT_DATA_MEMBER_LOCATION = 0x38,
    DEBUG_AT_DECLARATION = 0x3c,
    DEBUG_AT_ENCODING = 0x3e,
    DEBUG_AT_EXTERNAL = 0x3f,
    DEBUG_AT_TYPE = 0x49,
    DEBUG_AT_DATA_BIT_OFFSET = 0x6b,
    DEBUG_AT_LINKAGE_NAME = 0x6e,
};
/* DWARF's forms, and two that are written otherwise: a function's length (data4) and, below
   DWARF 4, its end (addr) */
enum {
    DEBUG_FORM_ADDRESS = 0x01,
    DEBUG_FORM_STRING = 0x08,
    DEBUG_FORM_FLAG = 0x0c,
    DEBUG_FORM_SDATA = 0x0d,
    DEBUG_FORM_UDATA = 0x0f,
    DEBUG_FORM_REFERENCE = 0x13, /* ref4 */
    DEBUG_FORM_LENGTH = 0xf0,
    DEBUG_FORM_END = 0xf1,
};
enum { DEBUG_ATTRIBUTES = 8, DEBUG_SHAPES = 96 };

typedef struct debug_attribute {
    uint16_t attribute;
    uint8_t form;
    const char *text; /* DEBUG_FORM_STRING, of number bytes */
    uint64_t number;  /* the value, type label or function symbol */
} debug_attribute;

typedef struct debug_die {
    uint16_t tag;
    bool children;
    uint8_t count;
    debug_attribute attributes[DEBUG_ATTRIBUTES];
} debug_die;

typedef struct debug_type {
    ast_qualified_type type;
    uint32_t tdef;
} debug_type;

typedef struct debug_writer {
    cc *c;
    tbuf *b;
    const char *word, *quad;
    debug_die shapes[DEBUG_SHAPES];
    uint32_t shape_count;
    debug_type *types; /* label i + 1's */
    uint32_t type_count, type_capacity;
    uint32_t *type_labels,
        *typedef_labels; /* by qualified type, by sym; 0: none yet */
} debug_writer;

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

/* an entry of tag, with children or not */
static debug_die debug_die_new(int tag, bool children)
{
    debug_die d = {};

    d.tag = (uint16_t)tag;
    d.children = children;
    return d;
}

static debug_attribute *debug_die_add(debug_die *d, int attribute, int form)
{
    debug_attribute *a = &d->attributes[d->count++];

    a->attribute = (uint16_t)attribute;
    a->form = (uint8_t)form;
    return a;
}

static void debug_die_number(debug_die *d, int attribute, int form,
                             uint64_t value)
{
    debug_die_add(d, attribute, form)->number = value;
}

/* a name of length bytes (the intern table's strings end at their length) */
static void debug_die_text(debug_die *d, int attribute, const char *text,
                           size_t length)
{
    debug_attribute *a = debug_die_add(d, attribute, DEBUG_FORM_STRING);

    a->text = text;
    a->number = length;
}

static void debug_die_identifier(debug_die *d, uint32_t name)
{
    if (name)
        debug_die_text(d, DEBUG_AT_NAME, vintern_ents[name].s,
                       vintern_ents[name].len);
}

/* the abbreviation code of d's shape (code 1 is the compile unit's) */
static uint32_t debug_abbreviation_find(debug_writer *w, const debug_die *d)
{
    for (uint32_t i = 0; i < w->shape_count; i++) {
        const debug_die *s = &w->shapes[i];
        bool same = s->tag == d->tag && s->children == d->children &&
                    s->count == d->count;

        for (int k = 0; same && k < d->count; k++)
            same = s->attributes[k].attribute == d->attributes[k].attribute &&
                   s->attributes[k].form == d->attributes[k].form;
        if (same)
            return i + 2;
    }
    if (w->shape_count == DEBUG_SHAPES)
        VBUG("too many shapes of debug entries");
    w->shapes[w->shape_count++] = *d;
    return w->shape_count + 1;
}

static void debug_die_write(debug_writer *w, const debug_die *d)
{
    tbuf *b = w->b;

    vtext_format_append(b, "\t.uleb128\t%u\n", debug_abbreviation_find(w, d));
    for (int k = 0; k < d->count; k++) {
        const debug_attribute *a = &d->attributes[k];

        switch (a->form) {
        case DEBUG_FORM_STRING:
            vtext_string_append(b, "\t.asciz\t\"");
            for (size_t i = 0; i < a->number; i++) {
                if (a->text[i] == '"' || a->text[i] == '\\')
                    vtext_char_append(b, '\\');
                vtext_char_append(b, a->text[i]);
            }
            vtext_string_append(b, "\"\n");
            break;
        case DEBUG_FORM_FLAG:
            vtext_format_append(b, "\t.byte\t%llu\n",
                                (unsigned long long)a->number);
            break;
        case DEBUG_FORM_UDATA:
            vtext_format_append(b, "\t.uleb128\t%llu\n",
                                (unsigned long long)a->number);
            break;
        case DEBUG_FORM_SDATA:
            vtext_format_append(b, "\t.sleb128\t%lld\n", (long long)a->number);
            break;
        case DEBUG_FORM_REFERENCE:
            vtext_format_append(b, "\t.%s\t.Ldbg_t%llu-.Ldbg_cu\n", w->word,
                                (unsigned long long)a->number);
            break;
        case DEBUG_FORM_ADDRESS: /* a function's start */
            vtext_format_append(b, "\t.%s\t", w->quad);
            codegen_symbol_name_append(w->c, b, (uint32_t)a->number);
            vtext_char_append(b, '\n');
            break;
        case DEBUG_FORM_LENGTH: /* its length, or its end below DWARF 4 */
        case DEBUG_FORM_END:
            if (a->form == DEBUG_FORM_LENGTH)
                vtext_format_append(b, "\t.%s\t", w->word);
            else
                vtext_format_append(b, "\t.%s\t", w->quad);
            debug_function_end_append(w->c, b, (uint32_t)a->number);
            if (a->form == DEBUG_FORM_LENGTH) {
                vtext_char_append(b, '-');
                codegen_symbol_name_append(w->c, b, (uint32_t)a->number);
            }
            vtext_char_append(b, '\n');
            break;
        default:
            VBUG("a debug entry's form");
        }
    }
}

static void debug_children_end(debug_writer *w)
{
    vtext_string_append(w->b, "\t.byte\t0\n");
}

/* the label of the entry of t (as the typedef named tdef, 0: none), queued when new; 0: void */
static uint32_t debug_type_label(debug_writer *w, ast_qualified_type t,
                                 uint32_t tdef)
{
    cc *c = w->c;

    if (!tdef && !AST_QUALIFIED_TYPE_QUALIFIERS(t) &&
        T(c, t)->kind == AST_TYPE_VOID &&
        !(T(c, t)->flags & AST_TYPE_FLAG_ATOMIC))
        return 0;
    /* the labels by type, and by typedef name */
    uint32_t *slot = &w->type_labels[t];

    if (tdef)
        slot = &w->typedef_labels[tdef];
    if (*slot)
        return *slot;
    if (w->type_count == w->type_capacity) {
        w->type_capacity = 2 * w->type_capacity + 64;
        w->types = (debug_type *)realloc(w->types,
                                         w->type_capacity * sizeof *w->types);
        if (!w->types)
            vout_of_memory();
    }
    w->types[w->type_count++] = VCC_LITERAL(debug_type, t, tdef);
    *slot = w->type_count;
    return w->type_count;
}

/* the typedef to name a declaration's type by: tdef (the typedef name it was written with) when
   it is the type itself (a pointer to it records it too), else none */
static uint32_t debug_typedef_of(const cc *c, uint32_t tdef,
                                 ast_qualified_type t)
{
    if (tdef && c->syms[tdef].ty == t)
        return tdef;
    return 0;
}

static void debug_die_type(debug_writer *w, debug_die *d, ast_qualified_type t,
                           uint32_t tdef)
{
    uint32_t label = debug_type_label(w, t, tdef);

    if (label)
        debug_die_number(d, DEBUG_AT_TYPE, DEBUG_FORM_REFERENCE, label);
}

static void debug_base_write(debug_writer *w, ast_qualified_type t)
{
    static const struct {
        const char *name;
        uint8_t encoding;
    } base[] = {
        [AST_TYPE_VOID] = {"void", 0},
        [AST_TYPE_BOOL] = {"_Bool", 2},
        [AST_TYPE_CHAR] = {"char", 6},
        [AST_TYPE_SIGNED_CHAR] = {"signed char", 6},
        [AST_TYPE_UNSIGNED_CHAR] = {"unsigned char", 8},
        [AST_TYPE_SHORT] = {"short", 5},
        [AST_TYPE_UNSIGNED_SHORT] = {"unsigned short", 7},
        [AST_TYPE_INT] = {"int", 5},
        [AST_TYPE_UNSIGNED_INT] = {"unsigned int", 7},
        [AST_TYPE_LONG] = {"long", 5},
        [AST_TYPE_UNSIGNED_LONG] = {"unsigned long", 7},
        [AST_TYPE_LONG_LONG] = {"long long", 5},
        [AST_TYPE_UNSIGNED_LONG_LONG] = {"unsigned long long", 7},
        [AST_TYPE_INT128] = {"__int128", 5},
        [AST_TYPE_UINT128] = {"unsigned __int128", 7},
        [AST_TYPE_BFLOAT16] = {"__bf16", 4},
        [AST_TYPE_FLOAT16] = {"_Float16", 4},
        [AST_TYPE_FLOAT] = {"float", 4},
        [AST_TYPE_DOUBLE] = {"double", 4},
        [AST_TYPE_LONG_DOUBLE] = {"long double", 4},
        [AST_TYPE_FLOAT128] = {"__float128", 4},
    };
    const ctype *y = T(w->c, t);
    debug_die d = debug_die_new(DEBUG_TAG_BASE_TYPE, false);
    int encoding = base[y->kind].encoding;

    /* plain char is unsigned on Linux arm64 */
    if (y->kind == AST_TYPE_CHAR && w->c->o->target == PP_LINUX_AARCH64)
        encoding = 8;
    debug_die_text(&d, DEBUG_AT_NAME, base[y->kind].name,
                   strlen(base[y->kind].name));
    debug_die_number(&d, DEBUG_AT_ENCODING, DEBUG_FORM_UDATA,
                     (uint64_t)encoding);
    debug_die_number(&d, DEBUG_AT_BYTE_SIZE, DEBUG_FORM_UDATA, y->size);
    debug_die_write(w, &d);
}

/* _Complex T, C's struct { T re, im; }: a base type, DW_ATE_complex_float (an integer element:
   clang's lo_user) */
static void debug_complex_write(debug_writer *w, ast_qualified_type t)
{
    const cc *c = w->c;
    const tag *r = &c->tags[T(c, t)->aux];
    debug_die d = debug_die_new(DEBUG_TAG_BASE_TYPE, false);
    int encoding = 3;

    if (T(c, r->f[0].ty)->kind < AST_TYPE_BFLOAT16)
        encoding = 0x80;
    debug_die_text(&d, DEBUG_AT_NAME, "complex", 7);
    debug_die_number(&d, DEBUG_AT_ENCODING, DEBUG_FORM_UDATA,
                     (uint64_t)encoding);
    debug_die_number(&d, DEBUG_AT_BYTE_SIZE, DEBUG_FORM_UDATA, r->size);
    debug_die_write(w, &d);
}

static void debug_record_write(debug_writer *w, ast_qualified_type t)
{
    cc *c = w->c;
    const tag *r = &c->tags[T(c, t)->aux];
    bool complete = r->flags & AST_RECORD_FLAG_COMPLETE;
    int kind = DEBUG_TAG_STRUCTURE_TYPE;

    if (r->flags & AST_RECORD_FLAG_UNION)
        kind = DEBUG_TAG_UNION_TYPE;
    debug_die d = debug_die_new(kind, complete);

    debug_die_identifier(&d, r->name);
    if (complete)
        debug_die_number(&d, DEBUG_AT_BYTE_SIZE, DEBUG_FORM_UDATA, r->size);
    else
        debug_die_number(&d, DEBUG_AT_DECLARATION, DEBUG_FORM_FLAG, 1);
    debug_die_write(w, &d);
    if (!complete)
        return;
    for (uint32_t i = 0; i < r->nfield; i++) {
        const field *f = &r->f[i];

        if (f->bitfield && !f->name)
            continue;
        debug_die m = debug_die_new(DEBUG_TAG_MEMBER, false);

        debug_die_identifier(&m, f->name);
        debug_die_type(w, &m, f->ty, debug_typedef_of(c, f->tdef, f->ty));
        if (f->bitfield) {
            debug_die_number(&m, DEBUG_AT_BIT_SIZE, DEBUG_FORM_UDATA, f->width);
            debug_die_number(&m, DEBUG_AT_DATA_BIT_OFFSET, DEBUG_FORM_UDATA,
                             f->off);
        } else {
            debug_die_number(&m, DEBUG_AT_DATA_MEMBER_LOCATION,
                             DEBUG_FORM_UDATA, f->off / 8);
        }
        debug_die_write(w, &m);
    }
    debug_children_end(w);
}

static void debug_enum_write(debug_writer *w, ast_qualified_type t)
{
    cc *c = w->c;
    const ctype *y = T(c, t);
    const tag *r = &c->tags[y->aux];
    debug_die d = debug_die_new(DEBUG_TAG_ENUMERATION_TYPE, true);
    bool negative = false;

    for (uint32_t i = 0; i < r->nenum; i++)
        negative = negative || c->syms[c->lists[r->enums + i]].val < 0;
    debug_die_identifier(&d, r->name);
    debug_die_number(&d, DEBUG_AT_BYTE_SIZE, DEBUG_FORM_UDATA, y->size);
    debug_die_type(w, &d, y->base, 0);
    debug_die_write(w, &d);
    for (uint32_t i = 0; i < r->nenum; i++) {
        const sym *s = &c->syms[c->lists[r->enums + i]];
        debug_die v = debug_die_new(DEBUG_TAG_ENUMERATOR, false);
        int form = DEBUG_FORM_UDATA;

        if (negative)
            form = DEBUG_FORM_SDATA;
        debug_die_identifier(&v, s->name);
        debug_die_number(&v, DEBUG_AT_CONST_VALUE, form, (uint64_t)s->val);
        debug_die_write(w, &v);
    }
    debug_children_end(w);
}

static void debug_array_write(debug_writer *w, ast_qualified_type t)
{
    cc *c = w->c;
    ast_qualified_type element = t;
    debug_die d = debug_die_new(DEBUG_TAG_ARRAY_TYPE, true);

    while (T(c, element)->kind == AST_TYPE_ARRAY)
        element = T(c, element)->base;
    debug_die_type(w, &d, element, 0);
    debug_die_write(w, &d);
    for (ast_qualified_type a = t; T(c, a)->kind == AST_TYPE_ARRAY;
         a = T(c, a)->base) {
        const ctype *y = T(c, a);
        debug_die r = debug_die_new(DEBUG_TAG_SUBRANGE_TYPE, false);

        if (!(y->flags & (AST_TYPE_FLAG_INCOMPLETE | AST_TYPE_FLAG_VLA)))
            debug_die_number(&r, DEBUG_AT_COUNT, DEBUG_FORM_UDATA, y->n);
        debug_die_write(w, &r);
    }
    debug_children_end(w);
}

static void debug_function_type_write(debug_writer *w, ast_qualified_type t)
{
    cc *c = w->c;
    const ctype *y = T(c, t);
    debug_die d = debug_die_new(DEBUG_TAG_SUBROUTINE_TYPE, true);

    debug_die_type(w, &d, y->base, 0);
    if (!(y->flags & AST_TYPE_FLAG_NO_PROTOTYPE))
        debug_die_number(&d, DEBUG_AT_PROTOTYPED, DEBUG_FORM_FLAG, 1);
    debug_die_write(w, &d);
    for (uint32_t i = 0; i < y->n; i++) {
        debug_die p = debug_die_new(DEBUG_TAG_FORMAL_PARAMETER, false);

        debug_die_type(w, &p, c->params[y->aux + i], 0);
        debug_die_write(w, &p);
    }
    if (y->flags & AST_TYPE_FLAG_VARIADIC) {
        debug_die v = debug_die_new(DEBUG_TAG_UNSPECIFIED_PARAMETERS, false);

        debug_die_write(w, &v);
    }
    debug_children_end(w);
}

/* a pointer's, a qualified type's or a typedef's entry: tag over the type under it */
static void debug_wrapper_write(debug_writer *w, int kind,
                                ast_qualified_type under, uint32_t name,
                                uint64_t size)
{
    debug_die d = debug_die_new(kind, false);

    debug_die_identifier(&d, name);
    if (size)
        debug_die_number(&d, DEBUG_AT_BYTE_SIZE, DEBUG_FORM_UDATA, size);
    debug_die_type(w, &d, under, 0);
    debug_die_write(w, &d);
}

static void debug_type_write(debug_writer *w, ast_qualified_type t,
                             uint32_t tdef)
{
    cc *c = w->c;
    const ctype *y = T(c, t);
    int q = AST_QUALIFIED_TYPE_QUALIFIERS(t);

    if (tdef) {
        /* the typedef's own qualifiers are in its type already */
        debug_wrapper_write(w, DEBUG_TAG_TYPEDEF, c->syms[tdef].ty,
                            c->syms[tdef].name, 0);
    } else if (q & AST_QUALIFIER_CONST) {
        debug_wrapper_write(w, DEBUG_TAG_CONST_TYPE,
                            t & ~(ast_qualified_type)AST_QUALIFIER_CONST, 0, 0);
    } else if (q & AST_QUALIFIER_VOLATILE) {
        debug_wrapper_write(w, DEBUG_TAG_VOLATILE_TYPE,
                            t & ~(ast_qualified_type)AST_QUALIFIER_VOLATILE, 0,
                            0);
    } else if (q & AST_QUALIFIER_RESTRICT) {
        debug_wrapper_write(w, DEBUG_TAG_RESTRICT_TYPE,
                            AST_QUALIFIED_TYPE_UNQUALIFIED(t), 0, 0);
    } else if (y->flags & AST_TYPE_FLAG_ATOMIC) {
        debug_wrapper_write(w, DEBUG_TAG_ATOMIC_TYPE,
                            AST_QUALIFIED_TYPE(y->n, 0), 0, 0);
    } else if (y->kind == AST_TYPE_POINTER) {
        debug_wrapper_write(w, DEBUG_TAG_POINTER_TYPE, y->base, 0, y->size);
    } else if (y->kind == AST_TYPE_ARRAY) {
        debug_array_write(w, t);
    } else if (y->kind == AST_TYPE_FUNCTION) {
        debug_function_type_write(w, t);
    } else if (y->kind == AST_TYPE_STRUCT &&
               (c->tags[y->aux].flags & AST_RECORD_FLAG_COMPLEX)) {
        debug_complex_write(w, t);
    } else if (y->kind == AST_TYPE_STRUCT || y->kind == AST_TYPE_UNION) {
        debug_record_write(w, t);
    } else if (y->kind == AST_TYPE_ENUM) {
        debug_enum_write(w, t);
    } else if (y->kind <= AST_TYPE_FLOAT128) {
        debug_base_write(w, t);
    } else {
        /* vectors, _BitInt, scalable types: not described yet */
        debug_die d = debug_die_new(DEBUG_TAG_UNSPECIFIED_TYPE, false);

        debug_die_text(&d, DEBUG_AT_NAME, "?", 1);
        debug_die_write(w, &d);
    }
}

/* the entries of every type referred to, those they refer to too */
static void debug_types_write(debug_writer *w)
{
    for (uint32_t i = 0; i < w->type_count; i++) {
        debug_type t = w->types[i];

        vtext_format_append(w->b, ".Ldbg_t%u:\n", i + 1);
        debug_type_write(w, t.type, t.tdef);
    }
}

static void debug_abbreviations_write(const debug_writer *w, tbuf *b)
{
    for (uint32_t i = 0; i < w->shape_count; i++) {
        const debug_die *s = &w->shapes[i];

        vtext_format_append(b, "\t.uleb128\t%u\n\t.uleb128\t%d\n\t.byte\t%d\n",
                            i + 2, s->tag, s->children);
        for (int k = 0; k < s->count; k++) {
            int form = s->attributes[k].form;

            if (form == DEBUG_FORM_LENGTH)
                form = 6; /* data4 */
            else if (form == DEBUG_FORM_END)
                form = DEBUG_FORM_ADDRESS;
            vtext_format_append(b, "\t.uleb128\t%d\n\t.uleb128\t%d\n",
                                s->attributes[k].attribute, form);
        }
        vtext_string_append(b, "\t.byte\t0, 0\n");
    }
    vtext_string_append(b, "\t.byte\t0\n");
}

/*
 * A function's DIE: where it is, what it is called and, in C, its type and parameters.  C: its name
 * in the source.  The other front end (C++, c->describe_location set): the symbol is the mangled
 * linkage name, the source's identifier is attrs' debug_name (0: none, an operator's or a
 * closure's), and its types are not described yet (D4).
 */
static void debug_function_append(debug_writer *w, uint32_t function,
                                  int version)
{
    cc *c = w->c;
    const sym *y = &c->syms[function];
    const attrs *a = codegen_symbol_attributes(c, function);
    bool other = c->describe_location != NULL;
    uint32_t parameters = 0;

    if (!other)
        parameters = N(y->body)->aux;
    const ctype *type = T(c, y->ty);
    bool variadic = !other && (type->flags & AST_TYPE_FLAG_VARIADIC);
    debug_die d = debug_die_new(DEBUG_TAG_SUBPROGRAM, parameters || variadic);

    if (!other) {
        debug_die_identifier(&d, y->name);
    } else {
        if (a->debug_name)
            debug_die_text(&d, DEBUG_AT_NAME, c->strs[a->debug_name - 1].s,
                           strlen(c->strs[a->debug_name - 1].s));
        if (a->asmname)
            debug_die_text(&d, DEBUG_AT_LINKAGE_NAME, c->strs[a->asmname - 1].s,
                           strlen(c->strs[a->asmname - 1].s));
        else
            debug_die_text(&d, DEBUG_AT_LINKAGE_NAME, vintern_ents[y->name].s,
                           vintern_ents[y->name].len);
    }
    debug_die_number(&d, DEBUG_AT_LOW_PC, DEBUG_FORM_ADDRESS, function);
    if (version >= 4)
        debug_die_number(&d, DEBUG_AT_HIGH_PC, DEBUG_FORM_LENGTH, function);
    else
        debug_die_number(&d, DEBUG_AT_HIGH_PC, DEBUG_FORM_END, function);
    debug_die_number(&d, DEBUG_AT_EXTERNAL, DEBUG_FORM_FLAG,
                     y->sc != AST_STORAGE_STATIC);
    if (!other) {
        debug_die_type(w, &d, type->base, 0);
        if (!(type->flags & AST_TYPE_FLAG_NO_PROTOTYPE))
            debug_die_number(&d, DEBUG_AT_PROTOTYPED, DEBUG_FORM_FLAG, 1);
    }
    debug_die_write(w, &d);
    for (uint32_t i = 0; i < parameters; i++) {
        const sym *p = &c->syms[c->lists[N(y->body)->a + i]];
        debug_die f = debug_die_new(DEBUG_TAG_FORMAL_PARAMETER, false);

        debug_die_identifier(&f, p->name);
        debug_die_type(w, &f, p->ty, debug_typedef_of(c, p->tdef, p->ty));
        debug_die_write(w, &f);
    }
    if (variadic) {
        debug_die v = debug_die_new(DEBUG_TAG_UNSPECIFIED_PARAMETERS, false);

        debug_die_write(w, &v);
    }
    if (d.children)
        debug_children_end(w);
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
    vtext_format_append(tail, "\t.section\t.debug_info,%s\n",
                        w ? "\"dr\"" : "\"\",@progbits");
    vtext_format_append(
        tail,
        ".Ldbg_cu:\n\t.%s\t.Ldbg_cu_end-.Ldbg_cu_start\n.Ldbg_cu_"
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

    static_assert(sizeof(debug_writer) < 32768, "on the stack");
    debug_writer writer = {};

    writer.c = c;
    writer.b = tail;
    writer.word = word;
    writer.quad = quad;
    writer.type_labels = (uint32_t *)calloc(
        (size_t)AST_QUALIFIED_TYPE(c->nty, 0), sizeof(uint32_t));
    writer.typedef_labels = (uint32_t *)calloc(c->nsym, sizeof(uint32_t));
    if (!writer.type_labels || !writer.typedef_labels)
        vout_of_memory();
    for (uint32_t i = 0; i < chunk_count; i++) {
        uint32_t function = (uint32_t)(chunks[i].key >> 1);

        /* (COFF: a weak function's name is a weak external, whose address in
           the object is not the code's: no entry) */
        if (debug_chunk_is_function(c, &chunks[i]) &&
            !(w && (c->syms[function].flags & AST_SYMBOL_FLAG_WEAK)))
            debug_function_append(&writer, function, v);
    }
    debug_types_write(&writer);
    free(writer.types);
    free(writer.type_labels);
    free(writer.typedef_labels);
    vtext_string_append(tail, "\t.byte\t0\n.Ldbg_cu_end:\n");
    vtext_format_append(tail, "\t.section\t.debug_abbrev,%s\n",
                        w ? "\"dr\"" : "\"\",@progbits");
    debug_abbreviations_write(&writer, tail);
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
