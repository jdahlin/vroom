/* ast-builder.cpp: the AST of a translation unit built by another front end
   (include/frontend/c/ast-builder.h), through C's own constructors */
#include "cc-internal.h"
#include "codegen/emit.h"
#include "frontend/c/ast-builder.h"

cc *ast_builder_open(pp *p, const cc_options *o)
{
    return cc_translation_unit_open(p, o);
}

struct ast *ast_builder_ast(cc *c)
{
    return &c->ast;
}

int ast_builder_emit(cc *c)
{
    if (setjmp(c->bail))
        return c->errors + 1;
    if (c->o->emit && !c->errors)
        c->errors += codegen_unit_emit(c, c->o);
    return c->errors;
}

void ast_builder_close(cc *c)
{
    cc_translation_unit_close(c);
}

static uint32_t ast_builder_identifier(cc *c, const char *name, size_t length);

/* ---- types ---- */

ast_qualified_type ast_builder_type_basic(int kind)
{
    return cc_type_basic(kind);
}

ast_qualified_type ast_builder_type_qualified(cc *c, ast_qualified_type t,
                                              int qualifiers)
{
    return cc_type_qualify(c, t, qualifiers);
}

ast_qualified_type ast_builder_type_pointer(cc *c, ast_qualified_type to)
{
    return cc_type_pointer(c, to);
}

ast_qualified_type ast_builder_type_array(cc *c, ast_qualified_type element,
                                          uint64_t count, bool known)
{
    if (!known)
        return cc_type_array(c, element, 0, AST_TYPE_FLAG_INCOMPLETE);
    return cc_type_array(c, element, count, 0);
}

ast_qualified_type
ast_builder_type_function(cc *c, ast_qualified_type result,
                          const ast_qualified_type *parameters, uint32_t count,
                          bool variadic)
{
    int flags = 0;

    if (variadic)
        flags = AST_TYPE_FLAG_VARIADIC;
    return cc_type_function(c, result, parameters, count, flags);
}

ast_qualified_type ast_builder_type_ptrdiff(cc *c)
{
    return c->t_ptrdiff;
}

ast_qualified_type ast_builder_type_va_list(cc *c)
{
    return c->t_valist;
}

uint64_t ast_builder_type_size(cc *c, ast_qualified_type t)
{
    return cc_type_size(c, t);
}

ast_qualified_type ast_builder_type_record(cc *c, const char *name,
                                           size_t length, bool is_union)
{
    uint32_t ti = cc_tag_new(c, is_union ? AST_TYPE_UNION : AST_TYPE_STRUCT,
                             ast_builder_identifier(c, name, length), 0);

    return AST_QUALIFIED_TYPE(c->tags[ti].ty, 0);
}

void ast_builder_type_record_complete(
    cc *c, ast_qualified_type record, const ast_qualified_type *fields,
    const char *const *names, const size_t *lengths, const uint32_t *aligned,
    const uint8_t *packed, const int32_t *widths, uint32_t count)
{
    tag *r = &c->tags[AST_TYPE(c, record)->aux];

    r->nfield = count;
    r->f = (field *)(cc_heap_allocate(c, (count ? count : 1) * sizeof(field)));
    memset(r->f, 0, (count ? count : 1) * sizeof(field));
    for (uint32_t i = 0; i < count; i++) {
        r->f[i].ty = fields[i];
        r->f[i].name = ast_builder_identifier(c, names[i], lengths[i]);
        if (widths && widths[i] >= 0) {
            r->f[i].bitfield = 1;
            r->f[i].width = (uint16_t)widths[i];
        }
    }
    r->flags |= AST_RECORD_FLAG_COMPLETE;
    cc_layout_record(c, r, aligned, packed);
}

void ast_builder_type_record_in_memory(cc *c, ast_qualified_type record)
{
    c->tags[AST_TYPE(c, record)->aux].flags |= AST_RECORD_FLAG_CALLS_IN_MEMORY;
}

