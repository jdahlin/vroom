/* symbols.cpp: symbols: functions and variables, constructors and destructors called (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* ---- symbols -------------------------------------------------------- */

void c_lowering::spelling(entity_index e, owned_buffer &out)
{
    uint32_t id = analysis.entity_table[e].name;
    if (!id)
        return;
    size_t length = 0;
    /* the identifier's number is the preprocessor's plus one */
    const char *s = tokens.pp.identifier_spelling(id - 1, &length);
    vbuffer_append(out.get(), s, length);
}

/* the storage and flags of the symbol of e, a function or a variable not
   local to a function */
void c_lowering::symbol_linkage(entity_index e, int &storage,
                                uint32_t &flags) const
{
    const entity &x = analysis.entity_table[e];
    storage = AST_STORAGE_NONE;
    if (x.linkage_of_name == linkage::internal &&
        !analysis.specialization_visible(e))
        storage = AST_STORAGE_STATIC;
    /* [impl.clang]: an entity whose type has no linkage (an unnamed
       class's) cannot be named in another translation unit, so its symbol
       is local, its name unmangled as an external one's */
    else if (x.defined && without_linkage(x.type))
        storage = AST_STORAGE_STATIC;
    else if (!x.defined && x.kind == entity_kind::variable)
        storage = AST_STORAGE_EXTERN;
    flags = AST_SYMBOL_FLAG_GLOBAL;
    /* [basic.def.odr]/11: an inline function or variable is defined in
       every translation unit that odr-uses it: emitted where referenced,
       weak so that the copies link as one ([abi:vague-inline] wants a
       COMDAT group too, which C's code generator does not make yet) */
    instantiation_kind explicit_ = analysis.explicitly_instantiated(e);
    if (explicit_ == instantiation_kind::definition)
        /* [temp.explicit]/10's explicit instantiation definition: emitted
           whether used or not, weak (clang's weak_odr) */
        flags |= AST_SYMBOL_FLAG_WEAK;
    else if (x.inline_ && explicit_ != instantiation_kind::declaration &&
             !(x.gnu_flags & gnu_inline_definition))
        /* (an inline gnu_inline function is not emitted at all: a plain
           global symbol, C's GNU extern inline, gnu_attributes) */
        flags |= AST_SYMBOL_FLAG_LINKONCE | AST_SYMBOL_FLAG_WEAK;
    /* [class.local]: a member of a local class has no linkage
       ([basic.link]/8); [impl.clang] its symbol is local unless the
       outermost function the class is in is inline or templated (then
       its copies link as one); so is a member of a closure type of
       namespace scope, unless an inline variable's initializer has it */
    if ((local_class_function(e) ||
         (x.enclosing && analysis.entity_table[x.enclosing].lambda)) &&
        !visible_local_class(analysis.entity_table, x.enclosing)) {
        storage = AST_STORAGE_STATIC;
        flags = AST_SYMBOL_FLAG_GLOBAL;
    }
}

/* the C symbol of a function or variable, made at its first use: named by
   its symbol (itanium_symbol_name) if it has one, by its identifier if it
   is automatic */