uint64_t ast_builder_type_record_offset(cc *c, ast_qualified_type record,
                                        uint32_t field)
{
    return c->tags[AST_TYPE(c, record)->aux].f[field].off / 8;
}

uint64_t ast_builder_type_record_bit_offset(cc *c, ast_qualified_type record,
                                            uint32_t field)
{
    return c->tags[AST_TYPE(c, record)->aux].f[field].off;
}

/* ---- symbols ---- */

/* an interned identifier of a name that need not outlive the call */
static uint32_t ast_builder_identifier(cc *c, const char *name, size_t length)
{
    char *padded;

    if (!length)
        return 0;
    padded = (char *)cc_heap_allocate(c, length + 17);
    memcpy(padded, name, length);
    memset(padded + length, 0, 17);
    return vintern_copy(padded, length);
}

uint32_t ast_builder_symbol(cc *c, int kind, const char *name, size_t length,
                            ast_qualified_type t, int storage, uint32_t flags,
                            uint32_t depth)
{
    uint32_t s =
        cc_symbol_new(c, kind, ast_builder_identifier(c, name, length), t, 0);

    c->syms[s].sc = (uint8_t)storage;
    c->syms[s].flags = flags;
    c->syms[s].depth = depth;
    return s;
}

/* the symbol's attribute record, made at its first use */
static attrs *ast_builder_attributes(cc *c, uint32_t s)
{
    if (!c->syms[s].attrs) {
        c->syms[s].attrs = c->nattr++;
        memset(&c->attr[c->syms[s].attrs], 0, sizeof(attrs));
    }
    return &c->attr[c->syms[s].attrs];
}

void ast_builder_symbol_constructor(cc *c, uint32_t s)
{
    ast_builder_attributes(c, s)->constructor = 1;
}

void ast_builder_symbol_keep(cc *c, uint32_t s)
{
    ast_builder_attributes(c, s)->used = 1;
    /* (codegen's roots are the top-level symbols, a block's static too) */
    if (c->syms[s].depth)
        ast_builder_toplevel(c, s);
}

/* a string of the symbol's attributes: its index + 1 */
static uint32_t ast_builder_attribute_string(cc *c, const char *name,
                                             size_t length)
{
    char *keep = (char *)cc_heap_allocate(c, length + 1);
    uint32_t i = c->nstr++;

    memcpy(keep, name, length);
    keep[length] = 0;
    c->strs[i] = VCC_LITERAL(cstr, keep, (uint32_t)length + 1, 1,
                             (uint8_t)LEX_TOKEN_STRING);
    return i + 1;
}

void ast_builder_symbol_assembler_name(cc *c, uint32_t s, const char *name,
                                       size_t length)
{
    ast_builder_attributes(c, s)->asmname =
        ast_builder_attribute_string(c, name, length);
}

void ast_builder_symbol_section(cc *c, uint32_t s, const char *name,
                                size_t length)
{
    ast_builder_attributes(c, s)->section =
        ast_builder_attribute_string(c, name, length);
}

void ast_builder_symbol_align(cc *c, uint32_t s, uint32_t alignment)
{
    if (alignment > c->syms[s].align)
        c->syms[s].align = alignment;
}

void ast_builder_symbol_flags_add(cc *c, uint32_t s, uint32_t flags)
{
    c->syms[s].flags |= flags;
}

void ast_builder_symbol_body(cc *c, uint32_t s, uint32_t node)
{
    c->syms[s].body = node;
}

void ast_builder_toplevel(cc *c, uint32_t s)
{
    c->toplevel[c->ntoplevel++] = s;
}

/* ---- nodes ---- */

uint32_t ast_builder_node(cc *c, int op, ast_qualified_type t, uint32_t a,
                          uint32_t b, uint32_t c_, uint16_t aux, uint8_t flags)
{
    uint32_t n = cc_node_new(c, op, t, 0);

    c->nodes[n].a = a;
    c->nodes[n].b = b;
    c->nodes[n].c = c_;
    c->nodes[n].aux = aux;
    c->nodes[n].flags = flags;
    return n;
}

uint32_t ast_builder_member(cc *c, uint32_t object, uint32_t field,
                            ast_qualified_type t)
{
    uint32_t n = cc_node_new(c, AST_NODE_MEMBER, t, 0);
    ast_qualified_type record =
        AST_QUALIFIED_TYPE_UNQUALIFIED(c->nodes[object].ty);

    c->nodes[n].a = object;
    c->nodes[n].b = AST_TYPE(c, record)->aux;
    c->nodes[n].c = field;
    c->nodes[n].flags = c->nodes[object].flags & AST_NODE_FLAG_LVALUE;
    return n;
}

uint32_t ast_builder_list(cc *c, const uint32_t *items, uint32_t count)
{
    cc_id_list l;

    cc_id_list_init(&l);
    for (uint32_t i = 0; i < count; i++)
        cc_id_list_append(c, &l, items[i]);
    return cc_id_list_freeze(c, &l);
}

uint32_t ast_builder_atomic_load(cc *c, ast_qualified_type t, uint32_t pointer,
                                 uint32_t order)
{
    uint32_t arguments[2] = {pointer, order};

    return ast_builder_node(c, AST_NODE_BUILTIN, t, 0,
                            ast_builder_list(c, arguments, 2), 2,
                            CC_BUILTIN_ATOMIC_LOAD_N, 0);
}

uint32_t ast_builder_integer(cc *c, ast_qualified_type t, int64_t value)
{
    /* the constant's value as C keeps it, in 128 bits: an unsigned type's
       is the 64 bits zero-extended */
    int k = AST_TYPE_KIND(c, t);
    if (k == AST_TYPE_UNSIGNED_LONG || k == AST_TYPE_UNSIGNED_LONG_LONG ||
        k == AST_TYPE_UINT128)
        return cc_node_integer_new(c, (i128)(uint64_t)value, t, 0);
    return cc_node_integer_new(c, (i128)value, t, 0);
}

uint32_t ast_builder_floating(cc *c, ast_qualified_type t, double value)
{
    return cc_node_float_new(c, value, t, 0);
}

uint32_t ast_builder_string(cc *c, ast_qualified_type element,
                            const void *bytes, uint32_t count, uint8_t size,
                            uint8_t kind)
{
    size_t n = (size_t)count * size;
    char *keep = (char *)cc_heap_allocate(c, n);
    uint32_t i = c->nstr++, r;

    memcpy(keep, bytes, n);
    c->strs[i] = VCC_LITERAL(cstr, keep, count, size, kind);
    r = cc_node_new(c, AST_NODE_STRING, cc_type_array(c, element, count, 0), 0);
    c->nodes[r].a = i;
    c->nodes[r].flags = AST_NODE_FLAG_LVALUE;
    return r;
}

uint32_t ast_builder_label(cc *c, const char *name, size_t length)
{
    uint32_t l = c->nlabel++;

    memset(&c->labels[l], 0, sizeof(label));
    c->labels[l].name = ast_builder_identifier(c, name, length);
    return l;
}

uint32_t ast_builder_label_define(cc *c, uint32_t l, uint32_t statement)
{
    uint32_t n = cc_statement_new(c, AST_NODE_LABEL, 0, l, 0, statement);

    c->labels[l].defined = n;
    return n;
}

uint32_t ast_builder_case(cc *c, int64_t value, uint32_t s)
{
    uint32_t n =
        cc_statement_new(c, AST_NODE_CASE, 0, c->nipool, c->nipool + 1, s);

    c->ipool[c->nipool] = (i128)value;
    c->ipool[c->nipool + 1] = (i128)value;
    c->nipool += 2;
    return n;
}