uint32_t c_lowering::symbol(entity_index e, token_index at, int variant)
{
    const entity &x = analysis.entity_table[e];
    if (x.unsupported_body && !declaring_symbols) {
        /* [temp.inst]/4: the definition of a member function used: its body
           has a construct the analysis has no rules for */
        const unsupported_construct &u =
            analysis.body_failures[x.unsupported_body - 1];
        failure.subclause = u.subclause;
        failure.file = u.file;
        failure.line = u.line;
        failure.function = u.function;
        failure.front_end = true;
        return fail("a function whose body is not supported",
                    u.node.first_token());
    }
    if (!declaring_symbols)
        for (const dropped_function &d : dropped_functions)
            if (d.entity == e) {
                failure = d.failure;
                return 0;
            }
    uint32_t *slot = &symbols[e];
    if (x.special != special_member::none && variant == 1)
        slot = &complete_symbols[e];
    if (x.special == special_member::destructor && variant == 0)
        slot = &deleting_symbols[e];
    if (*slot)
        return *slot;
    if (x.kind != entity_kind::function && x.kind != entity_kind::variable)
        return fail("an entity that is not a function or variable", at);
    /* [dcl.fct.def.delete]/2: a deleted function is never called in a
       well-formed program (a deleted destructor's object destroyed: the
       analysis does not see each destruction) */
    if (x.kind == entity_kind::function && x.deleted)
        return fail("a call of a deleted function", at);
    if (x.kind == entity_kind::variable && x.nonstatic_member)
        return fail("a non-static data member as an object", at);
    ast_qualified_type t = entity_type(e, at);
    if (variant == 2 && with_virtual_bases(e))
        t = structor_type(e, at);
    /* [basic.start.static]/2: an object of static storage duration
       initialized dynamically is written at start-up, so not in read-only
       memory whatever its type's cv-qualifiers; nor is one whose class
       has a destructor that runs, [class.dtor]/5: "const and volatile
       semantics are not applied on an object under destruction"; nor is
       an object with a mutable subobject, [dcl.stc]/8 */
    qualified_type element;
    if (x.kind == entity_kind::variable && !x.automatic && x.definition &&
        (x.type.cv() & cv_const) &&
        (!constant_initializer(x.definition) ||
         element_destructor(x.type, element) || has_mutable_subobject(x.type)))
        t = type(
            qualified_type::make(x.type.unqualified(), x.type.cv() & ~cv_const),
            at);
    owned_buffer name;
    uint32_t s = 0;
    if (x.kind == entity_kind::variable && x.automatic) {
        spelling(e, name);
        s = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(), name.size(),
                               t, AST_STORAGE_AUTO, 0, 1);
        *slot = s;
        gnu_attributes(e, s);
        return s;
    }
    if (!itanium_symbol_name(analysis, tokens.pp, e, name.get(), variant))
        return fail("an entity whose symbol is not mangled", at);
    int kind = AST_SYMBOL_VARIABLE;
    if (x.kind == entity_kind::function)
        kind = AST_SYMBOL_FUNCTION;
    int storage = AST_STORAGE_NONE;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    entity_index function = enclosing_function(e);
    if (function)
        symbol_linkage(function, storage, flags);
    /* [dcl.inline]/6: "A static local variable in an inline function with
       external or module linkage always refers to the same object": a
       static local of a function emitted weak in every translation unit
       is weak too (clang's linkonce_odr) */
    if (function && kind == AST_SYMBOL_VARIABLE &&
        storage == AST_STORAGE_NONE && (flags & AST_SYMBOL_FLAG_LINKONCE))
        flags = AST_SYMBOL_FLAG_GLOBAL | AST_SYMBOL_FLAG_LINKONCE |
                AST_SYMBOL_FLAG_WEAK;
    else if (function) {
        /* a local static: [basic.stc.static]/1, its symbol the mangled one
           (codegen writes an assembler name as given) */
        owned_buffer written;
        if (target == PP_DARWIN)
            vbuffer_string_append(written.get(), "_");
        vbuffer_append(written.get(), name.data(), name.size());
        owned_buffer identifier;
        spelling(e, identifier);
        s = ast_builder_symbol(c, kind, identifier.data(), identifier.size(), t,
                               AST_STORAGE_STATIC, 0, 1);
        ast_builder_symbol_assembler_name(c, s, written.data(), written.size());
        *slot = s;
        gnu_attributes(e, s);
        return s;
    }
    if (!function)
        symbol_linkage(e, storage, flags);
    /* [ext:clang_c11_thread_local] thread storage duration, C's
       _Thread_local (accessed directly: no wrapper function) */
    if (x.kind == entity_kind::variable && x.c11_thread_local)
        flags |= AST_SYMBOL_FLAG_THREAD;
    s = ast_builder_symbol(c, kind, name.data(), name.size(), t, storage, flags,
                           0);
    *slot = s;
    if (x.asm_label) {
        /* [ext:gnu_asm_labels] the symbol is the label's characters, as
           written (codegen writes an assembler name as given): the
           characters between each piece's quotes (sema took no escape
           sequence) */
        owned_buffer written, label;
        tokens.spelling(x.asm_label, written.get());
        bool inside = false;
        for (size_t i = 0; i < written.size(); i++) {
            if (written.data()[i] == '"')
                inside = !inside;
            else if (inside)
                vbuffer_append(label.get(), written.data() + i, 1);
        }
        ast_builder_symbol_assembler_name(c, s, label.data(), label.size());
    }
    ast_builder_toplevel(c, s);
    gnu_attributes(e, s);
    return s;
}

/* [ext:gnu_attributes] what the entity's GNU attributes ask of its
   symbol, as C's front end gives them */
void c_lowering::gnu_attributes(entity_index e, uint32_t s)
{
    const entity &x = analysis.entity_table[e];
    uint32_t flags = 0;
    if (x.gnu_flags & gnu_noinline)
        flags |= AST_SYMBOL_FLAG_NOINLINE;
    if (x.gnu_flags & gnu_always_inline)
        flags |= AST_SYMBOL_FLAG_ALWAYS_INLINE;
    if (x.gnu_flags & gnu_noreturn)
        flags |= AST_SYMBOL_FLAG_NORETURN;
    if (x.gnu_flags & gnu_weak)
        flags |= AST_SYMBOL_FLAG_WEAK;
    /* an inline gnu_inline function: C's GNU extern inline, which the
       code generator inlines and never emits */
    if ((x.gnu_flags & gnu_inline_definition) && x.inline_)
        flags |= AST_SYMBOL_FLAG_GNU_INLINE;
    if (flags)
        ast_builder_symbol_flags_add(c, s, flags);
    if (x.gnu_flags & gnu_used)
        ast_builder_symbol_keep(c, s);
    /* [ext:gnu_attributes] constructor and destructor: an entry of the
       program's .init_array or .fini_array, ordered by the priority, where
       the function is defined; it is emitted though nothing refers to it */
    if ((x.gnu_flags & (gnu_constructor | gnu_destructor)) && x.defined) {
        if (x.gnu_flags & gnu_constructor)
            ast_builder_symbol_constructor(c, s);
        else
            ast_builder_symbol_destructor(c, s);
        if (x.gnu_priority)
            ast_builder_symbol_priority(c, s, x.gnu_priority);
        ast_builder_symbol_keep(c, s);
    }
    /* cleanup: the function of a local variable, called with its address
       when it goes out of scope (C's code generator does it at each way
       out of the scope) */
    if (x.gnu_flags & gnu_cleanup) {
        uint32_t function = symbol(x.gnu_cleanup, 0);
        if (function)
            ast_builder_symbol_cleanup(c, s, function);
    }
    /* [ext:gnu_attributes] a class's visibility("hidden") is its
       members' (and its nested classes') */
    bool hidden = x.gnu_flags & gnu_hidden_visibility;
    for (entity_index k = x.enclosing;
         !hidden && k &&
         analysis.entity_table[k].kind == entity_kind::class_type;
         k = analysis.entity_table[k].enclosing)
        hidden = analysis.entity_table[k].gnu_flags & gnu_hidden_visibility;
    if (hidden)
        ast_builder_symbol_visibility(c, s, "hidden", 6);
    if (x.section) {
        /* the string-literal's characters between its quotes (sema took
           one with no escape sequence) */
        owned_buffer written;
        tokens.spelling(x.section, written.get());
        ast_builder_symbol_section(c, s, written.data() + 1,
                                   written.size() - 2);
    }
    if (x.kind == entity_kind::variable && x.alignment)
        ast_builder_symbol_align(c, s, x.alignment);
}

/* [abi:mangling-special-ctor-dtor]: an inline constructor or destructor
   is emitted as its base object variant alone, which its calls use (clang
   makes the complete object one an alias of it); another's calls use the
   complete object variant */
uint32_t c_lowering::special_callee(entity_index e, token_index at)
{
    int variant = 1;
    if (analysis.entity_table[e].inline_ && !with_virtual_bases(e))
        variant = 2;
    uint32_t s = symbol(e, at, variant);
    if (s)
        ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
    return s;
}

uint32_t c_lowering::special_call(entity_index e, uint32_t object,
                                  const uint32_t *arguments, uint32_t count,
                                  token_index at, uint32_t vtt_argument)
{
    uint32_t s = 0;
    ast_qualified_type f = 0;
    if (vtt_argument) {
        /* the base object variant, given its sub-VTT */
        s = symbol(e, at, 2);
        if (s)
            ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
        f = structor_type(e, at);
    } else {
        s = special_callee(e, at);
        f = entity_type(e, at);
    }
    if (!s || !object)
        return 0;
    uint32_t callee = cast(make(AST_NODE_VARIABLE, f, s, 0, 0, 0, 0),
                           pointer_to(f), AST_CAST_FUNCTION_DECAY);
    owned_array<uint32_t> all;
    all.append(object);
    if (vtt_argument)
        all.append(vtt_argument);
    for (uint32_t i = 0; i < count; i++)
        all.append(arguments[i]);
    uint32_t n = static_cast<uint32_t>(all.size());
    return make(AST_NODE_CALL, v, callee, ast_builder_list(c, all.data(), n), n,
                0, 0);
}

} // namespace cplusplus::lowering
